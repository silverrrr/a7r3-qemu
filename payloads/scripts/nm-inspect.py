#!/usr/bin/env python3
"""Inspect the most recent dumped frames: score + PNG next to them.

  nm-inspect.py [N]        (default 6 frames)

The raw frames come from the QEMU model (frame_8xxx = render target,
frame_7xxx / frame_6xxx = the two present buffers).  Converting a handful and
comparing them is the quickest way to see whether "black screen" is a dump
artifact or a real one.
"""
import os
import subprocess
import sys
import time

RAW = "/mnt/f/project/a7r3/analysis/live/raw"
SHOTS = "/mnt/f/project/a7r3/analysis/shots"
TOOL = "/mnt/f/project/a7r3/analysis/qemu_a7r3/raw2png.py"


def score(path):
    """Count sampled pixels whose RGB is non-zero (alpha is always 0xf)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return -1
    return sum(1 for i in range(0, len(data) - 1, 32)
               if (data[i] & 0xF0) or data[i + 1])


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    names = [f for f in os.listdir(RAW) if f.startswith("frame_")]
    entries = []
    now = time.time()
    for name in names:
        p = os.path.join(RAW, name)
        try:
            m = os.path.getmtime(p)
        except OSError:
            continue
        entries.append((m, name, p))
    entries.sort(reverse=True)
    os.makedirs(SHOTS, exist_ok=True)
    print("%-16s %8s %8s  %s" % ("frame", "age(s)", "score", "png"))
    for m, name, p in entries[:n]:
        s = score(p)
        out = os.path.join(SHOTS, "insp_" + name.replace(".raw", ".png"))
        subprocess.run([sys.executable, TOOL, p, out],
                       check=False, capture_output=True)
        print("%-16s %8.1f %8d  %s" % (name, now - m, s, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
