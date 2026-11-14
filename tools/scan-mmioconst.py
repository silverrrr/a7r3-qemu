#!/usr/bin/env python3
"""List MMIO-looking constants (0xf0000000-0xffffffff, page aligned) in a blob.

Kernel board files keep device bases as plain 32-bit words; scanning a kernel
image for page-aligned 0xf..... values finds them without disassembly.

usage: scan-mmioconst.py FILE [more files]
"""
import os
import struct
import sys

SKIP = {0xffffffff, 0xfffff000, 0xffffffff}


def main():
    for path in sys.argv[1:]:
        data = open(path, 'rb').read()
        seen = {}
        for i in range(0, len(data) - 4, 4):
            v = struct.unpack_from('<I', data, i)[0]
            if 0xf0000000 <= v < 0xfff00000 and v % 0x1000 == 0 and v not in SKIP:
                seen.setdefault(v, i)
        print('%s (%d bytes): %d page-aligned IO-looking constants'
              % (os.path.basename(path), len(data), len(seen)))
        for v in sorted(seen):
            print('   0x%08x  first@0x%x' % (v, seen[v]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
