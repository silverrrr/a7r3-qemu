#!/usr/bin/env python3
"""Decode a 614400-byte window at a given physical address from RAM chunk files.

usage: decode-at.py --pa 0x20098000 mem_20000000.bin [more chunks ...]
"""
import argparse
import array
import os
import sys

from PIL import Image

W, H = 640, 480
SPAN = W * H * 2


def morton_index(x, y):
    yy = H - 1 - y
    z = ((x & 1) | ((yy & 1) << 1) | ((x & 2) << 1) | ((yy & 2) << 2)
         | ((x & 4) << 2) | ((yy & 4) << 3))
    return (((yy >> 3) * (W // 8) + (x >> 3)) * 64 + z)


def unpack4444(v):
    return (((v >> 12) & 0xF) * 17, ((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17)


def unpack565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63,
            (v & 0x1F) * 255 // 31)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pa', type=lambda s: int(s, 0), required=True)
    ap.add_argument('--out', default='/mnt/f/project/a7r3/analysis/real-probe')
    ap.add_argument('chunks', nargs='+')
    args = ap.parse_args()

    blobs = []
    for path in args.chunks:
        try:
            pa = int(os.path.basename(path).split('_')[-1].split('.')[0], 16)
        except ValueError:
            continue
        a = array.array('H')
        a.frombytes(open(path, 'rb').read())
        blobs.append((pa, a))

    src = next((a for p, a in blobs if p <= args.pa < p + len(a) * 2), None)
    base = next((p for p, a in blobs if p <= args.pa < p + len(a) * 2), None)
    if src is None:
        print('address 0x%08x not covered by the chunks' % args.pa)
        return 1
    off = (args.pa - base) // 2
    words = src[off:off + W * H]
    if len(words) < W * H:
        print('window runs past the chunk end (%d words)' % len(words))
        words = src[off:]
    nz = sum(1 for v in words if v)
    print('PA=0x%08x: %d words, nonzero=%d (%.1f%%)'
          % (args.pa, len(words), nz, 100.0 * nz / max(1, len(words))))

    os.makedirs(args.out, exist_ok=True)
    for tag, morton, unpack in (('lin4444', False, unpack4444),
                                ('lin565', False, unpack565),
                                ('mort4444', True, unpack4444),
                                ('mort565', True, unpack565)):
        px = bytearray(W * H * 3)
        for y in range(H):
            for x in range(W):
                o = morton_index(x, y) if morton else (y * W + x)
                v = words[o] if o < len(words) else 0
                r, g, b = unpack(v)
                i = 3 * (y * W + x)
                px[i], px[i + 1], px[i + 2] = r, g, b
        dst = os.path.join(args.out, 'at_%08x_%s.png' % (args.pa, tag))
        Image.frombytes('RGB', (W, H), bytes(px)).save(dst)
        print('wrote', dst)
    return 0


if __name__ == '__main__':
    sys.exit(main())
