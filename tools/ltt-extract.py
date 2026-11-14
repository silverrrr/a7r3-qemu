#!/usr/bin/env python3
"""Extract the embedded TrueType from a Sony .ltt container and test-render CJK.

usage: ltt-extract.py <file.ltt> <out.ttf> [text-to-render]
"""
import re
import sys

from PIL import Image, ImageDraw, ImageFont


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = sys.argv[3] if len(sys.argv) > 3 else '显示质量 自动关机开始时间'
    data = open(src, 'rb').read()
    head = data[:4096].decode('latin-1', 'replace')
    kv = dict(re.findall(r'([a-z/0-9_]+)\s*=\s*([^\n]+)\n', head))
    off = int(kv['embeddedoffset'])
    size = int(kv['embeddedsize'])
    blob = data[off:off + size]
    open(dst, 'wb').write(blob)
    print('wrote %s (%d bytes)' % (dst, len(blob)))

    for px in (16, 20, 24):
        font = ImageFont.truetype(dst, px)
        img = Image.new('RGB', (640, px * 3 + 16), (0, 0, 0))
        d = ImageDraw.Draw(img)
        d.text((4, 4), text, font=font, fill=(255, 255, 255))
        out = '/mnt/f/project/a7r3/analysis/fonts/render_%dpx.png' % px
        img.save(out)
        print('rendered %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
