#!/usr/bin/env python3
"""Count how many pixels in each dumped frame equal the painted marker colours."""
import glob
import os
import re
import struct
import sys

W, H = 640, 480
MARKS = {0xF800: "red565", 0x07E0: "green565"}


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/paint-frames"
    files = sorted(glob.glob(os.path.join(d, "frame_*.raw")))
    total_hits = {k: 0 for k in MARKS}
    for f in files:
        data = open(f, "rb").read()
        if len(data) < W * H * 2:
            continue
        vals = struct.unpack_from("<%dH" % (W * H), data)
        counts = {k: vals.count(k) for k in MARKS}
        for k, c in counts.items():
            if c:
                total_hits[k] += c
                n = re.search(r"(\d+)", os.path.basename(f)).group(1)
                print("frame %s %s=%d" % (n, MARKS[k], c))
    print("totals: %s" % total_hits)


if __name__ == "__main__":
    main()
