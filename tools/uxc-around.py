#!/usr/bin/env python3
"""Look for the id/offset records that precede strings in a UXC container.

Dumps the bytes before several known labels and tries to read a u32 id and a
length right before the text, so the container layout can be pinned down.

usage: uxc-around.py <file.uxc> 显示质量 触摸操作 ...
"""
import struct
import sys


def main():
    path = sys.argv[1]
    labels = sys.argv[2:]
    data = open(path, 'rb').read()
    for s in labels:
        pat = s.encode('utf-8')
        i = data.find(pat)
        if i < 0:
            print('%s: not found' % s)
            continue
        print('\n%s at 0x%x' % (s, i))
        print('  before: %s' % data[max(0, i - 48):i].hex(' '))
        print('  text  : %s' % data[i:i + 40].hex(' '))
        for back in (4, 8, 12):
            if i >= back:
                w = struct.unpack_from('<I', data, i - back)[0]
                print('    -%d: %08x (%d)' % (back, w, w))
    return 0


if __name__ == '__main__':
    sys.exit(main())
