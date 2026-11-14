#!/usr/bin/env python3
"""Diagnostic: what does the RAM dump contain, and where is the most image-like data?

For each chunk, slides over 614400-byte windows and reports the top windows by
distinct 16-bit values / non-zero ratio (no alpha requirement), plus a coarse
map of "entropy" per 64 KiB block so we can see where the data lives.
"""
import array
import os
import sys


W, H = 640, 480
SPAN = W * H * 2
STEP = 0x4000
SAMPLE = 256          # sample every 256th word


def stats(a, off):
    sl = a[off // 2: off // 2 + W * H: SAMPLE]
    if not sl:
        return (0, 0.0)
    nz = 0
    seen = set()
    for v in sl:
        if v:
            nz += 1
        seen.add(v)
    return (len(seen), nz / len(sl))


def main():
    files = sys.argv[1:]
    for path in files:
        a = array.array('H')
        data = open(path, 'rb').read()
        a.frombytes(data[:len(data) // 2 * 2])
        # coarse map: non-zero fraction per 64 KiB block
        blocks = []
        for b in range(0, len(a) * 2, 0x10000):
            blk = a[b // 2: (b + 0x10000) // 2: 64]
            nz = sum(1 for v in blk if v)
            blocks.append((b, nz / max(1, len(blk))))
        busy = [b for b, f in blocks if f > 0.2]
        top = []
        max_off = len(a) * 2 - SPAN
        for off in range(0, max(1, max_off), STEP):
            d, nz = stats(a, off)
            if d:
                top.append((d * (0.05 + nz), off, d, nz))
        top.sort(reverse=True)
        print('%s: %d words, busy 64K blocks=%d, top windows:'
              % (os.path.basename(path), len(a), len(busy)))
        for sc, off, d, nz in top[:5]:
            print('   off=0x%06x distinct=%d nz=%.2f' % (off, d, nz))
        if busy:
            print('   busy blocks: %s%s'
                  % (['0x%x' % b for b in busy[:12]],
                     ' ...' if len(busy) > 12 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
