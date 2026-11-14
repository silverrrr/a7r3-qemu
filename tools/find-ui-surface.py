#!/usr/bin/env python3
"""Find the real device's display surface inside a raw RAM dump.

Scans raw RAM chunks for a 640x480 16bpp window that looks like a frame:
many distinct 16-bit values, high non-zero ratio, and (for RGBA4444 UI) a high
fraction of pixels with alpha nibble 0xF.  For the best candidates it writes
PNG previews in every plausible layout so you can match them against the
camera screen.

usage: find-ui-surface.py --base 0x20000000 mem_20000000.bin [more.bin ...]
                          [--top 12] [--out DIR]
"""
import argparse
import array
import os
import struct
import sys

W, H = 640, 480
SPAN = W * H * 2            # 614400 bytes
STEP = 8192                 # scoring stride in bytes
SAMPLE_STRIDE = 128         # sample every 128th 16-bit word for scoring


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


def decode(arr_words, out_prefix, morton, unpack):
    from PIL import Image
    px = bytearray(W * H * 3)
    base = 0
    for y in range(H):
        for x in range(W):
            o = morton_index(x, y) if morton else (y * W + x)
            v = arr_words[o]
            r, g, b = unpack(v)
            i = 3 * (y * W + x)
            px[i], px[i + 1], px[i + 2] = r, g, b
    dst = out_prefix + '.png'
    Image.frombytes('RGB', (W, H), bytes(px)).save(dst)
    return dst


def score_window(a, off):
    """Return (distinct, nonzero_frac, alpha_f) sampled over the window."""
    sl = a[off // 2: off // 2 + W * H: SAMPLE_STRIDE]
    if not sl:
        return (0, 0.0, 0.0)
    nz = 0
    alpha = 0
    seen = set()
    for v in sl:
        if v:
            nz += 1
        if (v & 0xF) == 0xF:
            alpha += 1
        seen.add(v)
    n = len(sl)
    return (len(seen), nz / n, alpha / n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--base', type=lambda s: int(s, 0), default=0x20000000)
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--out', default='/mnt/f/project/a7r3/analysis/real-probe')
    args = ap.parse_args()

    # load all chunks into one bytearray with their physical base
    blobs = []
    for path in args.files:
        name = os.path.basename(path)
        try:
            pa = int(name.split('_')[-1].split('.')[0], 16)
        except ValueError:
            pa = args.base
        data = open(path, 'rb').read()
        a = array.array('H')
        a.frombytes(data[:len(data) // 2 * 2])
        blobs.append((pa, a))
        print('loaded %-22s 0x%08x %d words' % (name, pa, len(a)))

    cands = []
    for pa, a in blobs:
        max_off = len(a) * 2 - SPAN
        if max_off <= 0:
            continue
        for off in range(0, max_off, STEP):
            distinct, nz, alpha = score_window(a, off)
            if distinct < 400 or alpha < 0.5:
                continue
            cands.append((distinct * (0.5 + alpha) * (0.2 + nz),
                          pa + off, pa, off, distinct, nz, alpha))
    cands.sort(reverse=True)
    print('\ncandidates: %d' % len(cands))
    seen_pas = []
    for sc, abs_pa, pa, off, distinct, nz, alpha in cands:
        if any(abs(abs_pa - p) < 0x40000 for p in seen_pas):
            continue
        seen_pas.append(abs_pa)
        print('  PA=0x%08x score=%.0f distinct=%d nz=%.2f alpha=%.2f'
              % (abs_pa, sc, distinct, nz, alpha))
        if len(seen_pas) >= args.top:
            break

    os.makedirs(args.out, exist_ok=True)
    for abs_pa in seen_pas:
        pa = abs_pa & ~0x1FFF
        blob = next((a for p, a in blobs if p <= pa < p + len(a) * 2), None)
        if blob is None:
            continue
        off = (pa - next(p for p, a in blobs if p <= pa)) // 2
        words = blob[off: off + W * H]
        if len(words) < W * H:
            continue
        for tag, morton, unpack in (('lin4444', False, unpack4444),
                                    ('lin565', False, unpack565),
                                    ('mort4444', True, unpack4444),
                                    ('mort565', True, unpack565)):
            dst = os.path.join(args.out, 'cand_%08x_%s' % (pa, tag))
            try:
                print('  wrote', decode(words, dst, morton, unpack))
            except Exception as exc:
                print('  decode failed %s: %r' % (dst, exc))
    return 0


if __name__ == '__main__':
    sys.exit(main())
