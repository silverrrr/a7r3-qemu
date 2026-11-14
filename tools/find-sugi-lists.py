#!/usr/bin/env python3
"""Find Sugilite 2D command lists inside raw RAM dumps.

The lists are sequences of (op << 16 | 0xf000, value) pairs; op 0x0112 carries
the destination surface (PA = value << 4), 0x0589 the texture, 0x0049 the area.
A run of several such words is almost certainly a live command list, and its
0x0112 value names the surface the app is drawing into.

usage: find-sugi-lists.py [--base 0x20000000] mem_*.bin
"""
import array
import os
import struct
import sys

KNOWN = {0x0112, 0x0589, 0x0049, 0x0047, 0x0048, 0x0105, 0x0202, 0x0216,
         0x0586, 0x0587, 0x0116, 0x0117, 0x0044, 0x0010, 0x0450, 0x0451,
         0x0452, 0x0453, 0x0454}


def main():
    args = sys.argv[1:]
    base_default = 0x20000000
    if args and args[0].startswith('--base='):
        base_default = int(args[0].split('=', 1)[1], 0)
        args = args[1:]

    for path in args:
        name = os.path.basename(path)
        try:
            pa = int(name.split('_')[-1].split('.')[0], 16)
        except ValueError:
            pa = base_default
        a = array.array('H')
        a.frombytes(open(path, 'rb').read())
        words = array.array('I')
        words.frombytes(open(path, 'rb').read()[:len(a) * 2])
        # walk 32-bit words for (op<<16|0xf000) followed by a plausible value
        n = len(words)
        runs = []
        i = 0
        while i < n - 1:
            w = words[i]
            if (w & 0xffff) == 0xf000 and (w >> 16) in KNOWN:
                j = i
                cnt = 0
                while j < n - 1 and (words[j] & 0xffff) == 0xf000:
                    cnt += 1
                    j += 2
                if cnt >= 3:
                    runs.append((i, cnt))
                    i = j
                    continue
            i += 1
        if not runs:
            continue
        print('%s @0x%08x: %d run(s)' % (name, pa, len(runs)))
        for off, cnt in runs[:8]:
            head = ' '.join('%08x' % words[off + 2 * k] for k in range(min(cnt, 6)))
            vals = ' '.join('%08x' % words[off + 2 * k + 1] for k in range(min(cnt, 6)))
            surf = ''
            for k in range(cnt):
                if (words[off + 2 * k] >> 16) == 0x0112:
                    surf = '  -> surf=0x%08x' % ((words[off + 2 * k + 1] << 4)
                                                 & 0xffffffff)
                    break
            print('   off=0x%06x (%d ops) pa=0x%08x%s' %
                  (off * 4, cnt, pa + off * 4, surf))
            print('      ops : %s' % head)
            print('      vals: %s' % vals)
    return 0


if __name__ == '__main__':
    sys.exit(main())
