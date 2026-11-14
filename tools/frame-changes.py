#!/usr/bin/env python3
"""How often does a dumped surface actually change?

If consecutive dumps are identical the surface is idle, which means a payload
write into it *should* survive; if they keep changing the app is repainting it
continuously and a raw write can never win.

usage: frame-changes.py DIR [frame_8*.raw]
"""
import glob
import hashlib
import os
import re
import sys


def main():
    d = sys.argv[1]
    pat = sys.argv[2] if len(sys.argv) > 2 else "frame_8*.raw"
    files = sorted(glob.glob(os.path.join(d, pat)),
                   key=lambda p: int(re.search(r"(\d+)", os.path.basename(p)).group(1)))
    prev = None
    changes = 0
    runs = []
    run = 0
    for f in files:
        h = hashlib.sha256(open(f, "rb").read()).hexdigest()
        if prev is None:
            prev, run = h, 1
            continue
        if h == prev:
            run += 1
        else:
            runs.append(run)
            run = 1
            changes += 1
        prev = h
    runs.append(run)
    print("frames=%d distinct_transitions=%d" % (len(files), changes))
    print("identical run lengths: %s" % runs[:20])
    if runs:
        print("longest stable run: %d frames (~%.1f s at 0.7 s/frame)"
              % (max(runs), max(runs) * 0.7))


if __name__ == "__main__":
    main()
