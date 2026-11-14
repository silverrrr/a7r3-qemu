#!/usr/bin/env python3
"""Find aligned display-buffer constants in the firmware trees.

Only 4-byte-aligned words that are exactly 0x202E0200 / 0x23A80000 / 0x23B16000
count (unaligned byte coincidences are noise).  Prints the words around each hit
so the owning module/function can be identified.

usage: find-display-pas2.py <dir> [<dir> ...]
"""
import os
import struct
import sys

PAS = {0x202E0200: "render-target", 0x23A80000: "flip-A", 0x23B16000: "flip-B"}


def main():
    roots = sys.argv[1:]
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
                for pa, nm in PAS.items():
                    pat = struct.pack("<I", pa)
                    start = 0
                    while True:
                        i = data.find(pat, start)
                        if i < 0:
                            break
                        if i % 4 == 0:
                            hits.append((i, pa, nm))
                        start = i + 1
                if not hits:
                    continue
                rel = os.path.relpath(path, root)
                print("%s  (%d aligned hits)" % (rel, len(hits)))
                for i, pa, nm in hits[:16]:
                    words = []
                    for k in range(-6, 7):
                        o = i + k * 4
                        if o < 0 or o + 4 > len(data):
                            words.append("--------")
                            continue
                        v = struct.unpack_from("<I", data, o)[0]
                        words.append("%08x" % v)
                    print("  %-13s off=0x%x  %s" % (nm, i, " ".join(words)))
                if len(hits) > 16:
                    print("  ... %d more" % (len(hits) - 16))
    return 0


if __name__ == "__main__":
    sys.exit(main())
