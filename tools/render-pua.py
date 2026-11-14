#!/usr/bin/env python3
"""Render private-use icon codepoints from the camera font to see what they are.

The menu titles are prefixed with PUA characters (e.g. U+E08C before
"JPEG影像质量"); the native UI draws them as small icons.  This previews what
the extracted font does with them.

usage: render-pua.py [cp ...]
"""
import sys

from PIL import Image, ImageDraw, ImageFont

TTF = '/mnt/f/project/a7r3/analysis/fonts/myinghei_gb18030.ttf'
CPS = [int(x, 0) for x in sys.argv[1:]] or [0xE08C, 0xE08D, 0xE08E, 0xE800, 0xE801, 0xE802]


def main():
    size = 32
    font = ImageFont.truetype(TTF, size)
    cell = 48
    img = Image.new('RGB', (cell * len(CPS), cell + 8), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, cp in enumerate(CPS):
        ch = chr(cp)
        d.text((i * cell + 4, cell), ch, font=font, fill=(255, 255, 255), anchor='ls')
        print('U+%04X: advance=%s bbox=%s'
              % (cp, font.getlength(ch), font.getbbox(ch)))
    out = '/mnt/f/project/a7r3/analysis/fonts/pua_preview.png'
    img.save(out)
    print('wrote', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
