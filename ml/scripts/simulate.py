"""Write a marked synthetic USART stream for offline capture tests."""
import argparse
from math import cos, sin
from pathlib import Path

from capture import DATA, HEADER, SYNTHETIC_MARKER


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=250)
    parser.add_argument("--with-errors", action="store_true")
    args = parser.parse_args()
    if args.count < 1:
        parser.error("--count must be positive")

    path = DATA / "synthetic" / "stream.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii") as output:
        print(SYNTHETIC_MARKER.decode(), HEADER, sep="\n", file=output)
        if args.with_errors:
            print("# ERROR synthetic I2C sample timeout", "bad,row",
                  "0,NaN,0,1,0,0,0", sep="\n", file=output)
        for index in range(args.count):
            t = index / 100
            print(f"{index * 10},{0.3 * sin(t * 5):.4f},{0.1 * cos(t * 5):.4f},"
                  f"1.0000,{30 * sin(t * 5):.3f},0.000,0.000", file=output)
    print(path)


if __name__ == "__main__":
    main()
