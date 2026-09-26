#!/usr/bin/env python3
"""Render MLX90640 frames dumped by firmware/03_mlx90640_integration as ASCII.

Usage:
    idf.py -p PORT monitor | python3 thermal_frame_view.py
    python3 thermal_frame_view.py capture.log

Lines that start with "FRAME," hold 768 comma-separated temperatures (degC,
row-major, 32 columns x 24 rows). Everything else passes through unchanged.
"""
import fileinput
import sys

COLS, ROWS = 32, 24
SHADES = " .:-=+*#%@"


def render(values):
    lo, hi = min(values), max(values)
    span = (hi - lo) or 1.0
    lines = []
    for row in range(ROWS):
        chunk = values[row * COLS:(row + 1) * COLS]
        lines.append("".join(SHADES[min(len(SHADES) - 1, int((v - lo) / span * len(SHADES)))] * 2 for v in chunk))
    lines.append(f"min {lo:.1f} C   max {hi:.1f} C")
    return "\n".join(lines)


def main():
    for line in fileinput.input():
        idx = line.find("FRAME,")
        if idx < 0:
            sys.stdout.write(line)
            continue
        try:
            values = [float(v) for v in line[idx + 6:].strip().split(",")]
        except ValueError:
            continue
        if len(values) == COLS * ROWS:
            print(render(values), flush=True)


if __name__ == "__main__":
    main()
