#!/usr/bin/env python3
"""Convert an RGBA4444 surf dump (640x480) to PNG."""
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, "rb").read()
w, h = 640, 480
px = bytearray(w * h * 3)
for i in range(w * h):
    v = raw[2 * i] | (raw[2 * i + 1] << 8)
    r = (v >> 12) & 0xF
    g = (v >> 8) & 0xF
    b = (v >> 4) & 0xF
    px[3 * i] = r * 17
    px[3 * i + 1] = g * 17
    px[3 * i + 2] = b * 17
img = Image.frombytes("RGB", (w, h), bytes(px))
img.save(dst)
print("wrote", dst)
