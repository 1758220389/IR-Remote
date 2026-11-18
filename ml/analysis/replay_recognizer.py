"""Replay frozen V1 windows through the ARMCC-built C function on the STM32.

This is an intrusive SWD test: it pauses/resets the independent firmware and uses
otherwise unallocated RAM. It never writes Flash.
"""
import argparse
import csv
import ctypes as ct
from contextlib import contextmanager
import json
from pathlib import Path
import re
from struct import Struct
import sys
from time import monotonic

from intelhex import IntelHex
import joblib
import numpy as np
import pandas as pd
from pyocd.core.target import Target

from train_v1 import load, ML
from features import extract

sys.path.insert(0, str(ML / "scripts"))
import capture_swd as swd

RESULT = Struct("<4f2I")
SAMPLES, OUTPUT, STUB, STACK = (0x20003000, 0x20003A00, 0x20003B00, 0x20004F00)


def code_address(path):
    match = re.search(r"^\s*gesture_predict\s+(0x[0-9a-fA-F]+)\s+Thumb Code",
                      path.read_text(), re.M)
    region = re.search(r"Execution Region RW_IRAM1 \(Exec base: (0x[0-9a-fA-F]+).*?Size: (0x[0-9a-fA-F]+)",
                       path.read_text())
    assert sum(int(v, 16) for v in region.groups()) <= SAMPLES
    return int(match[1], 16) & ~1


def execute(target, fn, rows, duration, truncated):
    raw = b"".join(swd.SAMPLE.pack(*row) for row in rows)
    target.write_memory_block8(SAMPLES, list(raw))
    target.write32(STACK, int(truncated))
    target.write_core_registers_raw(["r0", "r1", "r2", "r3", "sp", "lr", "pc", "xpsr"],
                                   [OUTPUT, SAMPLES, len(rows), duration, STACK, STUB | 1, fn, 1 << 24])
    target.flush()
    target.resume()
    deadline = monotonic() + 1
    while target.get_state() != Target.State.HALTED:
        if monotonic() >= deadline:
            target.halt()
            raise RuntimeError("C replay did not return")
    if target.read_core_register("pc") not in (STUB, STUB + 2):
        raise RuntimeError(f"C replay stopped outside return stub: {target.read_core_register('pc'):#x}")
    return RESULT.unpack(bytes(target.read_memory_block8(OUTPUT, RESULT.size)))


@contextmanager
def board_runner(pack):
    fn = code_address(swd.DEFAULT_MAP)
    image = IntelHex(str(ML / "firmware/Objects/GestureCapture.hex"))
    session = swd.connect_probe(pack)
    if session is None:
        raise SystemExit("ST-Link unavailable")
    with session:
        target = session.target
        assert bytes(target.read_memory_block8(fn, 64)) == bytes(image.tobinarray(start=fn, end=fn + 63))
        assert not swd.pending_header(target, swd.capture_address(swd.DEFAULT_MAP, "g_pending_window"))[3]
        target.halt()
        try:
            target.write_core_register("primask", 1)
            target.write16(STUB, 0xBE00)
            yield lambda rows, duration, truncated: execute(target, fn, rows, duration, truncated)
        finally:
            target.reset(Target.ResetType.SYSRESETREQ)


@contextmanager
def host_runner(path):
    class Sample(ct.Structure):
        _fields_ = [("t_ms", ct.c_uint32), ("axes", ct.c_int16 * 6)]

    class Prediction(ct.Structure):
        _fields_ = [("angle", ct.c_float), ("scores", ct.c_float * 3),
                    ("label", ct.c_uint32), ("flags", ct.c_uint32)]

    dll = ct.CDLL(str(path.resolve()))
    function = dll.gesture_predict
    function.argtypes = [ct.POINTER(Sample), ct.c_uint, ct.c_uint32, ct.c_uint32]
    function.restype = Prediction

    def run(rows, duration, truncated):
        samples = (Sample * len(rows))(*(Sample(t, (ct.c_int16 * 6)(*axes)) for t, *axes in rows))
        result = function(samples, len(rows), duration, truncated)
        return result.angle, *result.scores, result.label, result.flags

    yield run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--pack", type=Path)
    source.add_argument("--host-dll", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ML / "models/v1" / ("host_c_parity.csv" if args.host_dll else "mcu_parity.csv")
    if output.resolve().is_relative_to((ML / "data/real").resolve()):
        parser.error("output must be outside the raw capture tree")
    data, signals = load(ML / "data/v1/manifest.csv")
    folder = ML / "models/v1"
    model = joblib.load(folder / "recommended_C.joblib")["model"]
    details = json.loads((folder / "recommended_C.json").read_text())
    results = []
    runner = host_runner(args.host_dll) if args.host_dll else board_runner(args.pack)
    with runner as run:
        for row in data.itertuples():
            with (ML / row.raw_csv).open() as f:
                frame = [(int(r["t_ms"]), *(int(r[k + "_raw"]) for k in
                          ("ax","ay","az","gx","gy","gz"))) for r in csv.DictReader(f)]
            pc = extract(*signals[row.Index], details["bias_dps"], row.duration_ms)["gyro_integral_z_deg"]
            x = pd.DataFrame({"gyro_integral_z_deg": [pc]})
            scores = model.decision_function(x)[0]
            angle, a, b, c, label, flags = run(frame, int(row.duration_ms), int(row.truncated))
            if row.truncated:
                assert label == 3 and flags & 2
            else:
                assert flags == 0 and details["classes"][label] == model.predict(x)[0]
                np.testing.assert_allclose([angle, a, b, c], [pc, *scores], atol=1e-4, rtol=0)
            margin = np.sort(scores)[-1] - np.sort(scores)[-2]
            results.append(dict(batch_id=row.batch_id, scope=row.scope, label=row.label,
                                mcu=(*details["classes"], "REJECT")[label], flags=flags,
                                pc=model.predict(x)[0], margin=margin, mcu_angle_deg=angle,
                                pc_angle_deg=pc, angle_error_deg=angle-pc,
                                max_score_error=max(abs(np.array([a,b,c])-scores))))
        origin = [(10*i, 0, 0, 0, 0, 0, 0) for i in range(15)]
        assert run(origin[:7], 70, 0)[-1] & 1
        assert run(origin[:14], 150, 0)[-1] & 4
        assert run(origin[:-1] + [(151, 0, 0, 0, 0, 0, 0)], 150, 0)[-1] & 8
        assert run(origin[:-1] + [(140, 32767, 0, 0, 0, 0, 0)], 150, 0)[-1] & 16
    table = pd.DataFrame(results)
    table.to_csv(output, index=False)
    valid = table[table["flags"].eq(0)]
    platform = "Host" if args.host_dll else "STM32 ARM"
    print(f"{platform} C: {len(valid)} matches, {len(table)-len(valid)} policy rejections")
    print(f"Max |angle error| {valid.angle_error_deg.abs().max():.8f} deg; max score error {valid.max_score_error.max():.8f}")
    print(valid.nsmallest(5, "margin")[["label","mcu","pc_angle_deg","margin"]].to_string(index=False))


if __name__ == "__main__":
    main()

