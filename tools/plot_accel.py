#!/usr/bin/env python3
"""Plot a decimated AMI accelerometer CSV in g.

Usage:
    python3 tools/plot_accel.py capture.csv
    python3 tools/plot_accel.py capture.csv --hz 100 --output accel.png

Requires matplotlib: python3 -m pip install matplotlib
"""

import argparse
import csv
import sys
from pathlib import Path

SAMPLE_RATE_HZ = 32_000
COUNTS_PER_G = 500  # ADXL383 at the firmware's +/-60 g setting
REQUIRED_COLUMNS = {"x", "y", "z"}


def load_decimated(path, stride):
    time_s, x_g, y_g, z_g = [], [], [], []
    with open(path, newline="") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames or not REQUIRED_COLUMNS.issubset(reader.fieldnames):
            raise ValueError("CSV must have x, y, and z columns")
        for index, row in enumerate(reader):
            if index % stride:
                continue
            time_s.append(index / SAMPLE_RATE_HZ)
            x_g.append(int(row["x"]) / COUNTS_PER_G)
            y_g.append(int(row["y"]) / COUNTS_PER_G)
            z_g.append(int(row["z"]) / COUNTS_PER_G)
    return time_s, x_g, y_g, z_g


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", help="CSV produced by tools/dump_flash.py or dump csv")
    parser.add_argument("--hz", type=float, default=100.0,
                        help="approximate plotted sample rate (default: 100 Hz)")
    parser.add_argument("--output", help="save the plot to a file instead of opening a window")
    args = parser.parse_args()
    if not 0 < args.hz <= SAMPLE_RATE_HZ:
        parser.error("--hz must be greater than 0 and no more than 32000")
    stride = max(1, round(SAMPLE_RATE_HZ / args.hz))

    try:
        if args.output:
            import matplotlib
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("Install matplotlib: python3 -m pip install matplotlib") from exc

    time_s, x_g, y_g, z_g = load_decimated(args.csv_file, stride)
    if not time_s:
        raise ValueError("CSV contains no accelerometer samples")

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(time_s, x_g, label="X", linewidth=0.8)
    ax.plot(time_s, y_g, label="Y", linewidth=0.8)
    ax.plot(time_s, z_g, label="Z", linewidth=0.8)
    ax.set(xlabel="Recorded time (s, nominal)", ylabel="Acceleration (g)",
           title=f"AMI acceleration (about {SAMPLE_RATE_HZ / stride:g} Hz plotted)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    # Headless Matplotlib selects Agg, where show() only emits a warning.
    output = args.output
    if output is None and plt.get_backend().lower() == "agg":
        input_path = Path(args.csv_file)
        output = str(input_path.with_name(f"{input_path.stem}_plot.png"))
    if output:
        fig.savefig(output, dpi=150)
        print(f"Saved {len(time_s)} plotted XYZ samples to {output}")
    else:
        plt.show()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"plot failed: {exc}", file=sys.stderr)
        sys.exit(1)
