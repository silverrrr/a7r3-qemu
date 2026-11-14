#!/usr/bin/env python3
"""Decode a dumped /dev/dmpgles2 mapping (GPU command/shared region).

The grm_gles surface descriptor sits in this mapping as a word run that
contains the pixel format 0x4401 followed by width 640 and height 480; the
surface physical address is five words before the format.

Known layout (offsets from the format word k, verified on A7R3):

  k-8   message/struct type      (5 = surface/display config)
  k-5   surface physical address (e.g. 0x202E0200)
  k-1   0
  k     pixel format             (0x4401 = RGBA4444)
  k+1   width   (640)
  k+2   height  (480)
  k+3   stride  (640)
  k+4   stride  (480)
  k+7   crop width  (640)
  k+8   crop height (480)

usage: decode-gpu-region.py FILE [--windows N]
"""
import argparse
import struct
import sys

FMT = 0x4401
W, H = 640, 480


def find_descs(words):
    out = []
    for k in range(8, len(words) - 9):
        if words[k] != FMT or words[k + 1] != W or words[k + 2] != H:
            continue
        out.append({
            "k": k,
            "off": (k - 12) * 4,
            "type": words[k - 8],
            "surf": words[k - 5],
            "fmt": words[k],
            "w": words[k + 1],
            "h": words[k + 2],
            "stride": words[k + 3],
            "cropw": words[k + 7],
            "croph": words[k + 8],
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--windows", type=int, default=4)
    args = ap.parse_args()

    data = open(args.path, "rb").read()
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    nz = sum(1 for w in words if w)
    print("file    %s" % args.path)
    print("bytes   %d  words %d  nonzero %d" % (len(data), len(words), nz))

    descs = find_descs(words)
    print("descr   %d" % len(descs))
    for d in descs:
        print("  off=%#06x type=%d surf=%08x fmt=%04x %dx%d stride=%d crop=%dx%d"
              % (d["off"], d["type"], d["surf"], d["fmt"], d["w"], d["h"],
                 d["stride"], d["cropw"], d["croph"]))

    # any surface-looking value (0x2xxxxxxx / 0x0xxxxxxx within the first 8K)
    cands = [(i, w) for i, w in enumerate(words[:2000])
             if 0x20000000 <= w < 0x30000000 and (w & 0xf) == 0]
    if cands:
        print("surfaces (0x2xxxxxxx, aligned) in the first 8 KiB:")
        for i, w in cands[:20]:
            print("  off=%#06x  %08x" % (i * 4, w))

    # print a readable window around each descriptor
    for d in descs[: args.windows]:
        k = d["k"]
        print("\nwindow around k=%d (offsets relative to k):" % k)
        for q in range(-12, 12):
            if k + q < 0:
                continue
            print("  k%+03d  off=%#06x  %08x" % (q, (k + q) * 4, words[k + q]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
