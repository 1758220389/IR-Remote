"""Rebuild the V1 manifest, exploratory features and figures from immutable captures."""
import argparse
from collections import Counter
from hashlib import sha256
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from features import extract

ML = Path(__file__).resolve().parents[1]
RAW = ML / "data/real"
GOALS = {("CW", False): 10, ("CCW", False): 10, ("NONE", True): 5, ("NONE", False): 5}
AXES = [axis + "_raw" for axis in ("ax", "ay", "az", "gx", "gy", "gz")]
GROUPS = ("CW", "CCW", "NONE static", "NONE motion")
COLORS = ("#a93450", "#2562ab", "#438574", "#c47821")


def scan():
    records, signals, seen = [], {}, set()
    for meta in sorted((RAW / "sessions").glob("*/*/*/metadata.json")):
        d = json.loads(meta.read_text(encoding="utf-8"))
        csv = meta.with_name("raw.csv")
        session = meta.parents[2].name
        row = dict(session=session, label=d["label"], stationary=d.get("stationary", False),
                   protocol=d.get("protocol_version", 3), batch_id=d["batch_id"],
                   raw_csv=csv.relative_to(ML).as_posix(), metadata=meta.relative_to(ML).as_posix(),
                   quality_passed=d["quality"]["passed"], manually_rejected=meta.with_name("rejected").exists(),
                   duration_ms=d.get("duration_ms"), window_number=d["window_number"],
                   truncated=d.get("truncated"), recorded_count=d["quality"]["samples"],
                   start_ms=None, end_ms=None, span_ms=None, sample_count=None, raw_sha256=None,
                   reason="")
        records.append(row)
        if not csv.is_file():
            row["reason"] = "missing_csv"
            continue
        row["raw_sha256"] = sha256(csv.read_bytes()).hexdigest()
        if row["protocol"] != 4 or row["label"] not in ("CW", "CCW", "NONE"):
            row["reason"] = "incompatible_protocol_or_label"
            continue
        data = pd.read_csv(csv)
        ticks = data.t_ms.to_numpy(dtype=np.int64)
        raw = data[AXES].to_numpy(dtype=np.int64)
        gaps = np.diff(ticks) % 2**32
        if len(ticks):
            row.update(start_ms=ticks[0], end_ms=ticks[-1], span_ms=int(gaps.sum()), sample_count=len(ticks))
        if session.startswith(("acceptance", "v4-acceptance", "_")):
            row["reason"] = "acceptance_or_unassigned"
        elif row["manually_rejected"]:
            row["reason"] = "manually_rejected"
        elif not row["quality_passed"]:
            row["reason"] = "capture_quality_failed"
        elif (len(ticks) < 2 or len(ticks) != row["recorded_count"] or
              len(ticks) != d["sample_count"] or not np.all(gaps == 10) or
              np.any((raw == -32768) | (raw == 32767)) or
              not (data.label.eq(row["label"]) & data.protocol_version.eq(4) &
                   data.batch_id.eq(row["batch_id"]) & data.duration_ms.eq(row["duration_ms"]) &
                   data.sample_count.eq(len(ticks)) & data.truncated.eq(int(row["truncated"]))).all()):
            row["reason"] = "csv_metadata_or_signal_mismatch"
        else:
            signature = sha256(np.column_stack((ticks, raw)).astype("<i8").tobytes()).hexdigest()
            if signature in seen:
                row["reason"] = "duplicate_raw_window"
            else:
                seen.add(signature)
                signals[row["raw_csv"]] = (ticks, raw[:, :3] / d["accel_lsb_per_g"],
                                          raw[:, 3:] / d["gyro_lsb_per_dps"])
    counts = Counter((r["session"], r["label"], r["stationary"]) for r in records if not r["reason"])
    for r in records:
        session = r["session"]
        if r["reason"]:
            continue
        if session.startswith("manual-"):
            r["scope"] = "supplemental_manual"
        elif all(counts[session, label, still] >= goal for (label, still), goal in GOALS.items()):
            r["scope"] = "complete_guided"
        else:
            r["reason"] = "incomplete_guided"
    return records, signals


