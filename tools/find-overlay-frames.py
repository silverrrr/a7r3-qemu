#!/usr/bin/env python3
"""Find dumped frames that contain our overlay (orange selection bar)."""
import glob
import os
import struct
import sys

W, H = 640, 480
BAR = 0xD520        # selection colour used by menu_overlay.c
HDR = 0x111F


def main():
    d = sys.argv[1]
    hits = []
    for f in sorted(glob.glob(os.path.join(d, "frame_*.raw"))):
        data = open(f, "rb").read()
        if len(data) < W * H * 2:
            continue
        vals = struct.unpack_from("<%dH" % (W * H), data)
        bar = vals.count(BAR)
        if bar > 500:
            hits.append((os.path.basename(f), bar))
    if not hits:
        print("no overlay frames found")
        return 1
    print("overlay frames: %d" % len(hits))
    for name, n in hits[:6]:
        print("  %s bar_pixels=%d" % (name, n))
    print("  ...")
    for name, n in hits[-3:]:
        print("  %s bar_pixels=%d" % (name, n))
    print("LAST=%s" % hits[-1][0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
