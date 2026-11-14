#!/usr/bin/env python3
"""Decode every Sugilite command list found in a RAM dump.

For each (op<<16|0xf000) run it reports the ops that matter for rendering:
0x0112 destination surface, 0x0589 texture, 0x0049 drawing area, 0x047/0x048
rect edges, and how many 0x0202 / 0x00000281 draws it contains.  Runs can be
written out as raw files for the full decoder (decode-cmdbuf.py).

usage: decode-sugi-runs.py [--save DIR] mem_*.bin
"""
import array
import os
import sys

KNOWN_MASK = 0xffff
TAG = 0xf000
INTEREST = {0x0112: 'surf', 0x0589: 'tex', 0x0049: 'area', 0x0047: 'r47',
            0x0048: 'r48', 0x0586: 'texsz', 0x0587: 'texsz', 0x0105: 'color'}


def main():
    args = sys.argv[1:]
    save = None
    if args and args[0] == '--save':
        save = args[1]
        args = args[2:]
        os.makedirs(save, exist_ok=True)

    for path in args:
        name = os.path.basename(path)
        try:
            base = int(name.split('_')[-1].split('.')[0], 16)
        except ValueError:
            base = 0x20000000
        data = open(path, 'rb').read()
        words = array.array('I')
        words.frombytes(data[:len(data) // 4 * 4])
        n = len(words)
        i = 0
        while i < n - 1:
            w = words[i]
            if (w & KNOWN_MASK) == TAG:
                j = i
                while j < n - 1 and (words[j] & KNOWN_MASK) == TAG:
                    j += 2
                cnt = (j - i) // 2
                if cnt >= 3:
                    info = {}
                    draws = 0
                    for k in range(cnt):
                        op = words[i + 2 * k] >> 16
                        val = words[i + 2 * k + 1]
                        if op in INTEREST:
                            info.setdefault(INTEREST[op], []).append(val)
                        if op == 0x0202:
                            draws += 1
                        if words[i + 2 * k] == 0x00000281:
                            draws += 1
                    pa = base + i * 4
                    desc = []
                    for k in ('surf', 'area', 'tex', 'texsz', 'color'):
                        if k in info:
                            vals = info[k]
                            if k == 'surf':
                                desc.append('surf=%s' % ','.join(
                                    '0x%08x' % ((v << 4) & 0xffffffff) for v in vals[:4]))
                            elif k == 'tex':
                                desc.append('tex=%s' % ','.join(
                                    '0x%08x' % ((v << 3) & 0xffffffff) for v in vals[:4]))
                            else:
                                desc.append('%s=%s' % (k, ','.join(
                                    '0x%08x' % v for v in vals[:3])))
                    print('%s pa=0x%08x ops=%d draws=%d %s'
                          % (name, pa, cnt, draws, ' '.join(desc)))
                    if save:
                        out = os.path.join(save, 'list_%08x.bin' % pa)
                        with open(out, 'wb') as f:
                            f.write(data[i * 4:(i + 2 * cnt) * 4])
                    i = j
                    continue
            i += 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
