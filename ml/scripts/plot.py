"""Plot the six channels from one saved capture."""
import argparse
from pathlib import Path

from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
import pandas as pd

from capture import FIELDS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--show", action="store_true", help="Open an interactive sample plot")
    args = parser.parse_args()
    try:
        data = pd.read_csv(args.csv, usecols=lambda column: column in (*FIELDS, "label", "source",
                           "protocol_version", "duration_ms", "truncated"))
        if data.empty:
            parser.error("CSV contains no samples")
        time = ((data["t_ms"] - data["t_ms"].iloc[0]) % 2**32) / 1000
        if args.show:
            import matplotlib.pyplot as plt
            fig = plt.figure(figsize=(9, 6), layout="constrained")
        else:
            fig = Figure(figsize=(9, 6), layout="constrained")
            FigureCanvasAgg(fig)
        axes = fig.subplots(2, 1, sharex=True)
        for axis, fields, unit in zip(axes, (FIELDS[1:4], FIELDS[4:7]), ("g", "deg/s")):
            for field, direction in zip(fields, "XYZ"):
                axis.plot(time, data[field], label=direction)
            axis.set_ylabel(unit)
            axis.legend()
            axis.grid(alpha=0.3)
        axes[1].set_xlabel("Time since first sample (s)")
        title = f"{data['source'].iloc[0]} / {data['label'].iloc[0]}"
        if "duration_ms" in data:
            duration = data["duration_ms"].iloc[0]
            axes[1].set_xlim(0, max(duration / 1000, time.iloc[-1]))
            cutoff = " / capped" if data["truncated"].iloc[0] else ""
            title += f" / v{data['protocol_version'].iloc[0]} / {duration} ms{cutoff}"
        fig.suptitle(title)
        if args.output or not args.show:
            output = args.output or args.csv.with_suffix(".png")
            fig.savefig(output, dpi=140)
            print(output)
        if args.show:
            plt.show()
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
