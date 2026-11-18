"""Capture the GestureCapture RAM buffer through ST-Link."""
import argparse
from pathlib import Path
import re
from struct import Struct
from time import monotonic, sleep

from pyocd.core.helpers import ConnectHelper
from pyocd.core.exceptions import Error

from windows import SAMPLE, valid_window, WindowStore, add_options, physical, validate_options

from capture import LABELS, LivePlot, capture

HEADER = Struct("<8I")
MAGIC = 0x47535734
CAPACITY = 256
DEFAULT_MAP = Path(__file__).resolve().parents[1] / "firmware/Listings/GestureCapture.map"
SENSORS = {0x68: "MPU6050", 0x70: "MPU6500"}
ERRORS = {
    1: "WHO_AM_I read failed",
    2: "Unexpected WHO_AM_I",
    3: "Sensor wake failed",
    4: "Sensor configuration failed",
    5: "Six-axis read failed",
    6: "I2C initial START failed (SCL or SDA stuck low)",
    7: "I2C address 0x68 write phase failed; 0x69 did not ACK either",
    8: "I2C register index was not acknowledged",
    9: "I2C STOP or read START after register selection failed",
    10: "I2C read address was not acknowledged",
    11: "I2C data or STOP phase failed",
    12: "I2C address 0x69 ACKed; expected 0x68 with AD0 low",
}


def read_header(target, address):
    header = HEADER.unpack(bytes(target.read_memory_block8(address, HEADER.size)))
    if header[0] != MAGIC:
        raise RuntimeError("SWD capture buffer absent; flash the matching firmware")
    return header


def read_rows(target, address, first, count):
    offset = first % CAPACITY
    start = address + HEADER.size + offset * SAMPLE.size
    raw = bytes(target.read_memory_block8(start, count * SAMPLE.size))
    return SAMPLE.iter_unpack(raw)


def csv_line(row):
    timestamp, ax, ay, az, gx, gy, gz = row
    return (f"{timestamp},{ax/16384:.4f},{ay/16384:.4f},{az/16384:.4f},"
            f"{gx/131:.3f},{gy/131:.3f},{gz/131:.3f}\n").encode()


def samples(target, address, idle, timeout=None):
    next_sample = None
    previous_status = None
    last_sample_at = monotonic()
    while True:
        _, head, who, status, *_ = read_header(target, address)
        if (who, status) != previous_status:
            previous_status = (who, status)
            if status:
                yield f"# ERROR {ERRORS.get(status, status)} (WHO_AM_I=0x{who:02X})\n".encode()
            elif who in SENSORS:
                yield f"# OK {SENSORS[who]} WHO_AM_I=0x{who:02X}\n".encode()
        if next_sample is None:
            next_sample = head
        if head < next_sample:
            raise RuntimeError("MCU reset detected; start a new capture")
        if head - next_sample >= CAPACITY:
            dropped = head - next_sample - CAPACITY + 1
            next_sample += dropped
            yield f"# WARNING SWD buffer overrun: {dropped} dropped samples\n".encode()
        count = min(head - next_sample, CAPACITY - next_sample % CAPACITY, 32)
        if not count:
            if timeout is not None and monotonic() - last_sample_at >= timeout:
                raise RuntimeError("Timed out waiting for a valid six-axis sample")
            if idle:
                idle()
            sleep(0.02)
            continue
        rows = read_rows(target, address, next_sample, count)
        _, head_after, *_ = read_header(target, address)
        if head_after < head:
            raise RuntimeError("MCU reset detected; start a new capture")
        if head_after - next_sample >= CAPACITY:
            continue
        next_sample += count
        last_sample_at = monotonic()
        for row in rows:
            yield csv_line(row)


PENDING = Struct("<7I")


def capture_address(map_path, name):
    match = re.search(rf"^\s*{name}\s+(0x[0-9a-fA-F]+)\s+Data\b",
                      map_path.read_text(), re.MULTILINE)
    if not match:
        raise RuntimeError(f"{name} is missing from the Keil map")
    return int(match[1], 16)


