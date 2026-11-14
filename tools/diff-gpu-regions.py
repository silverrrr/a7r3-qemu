#!/usr/bin/env python3
"""Compare two dumps of the same GPU shared region.

Prints every word that changed (offset, old, new) and, when asked, all nonzero
words of either file.  Useful to find ring head/tail indices and per-frame
surface descriptors in the /dev/dmpgles2 mapping.

usage: diff-gpu-regions.py A.bin B.bin [--all] [--limit N]
"""
import argparse
import struct
import sys


def load(path):
    data = open(path, "rb").read()
    n = len(data) // 4
    return list(struct.unpack("<%dI" % n, data)), len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--limit", type=int, default=80)
    args = ap.parse_args()

    wa, la = load(args.a)
    wb, lb = load(args.b)
    n = min(len(wa), len(wb))
    diffs = [(i, wa[i], wb[i]) for i in range(n) if wa[i] != wb[i]]
    print("A %s %d bytes, B %s %d bytes, common %d words"
          % (args.a, la, args.b, lb, n))
    print("changed words: %d" % len(diffs))
    for i, x, y in diffs[: args.limit]:
        print("  off=%#06x  %08x -> %08x" % (i * 4, x, y))
    if len(diffs) > args.limit:
        print("  ... %d more" % (len(diffs) - args.limit))

    if args.all:
        for name, w in (("A", wa), ("B", wb)):
            nz = [(i, x) for i, x in enumerate(w) if x]
            print("\n%s nonzero words: %d" % (name, len(nz)))
            for i, x in nz[: args.limit]:
                print("  off=%#06x  %08x" % (i * 4, x))
    return 0


if __name__ == "__main__":
    sys.exit(main())
