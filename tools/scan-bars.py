#!/usr/bin/env python3
"""Scan dumped A7R3 frames for a painted full-width bar.

The payload writes RGB565 0xF800 / 0x07E0 into PA 0x202E0200; the model dumps
that surface after a Morton reorder, so a painted bar shows up as 40 identical
rows of the same 16-bit value.  Prints frame number + the value + which rows.
"""
import glob
import os
import re
import struct
import sys

W, H = 640, 480


def rows_of(path):
    data = open(path, "rb").read()
    if len(data) < W * H * 2:
        return None
    return data


def uniform_rows(data, y0, y1, expect=None):
    n = 0
    for y in range(y0, y1):
        row = data[y * W * 2:(y + 1) * W * 2]
        vals = set(struct.unpack_from("<%dH" % W, row))
        if len(vals) == 1:
            v = next(iter(vals))
            if v not in (0x0000, 0x0fff, 0xffff) and (expect is None or v == expect):
                n += 1
    return n


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/paint-frames"
    files = sorted(glob.glob(os.path.join(d, "frame_8*.raw")))
    print("frames: %d" % len(files))
    best = []
    for f in files:
        data = rows_of(f)
        if not data:
            continue
        top = uniform_rows(data, 0, 40)
        mid = uniform_rows(data, 200, 240)
        if top >= 30 or mid >= 30:
            n = re.search(r"frame_(\d+)", f).group(1)
            print("HIT frame %s top=%d mid=%d" % (n, top, mid))
            best.append((n, top, mid))
    if not best:
        print("no painted bar found in periodic dumps")


if __name__ == "__main__":
    main()
