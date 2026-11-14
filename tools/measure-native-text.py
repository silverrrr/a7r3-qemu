#!/usr/bin/env python3
"""Measure text metrics from a native A7R3 frame.

Prints bright-pixel bands (low threshold, so dim grey rows count too) and the
bounding box of a given crop, which is how the native font size is derived.

usage: measure-native-text.py [frame.png] [--crop x0,y0,x1,y1]
"""
import sys

from PIL import Image

FRAME = sys.argv[1] if len(sys.argv) > 1 else '/mnt/f/project/a7r3/analysis/cd2_9005.png'


def bands(img, thr):
    w, h = img.size
    px = img.load()
    counts = [sum(1 for x in range(w) if px[x, y] > thr) for y in range(h)]
    out = []
    y = 0
    while y < h:
        if counts[y] > 12:
            y0 = y
            while y < h and counts[y] > 12:
                y += 1
            out.append((y0, y - 1, max(counts[y0:y])))
        else:
            y += 1
    return out


def main():
    img = Image.open(FRAME).convert('L')
    w, h = img.size
    print('frame %s %dx%d' % (FRAME, w, h))
    for thr in (160, 110, 70):
        print('threshold %d:' % thr)
        for b in bands(img, thr)[:16]:
            print('   y %3d..%3d  peak=%d  height=%d'
                  % (b[0], b[1], b[2], b[1] - b[0] + 1))

    for crop in ((30, 130, 140, 175), (300, 70, 420, 112), (30, 185, 260, 220)):
        c = img.crop(crop)
        bbox = c.point(lambda v: 255 if v > 90 else 0).getbbox()
        if bbox:
            print('crop %s -> text bbox %s (h=%d)' % (crop, bbox, bbox[3] - bbox[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
