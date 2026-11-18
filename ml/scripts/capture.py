"""Capture one labeled batch from USART or a marked synthetic stream."""

import argparse

import csv

from collections import deque

from datetime import datetime, timezone

from math import isfinite

from pathlib import Path

import sys

from time import monotonic, sleep

from uuid import uuid4



from serial import Serial, SerialException

from serial.tools.list_ports import comports



FIELDS = ("t_ms", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps")

HEADER = ",".join(FIELDS)

LABELS = ("CW", "CCW", "NONE")

SYNTHETIC_MARKER = b"# SOURCE=synthetic"

DATA = Path(__file__).resolve().parents[1] / "data"





class LivePlot:

    """Display the latest five seconds without slowing down CSV writes."""



    def __init__(self):

        import matplotlib.pyplot as plt



        self.plt = plt

        self.figure, axes = plt.subplots(2, 1, sharex=True)

        self.axes = axes

        self.lines = []

        self.samples = deque(maxlen=500)

        self.next_draw = 0.0

        for axis, unit in zip(axes, ("g", "deg/s")):

            self.lines.extend(axis.plot([], [], label=name)[0] for name in "XYZ")

            axis.set_ylabel(unit)

            axis.legend(loc="upper right")

        axes[1].set_xlabel("Device uptime (s)")

        plt.show(block=False)



    def update(self, row=None):

        if not self.plt.fignum_exists(self.figure.number):

            raise KeyboardInterrupt

        if row is not None:

            self.samples.append(row)

        now = monotonic()

        if now < self.next_draw:

            return

        self.next_draw = now + 0.1

        if self.samples:

            timestamps, *channels = zip(*self.samples)

            seconds = [t / 1000 for t in timestamps]

            for line, values in zip(self.lines, channels):

                line.set_data(seconds, values)

            for axis in self.axes:

                axis.relim()

                axis.autoscale_view()

            self.figure.canvas.draw_idle()

        self.figure.canvas.flush_events()





def capture(stream, source, label, limit=None, quiet=False, live=None):

    batch = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid4().hex[:8]

    path = DATA / source / label / f"{batch}.csv"

    path.parent.mkdir(parents=True, exist_ok=True)

    count = 0

    previous = -1

    print(f"CSV: {path}", flush=True)

    with path.open("x", newline="", encoding="utf-8") as output:

        writer = csv.writer(output)

        writer.writerow((*FIELDS, "label", "batch_id", "source"))

        try:

            for raw in stream:

                try:

                    if len(raw) > 256 or not raw.endswith(b"\n"):

                        raise ValueError("incomplete or oversized line")

                    line = raw.decode("ascii").strip()

                    if line == HEADER:

                        continue

                    if line.startswith("#"):

                        if line == SYNTHETIC_MARKER.decode() and source == "real":

                            raise SerialException("Synthetic marker received on the real-data input")

                        print(line, file=sys.stderr)

                        continue

                    cells = line.split(",")

                    if len(cells) != len(FIELDS):

                        raise ValueError(f"expected 7 columns, got {len(cells)}")

                    timestamp = int(cells[0])

                    values = [float(cell) for cell in cells[1:]]

                    if timestamp < 0 or not all(map(isfinite, values)):

                        raise ValueError("expected nonnegative timestamp and finite sensor values")

                    if timestamp <= previous:

                        raise ValueError("timestamp went backwards or repeated; start a new batch after board reset")

                except ValueError as error:

                    print(f"Rejected {raw[:100]!r}: {error}", file=sys.stderr)

                    continue

                writer.writerow((timestamp, *values, label, batch, source))

                previous = timestamp

                count += 1

                if not quiet:

                    print(line, flush=True)

                if live:

                    live.update((timestamp, *values))

                if count == limit:

                    break

        except KeyboardInterrupt:

            pass

        finally:

            print(f"Saved {count} samples to {path}", flush=True)

    return count





def serial_lines(port, idle=None):

    """Keep partial lines across serial read timeouts."""

    pending = b""

    while True:

        read = port.read_until(size=256)

        if not read:

            if idle:

                idle()

            continue

        pending += read

        if pending.endswith(b"\n"):

            yield pending

            pending = b""

        elif len(pending) >= 256:

            yield pending

            while not port.read_until(size=256).endswith(b"\n"):

                pass

            pending = b""





def capture_uart_windows(args):

    from windows import UartWindow, WindowStore



    live = LivePlot() if args.live else None

    store = WindowStore(args, "uart", live)

    print("BT6: press, move and release (150 ms to 1.5 s). Waiting for the board.", flush=True)
    while True:

        try:

            with Serial(args.port, args.baud, timeout=0.1, write_timeout=1) as port:

                frame = UartWindow()

                for line in serial_lines(port, live.update if live else None):

                    window = frame.parse(line)

                    if window:

                        store.save(*window)

                        port.write(f"A{window[0]}\n".encode("ascii"))

                        port.flush()

                    elif not line.startswith((b"W4,", b"#")):

                        try:

                            stamp, *values = line.strip().split(b",")

                            row = (int(stamp), *map(float, values))

                            if len(row) != len(FIELDS):

                                continue

                        except ValueError:

                            continue

                        if store.done():

                            return 0

                        if live and (not live.samples or row[0] > live.samples[-1][0]):

                            live.update(row)

                    elif line.startswith(b"# ERROR"):

                        print(line.decode("ascii", errors="replace").strip(), flush=True)

                    if live:

                        live.update()

        except SerialException as error:

            print(f"UART: {error}; retrying {args.port} in 2 s", flush=True)

            sleep(2)





def main():

    from windows import add_options, validate_options



    parser = argparse.ArgumentParser(description=__doc__)

    mode = parser.add_mutually_exclusive_group(required=True)

    mode.add_argument("--list", action="store_true")

    mode.add_argument("--port")

    mode.add_argument("--synthetic-input", type=Path, metavar="FILE")

    parser.add_argument("--baud", type=int, default=115200)

    parser.add_argument("--label", choices=LABELS, default="NONE")

    parser.add_argument("--count", type=int, help="Stop after this many valid samples")

    parser.add_argument("--live", action="store_true", help="Plot six axes during serial capture")

    parser.add_argument("--quiet", action="store_true")

    parser.add_argument("--trigger", action="store_true", help="Save complete BT6 windows and ACK over PA10")

    add_options(parser)

    args = parser.parse_args()

    if args.trigger:

        if not args.port or args.count is not None:

            parser.error("--trigger requires --port and uses --windows instead of --count")

        validate_options(parser, args)

        try:

            return capture_uart_windows(args)

        except KeyboardInterrupt:

            return 130

        except (OSError, ValueError) as error:

            parser.error(str(error))

    if args.windows is not None or args.stationary:

        parser.error("--windows and --stationary require --trigger")

    if args.count is not None and args.count < 1:

        parser.error("--count must be positive")

    if args.live and not args.port:

        parser.error("--live requires --port")

    if args.list:

        ports = sorted(comports())

        for port in ports:

            print(f"{port.device}  {port.description}")

        if not ports:

            print("No COM ports found.")

        return 0



    try:

        if args.synthetic_input:

            with args.synthetic_input.open("rb") as stream:

                if stream.readline().strip() != SYNTHETIC_MARKER:

                    parser.error("synthetic input must start with # SOURCE=synthetic")

                count = capture(stream, "synthetic", args.label, args.count, args.quiet)

        else:

            with Serial(args.port, args.baud, timeout=0.1 if args.live else 0.5) as port:

                live = LivePlot() if args.live else None

                lines = serial_lines(port, live.update if live else None)

                count = capture(lines, "real", args.label, args.count, args.quiet, live)

    except (OSError, ValueError) as error:

        print(f"Capture failed: {error}", file=sys.stderr)

        return 1

    return int(count == 0 or (args.count is not None and count < args.count))





if __name__ == "__main__":

    sys.exit(main())