def connect_probe(pack, probe=None, frequency=1_000_000):
    return ConnectHelper.session_with_chosen_probe(
        unique_id=probe, blocking=False, target_override="stm32f103c8",
        pack=str(pack), frequency=frequency, connect_mode="attach",
        no_config=True, resume_on_disconnect=False,
    )


def pending_header(target, address):
    return PENDING.unpack(bytes(target.read_memory_block8(address, PENDING.size)))


def read_pending(target, address, header):
    number, count, who, ready, ack, duration, truncated = header
    if not ready or number == ack:
        return None
    if not valid_window(count, duration, truncated):
        raise RuntimeError(f"Invalid pending window: count={count}, duration={duration}, truncated={truncated}")
    raw = bytes(target.read_memory_block8(address + PENDING.size, count * SAMPLE.size))
    if pending_header(target, address) != header:
        return None
    return number, who, list(SAMPLE.iter_unpack(raw)), duration, truncated


def acknowledge(target, address, number):
    target.write32(address + 16, number)
    target.flush()


def triggered(target, address, pending, store):
    previous = None
    while True:
        _, head, who, status, state, _, _, cancelled = read_header(target, address)
        current = (who, status, state, cancelled)
        if current != previous:
            print(f"BT6 state={state}, cancelled={cancelled}, sensor={ERRORS.get(status, status)}", flush=True)
            previous = current
        header = pending_header(target, pending)
        ready = header[3]
        window = read_pending(target, pending, header)
        if window:
            store.save(*window)
            acknowledge(target, pending, window[0])
        elif not ready and store.done():
            return
        if store.live:
            if not ready and head:
                row, = read_rows(target, address, head - 1, 1)
                if not store.live.samples or row[0] > store.live.samples[-1][0]:
                    store.live.update(physical(row))
            store.live.update()
        sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--probe", help="ST-Link unique ID (optional for one probe)")
    parser.add_argument("--swd-hz", type=int, default=1_000_000,
                        help="SWD clock frequency in Hz (default: 1000000)")
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--label", choices=LABELS, default="NONE")
    parser.add_argument("--count", type=int)
    parser.add_argument("--trigger", action="store_true", help="Save one CSV per completed BT6 hold")
    add_options(parser)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    if args.swd_hz < 1:
        parser.error("--swd-hz must be positive")
    if args.count is not None and args.count < 1:
        parser.error("--count must be positive")
    if args.trigger:
        validate_options(parser, args)
        if args.count is not None:
            parser.error("--trigger uses --windows instead of --count")
    elif args.windows is not None or args.stationary:
        parser.error("--windows and --stationary require --trigger")
    try:
        import recognition_swd as recognition

        address = capture_address(args.map, "g_swd_capture")
        mode_address = recognition.address_for(args.map, optional=True)

        def select_capture(target):
            if mode_address is None:
                return
            deadline = monotonic() + 5
            while not recognition.request_mode(target, mode_address,
                    recognition.header(target, mode_address), recognition.CAPTURE):
                if monotonic() >= deadline:
                    raise RuntimeError("Release BT6 to switch to capture mode")
                sleep(.05)
        live = LivePlot() if args.live else None
        if args.trigger:
            pending = capture_address(args.map, "g_pending_window")
            store = WindowStore(args, "swd", live)
            while True:
                try:
                    session = connect_probe(args.pack, args.probe, args.swd_hz)
                    if session is None:
                        raise ConnectionError("No matching ST-Link")
                    with session:
                        select_capture(session.target)
                        triggered(session.target, address, pending, store)
                    return 0
                except (Error, ConnectionError) as error:
                    print(f"SWD: {error}; retrying in 2 s without resetting the board", flush=True)
                    sleep(2)
        session = connect_probe(args.pack, args.probe, args.swd_hz)
        if session is None:
            parser.error("No matching ST-Link found")
        with session:
            select_capture(session.target)
            stream = samples(session.target, address, live.update if live else None,
                             timeout=5 if args.count is not None else None)
            count = capture(stream, "real", args.label, args.count, args.quiet, live)
        return int(count == 0 or (args.count is not None and count < args.count))
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, ValueError, Error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
