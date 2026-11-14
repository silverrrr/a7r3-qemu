#!/usr/bin/env python3
"""Test the hypothesis that a menu title resid is an offset into a UXC string blob.

usage: uxc-offset-test.py <string.uxc> 0x204e146 0x204e30d ...
"""
import sys


def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    for arg in sys.argv[2:]:
        v = int(arg, 0)
        for cand, note in ((v & 0xffffff, 'low24'),
                           (v & 0xfffff, 'low20'),
                           (v - 0x2000000, 'minus 0x02000000')):
            if 0 <= cand < len(data):
                chunk = data[cand:cand + 64]
                try:
                    txt = chunk.split(b'\x00')[0].decode('utf-8')
                except UnicodeDecodeError:
                    txt = repr(chunk[:24])
                print('%08x %-16s off=0x%x: %s' % (v, note, cand, txt[:40]))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
