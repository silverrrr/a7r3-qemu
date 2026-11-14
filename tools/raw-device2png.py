#!/usr/bin/env python3
"""Decode a real-device 640x480 16bpp surface dump into candidate PNGs.

The QEMU model showed the panel buffer as RGBA4444 in an 8x8 Morton tile layout
with the row order flipped.  The real device may differ, so this writes every
plausible interpretation and lets you pick the one that matches the screen:

  <out>_lin4444.png   linear rows, RGBA4444
  <out>_lin565.png    linear rows, RGB565
  <out>_mort4444.png  Morton 8x8 + flipped rows, RGBA4444
  <out>_mort565.png   Morton 8x8 + flipped rows, RGB565

usage: raw-device2png.py IN.raw OUTPREFIX
"""
import sys

from PIL import Image

W, H = 640, 480


def unpack4444(v):
    return (((v >> 12) & 0xF) * 17, ((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17)


def unpack565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def morton_index(x, y):
    yy = H - 1 - y
    z = ((x & 1) | ((yy & 1) << 1) | ((x & 2) << 1) | ((yy & 2) << 2)
         | ((x & 4) << 2) | ((yy & 4) << 3))
    return (((yy >> 3) * (W // 8) + (x >> 3)) * 64 + z)


def main():
    src, prefix = sys.argv[1], sys.argv[2]
    raw = open(src, "rb").read()
    need = W * H * 2
    if len(raw) < need:
        print("short read: %d < %d" % (len(raw), need))
        return 1
    for tag, morton, unpack in (("lin4444", False, unpack4444),
                                ("lin565", False, unpack565),
                                ("mort4444", True, unpack4444),
                                ("mort565", True, unpack565)):
        px = bytearray(W * H * 3)
        for y in range(H):
            for x in range(W):
                o = morton_index(x, y) if morton else (y * W + x)
                v = raw[2 * o] | (raw[2 * o + 1] << 8)
                r, g, b = unpack(v)
                i = 3 * (y * W + x)
                px[i], px[i + 1], px[i + 2] = r, g, b
        dst = "%s_%s.png" % (prefix, tag)
        Image.frombytes("RGB", (W, H), bytes(px)).save(dst)
        print("wrote %s" % dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
