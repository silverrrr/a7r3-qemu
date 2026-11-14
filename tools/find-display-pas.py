#!/usr/bin/env python3
"""Locate the display-buffer constants inside the extracted firmware trees.

Scans every file under the given roots for the little-endian constants
0x202E0200 / 0x23A80000 / 0x23B16000 and prints file, offset and the 8 words
around the hit, so the owning module/function can be identified.

usage: find-display-pas.py <dir> [<dir> ...]
"""
import os
import struct
import sys

PAS = {0x202E0200: "render-target", 0x23A80000: "flip-A", 0x23B16000: "flip-B"}


def main():
    roots = sys.argv[1:]
    if not roots:
        print(__doc__, file=sys.stderr)
        return 1
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                path = os.path.join(dirpath, name)
                try:
                    if os.path.getsize(path) > 80 * 1024 * 1024:
                        continue
                    data = open(path, "rb").read()
                except OSError:
                    continue
                hits = []
                for pa in PAS:
                    pat = struct.pack("<I", pa)
                    start = 0
                    while True:
                        i = data.find(pat, start)
                        if i < 0:
                            break
                        hits.append((i, pa))
                        start = i + 1
                if not hits:
                    continue
                rel = os.path.relpath(path, root)
                print("%s  (%d hits)" % (rel, len(hits)))
                for i, pa in hits[:12]:
                    base = i & ~3
                    words = []
                    for k in range(-8, 9):
                        o = base + k * 4
                        if o < 0 or o + 4 > len(data):
                            words.append("--------")
                            continue
                        words.append("%08x" % struct.unpack_from("<I", data, o)[0])
                    print("  %-13s off=0x%x  %s" % (PAS[pa], i, " ".join(words)))
                if len(hits) > 12:
                    print("  ... %d more" % (len(hits) - 12))
    return 0


if __name__ == "__main__":
    sys.exit(main())
