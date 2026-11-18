"""Save and review immutable BT6 windows from UART or SWD."""
from binascii import crc_hqx
from datetime import datetime, timezone
from hashlib import sha256
import csv
import json
import os
from pathlib import Path
import re
from statistics import mean
from struct import Struct, error as StructError
from tempfile import TemporaryDirectory

from capture import DATA, FIELDS, LABELS

SAMPLE = Struct("<I6h")
WINDOW_SAMPLES = 150
PROTOCOL_VERSION = 4
MIN_DURATION_MS = 150
MAX_DURATION_MS = 1500
ROOT = DATA / "real" / "sessions"
AXES = ("ax", "ay", "az", "gx", "gy", "gz")


def add_options(parser):
    parser.add_argument("--session", default=datetime.now().strftime("%Y%m%d"),
                        help="Session folder name; reuse across labels and reconnects")
    parser.add_argument("--windows", type=int, help="Stop when this label has this many accepted windows")
    parser.add_argument("--stationary", action="store_true", help="Mark a stationary NONE window for gyro bias estimation")


def validate_options(parser, args):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.session):
        parser.error("use 1-64 ASCII letters, digits, underscores or hyphens in --session")
    if args.label not in LABELS or (args.windows is not None and args.windows < 1):
        parser.error("BT6 capture requires CW/CCW/NONE and a positive --windows")
    if args.stationary and args.label != "NONE":
        parser.error("--stationary requires --label NONE")


def physical(row):
    return (row[0], *(v / 16384 for v in row[1:4]), *(v / 131 for v in row[4:]))


def assess(rows, stationary, duration_ms):
    gaps = [(b[0] - a[0]) & 0xFFFFFFFF for a, b in zip(rows, rows[1:])]
    saturated = {axis: sum(row[i] in (-32768, 32767) for row in rows)
                 for i, axis in enumerate(AXES, 1)}
    timing_errors = sum(gap != 10 for gap in gaps)
    duration_consistent = abs(len(rows) * 10 - duration_ms) < 10
    quality = dict(samples=len(rows), interval_min_ms=min(gaps, default=None),
                   interval_max_ms=max(gaps, default=None), unexpected_intervals=timing_errors,
                   duration_consistent=duration_consistent, saturated=saturated,
                   passed=duration_consistent and not timing_errors and not any(saturated.values()))
    if stationary and quality["passed"]:
        quality["stationary_gyro_mean_dps"] = [mean(row[i] for row in rows) / 131 for i in (4, 5, 6)]
    return quality


def accepted(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    return data.get("protocol_version") == PROTOCOL_VERSION and data["quality"]["passed"]


def valid_window(count, duration_ms, truncated):
    return (0 < count <= WINDOW_SAMPLES and MIN_DURATION_MS <= duration_ms <= MAX_DURATION_MS
            and truncated in (0, 1) and (not truncated or duration_ms == MAX_DURATION_MS))


def durable(file):
    file.flush()
    os.fsync(file.fileno())


class WindowStore:
    def __init__(self, args, transport, live):
        self.args = args
        self.transport = transport
        self.live = live
        self.folder = ROOT / args.session / args.label
        self.report()

    def count(self):
        return sum((p / "metadata.json").is_file() and not (p / "rejected").exists()
                   and accepted(p / "metadata.json")
                   for p in self.folder.glob("w-*"))

    def report(self):
        print(f"{self.args.label}: {self.count()} accepted | session {self.args.session}", flush=True)

    def done(self):
        return self.args.windows is not None and self.count() >= self.args.windows

    def save(self, number, who, rows, duration_ms, truncated, *, excluded=False):
        if not valid_window(len(rows), duration_ms, truncated) or who not in (0x68, 0x70):
            raise ValueError("Unexpected BT6 window size or sensor identity")
        payload = (Struct("<BBIHB").pack(PROTOCOL_VERSION, who, number, duration_ms, truncated)
                   + b"".join(SAMPLE.pack(*r) for r in rows))
        key = f"w-{number}-{sha256(payload).hexdigest()}"
        for existing in ROOT.glob(f"*/*/{key}"):
            if (existing / "raw.csv").is_file() and (existing / "metadata.json").is_file():
                return existing
        quality = assess(rows, self.args.stationary, duration_ms)
        metadata = dict(window_number=number, batch_id=key, session=self.args.session,
                        label=self.args.label, transport=self.transport,
                        protocol_version=PROTOCOL_VERSION, duration_ms=duration_ms,
                        sample_count=len(rows), truncated=bool(truncated),
                        saved_utc=datetime.now(timezone.utc).isoformat(), who_am_i=f"0x{who:02X}",
                        sensor={0x68: "MPU6050", 0x70: "MPU6500"}[who], sample_rate_hz=100,
                        accel_range_g=2, gyro_range_dps=250, accel_lsb_per_g=16384,
                        gyro_lsb_per_dps=131, stationary=self.args.stationary, quality=quality)
        self.folder.mkdir(parents=True, exist_ok=True)
        dest = self.folder / key
        with TemporaryDirectory(dir=self.folder, prefix=".saving-") as tmp:
            stage = Path(tmp) / key
            stage.mkdir()
            with (stage / "raw.csv").open("x", newline="", encoding="utf-8") as out:
                writer = csv.writer(out)
                writer.writerow((*FIELDS, *(f"{axis}_raw" for axis in AXES),
                                 "label", "batch_id", "source", "protocol_version",
                                 "duration_ms", "sample_count", "truncated"))
                writer.writerows((*physical(row), *row[1:], self.args.label, key, "real",
                                  PROTOCOL_VERSION, duration_ms, len(rows), int(truncated))
                                 for row in rows)
                durable(out)
            with (stage / "metadata.json").open("x", encoding="utf-8") as out:
                json.dump(metadata, out, indent=2)
                durable(out)
            if excluded:
                (stage / "rejected").touch()
            stage.rename(dest)
        print(f"{dest / 'raw.csv'}\n{json.dumps(quality)}", flush=True)
        self.report()
        if self.live:
            self.live.samples.clear()
            self.live.samples.extend(map(physical, rows))
            self.live.update()
        return dest


class UartWindow:
    def __init__(self):
        self.key = None
        self.rows = {}

    def parse(self, line):
        if not line.startswith(b"W4,"):
            return None
        try:
            prefix, crc = line.strip().rsplit(b",", 1)
            if crc_hqx(prefix, 0) != int(crc, 16):
                return None
            number, count, who, duration_ms, truncated, index, *row = map(int, prefix.split(b",")[1:])
            if (not valid_window(count, duration_ms, truncated) or not 0 <= index < count
                    or not 0 < number < 2**32):
                return None
            SAMPLE.pack(*row)
        except (ValueError, StructError):
            return None
        if index == 0:
            self.key = (number, who, count, duration_ms, truncated)
            self.rows.clear()
        if self.key != (number, who, count, duration_ms, truncated):
            return None
        self.rows[index] = row
        if len(self.rows) == count:
            self.key = None
            return number, who, [self.rows[i] for i in range(count)], duration_ms, truncated
        return None


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("window", type=Path, help="Saved window folder or raw.csv")
    parser.add_argument("--reject", action="store_true", help="Exclude this window without changing its CSV")
    args = parser.parse_args()
    folder = args.window.parent if args.window.is_file() else args.window
    if not (folder / "metadata.json").is_file() or not (folder / "raw.csv").is_file():
        parser.error("expected a saved BT6 window")
    if args.reject:
        (folder / "rejected").touch()
        print(f"Excluded: {folder}")
    else:
        print((folder / "metadata.json").read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