def figures(features, output):
    fig, axes = plt.subplots(2, 3, figsize=(13, 7), layout="constrained")
    for ax, (key, label) in zip(axes.flat, (
        ("gyro_integral_z_deg", "Z signed angle (deg)"), ("gyro_abs_integral_z_deg", "Z absolute angle (deg)"),
        ("gyro_peak_abs_z_dps", "Z peak (deg/s)"), ("gyro_z_energy_ratio", "Z energy fraction"),
        ("duration_ms", "BT6 duration (ms)"), ("accel_norm_range_g", "Acceleration norm range (g)"),
    )):
        values = [features.loc[features.group.eq(g), key] for g in GROUPS]
        ax.boxplot(values, tick_labels=GROUPS, showfliers=False)
        for i, (v, color) in enumerate(zip(values, COLORS), 1):
            ax.scatter(i + np.linspace(-.13, .13, len(v)), v, s=8, alpha=.6, color=color)
        ax.set_ylabel(label)
        ax.grid(axis="y", alpha=.18)
    fig.savefig(output / "distributions.png", dpi=170)
    plt.close(fig)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.3), layout="constrained")
    for ax, y, ylabel in zip(axes, ("gyro_z_energy_ratio", "gyro_peak_abs_z_dps"),
                            ("Z energy fraction", "Z peak (deg/s)")):
        for group, color in zip(GROUPS, COLORS):
            part = features[features.group.eq(group)]
            ax.scatter(part.gyro_integral_z_deg, part[y], color=color, label=group, alpha=.65, s=24)
        ax.set(xlabel="Z signed angle (deg)", ylabel=ylabel)
        ax.grid(alpha=.2)
    axes[0].legend()
    fig.savefig(output / "separation.png", dpi=170)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ML / "data/v1")
    out = parser.parse_args().output.resolve()
    if out.is_relative_to(RAW.resolve()):
        parser.error("output must be outside the raw data tree")
    out.mkdir(parents=True, exist_ok=True)
    records, signals = scan()
    included = [r for r in records if not r["reason"]]
    calibration = {}
    for r in included:
        if r["stationary"] and r["label"] == "NONE":
            calibration.setdefault(r["session"], []).append((r, signals[r["raw_csv"]][2]))
    bias = {s: np.concatenate([g for _, g in items]).mean(axis=0) for s, items in calibration.items()}
    pooled = np.concatenate([g for items in calibration.values() for _, g in items]).mean(axis=0)
    table = []
    for r in included:
        mode = "session_static" if r["session"] in bias else "pooled_exploratory"
        t, a, g = signals[r["raw_csv"]]
        group = r["label"] if r["label"] != "NONE" else ("NONE static" if r["stationary"] else "NONE motion")
        table.append(dict(batch_id=r["batch_id"], session=r["session"], scope=r["scope"], label=r["label"],
                          group=group, bias_source=mode, truncated=r["truncated"],
                          **extract(t, a, g, bias.get(r["session"], pooled), r["duration_ms"])))
    features = pd.DataFrame(table)
    pd.DataFrame(records).assign(included=lambda d: d.reason.eq("")).to_csv(out / "manifest.csv", index=False)
    features.to_csv(out / "features.csv", index=False)
    bias_rows = [(s, len(calibration[s]), sum(len(g) for _, g in calibration[s]),
                  "|".join(r["batch_id"] for r, _ in calibration[s]), *v) for s, v in bias.items()]
    bias_rows.append(("pooled_exploratory", sum(map(len, calibration.values())),
                      sum(len(g) for items in calibration.values() for _, g in items),
                      "|".join(r["batch_id"] for items in calibration.values() for r, _ in items), *pooled))
    pd.DataFrame(bias_rows, columns=["session", "windows", "points", "source_batch_ids",
                                    "bias_x_dps", "bias_y_dps", "bias_z_dps"]).to_csv(out / "bias.csv", index=False)
    features.groupby(["session", "scope", "group"])[["duration_ms", "sample_span_ms", "sample_count",
        "gyro_integral_z_deg", "gyro_peak_abs_z_dps"]].agg(["count", "min", "median", "max"]).to_csv(out / "session_summary.csv")
    figures(features, out)
    print(Counter(r["reason"] or r["scope"] for r in records))
    print(features.groupby("group")[["duration_ms", "sample_count", "gyro_integral_z_deg", "gyro_peak_abs_z_dps",
                                   "gyro_z_energy_ratio"]].agg(["min", "median", "max"]).to_string())
    print(features.select_dtypes("number").corr(method="spearman").gyro_integral_z_deg.sort_values().to_string())


if __name__ == "__main__":
    main()
