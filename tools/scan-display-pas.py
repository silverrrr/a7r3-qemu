#!/usr/bin/env python3
"""Search the real-device dumps for the display buffer addresses.

We decoded (in QEMU) that the UI presents as
    0x202E0200 (render target) -> 0x23A80000 / 0x23B16000 (flip buffers)
This scans the full partition dumps (kernel, /usr, /system ...) for those
constants to see which binary programs them, i.e. whether the addresses are a
fixed part of the display path (not a QEMU artefact).

usage: scan-display-pas.py [--dir dumps]
"""
import argparse
import glob
import os
import struct
import sys

PAS = {
    0x202E0200: "render-target",
    0x23A80000: "flip-A",
    0x23B16000: "flip-B",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="/mnt/f/project/a7r3/dumps")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.dir, "*.bin")))
    if not files:
        print("no *.bin under %s" % args.dir, file=sys.stderr)
        return 1

    for path in files:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            data = f.read()
        hits = {pa: [] for pa in PAS}
        for pa, name in PAS.items():
            pat = struct.pack("<I", pa)
            start = 0
            while True:
                i = data.find(pat, start)
                if i < 0:
                    break
                hits[pa].append(i)
                start = i + 1
        total = sum(len(v) for v in hits.values())
        if not total:
            continue
        print("%s (%d bytes)" % (os.path.basename(path), size))
        for pa, name in PAS.items():
            if hits[pa]:
                print("  %-13s %08x  hits=%d  first=0x%x"
                      % (name, pa, len(hits[pa]), hits[pa][0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
