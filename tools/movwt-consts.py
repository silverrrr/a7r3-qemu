#!/usr/bin/env python3
"""Reconstruct the 32-bit constants a Thumb-2 module builds with movw/movt.

MMIO bases are almost never literals: the compiler emits
    movw rX, #lo16
    movt rX, #hi16
so a plain byte search misses them.  This parses the disassembly, pairs the
immediates written to the same register (movt after movw) and prints every
reconstructed constant in the register/MMIO range, with the address of the
first instruction so it can be looked up.

usage: movwt-consts.py <module.ko> [--range f0000000-ffffffff]
"""
import argparse
import re
import subprocess
import sys

OD = '/usr/lib/llvm-21/bin/llvm-objdump'
MOVW = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f ]+?)\s+movw\s+(r\d+|lr|ip|sp),\s*#(0x[0-9a-f]+)')
MOVT = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f ]+?)\s+movt\s+(r\d+|lr|ip|sp),\s*#(0x[0-9a-f]+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--range', default='f0000000-ffffffff')
    args = ap.parse_args()
    lo_s, hi_s = args.range.split('-')
    lo, hi = int(lo_s, 16), int(hi_s, 16)

    dis = subprocess.run([OD, '-d', '--triple=armv7-linux-gnueabi', args.module],
                         capture_output=True, text=True).stdout
    pending = {}
    consts = []
    for line in dis.splitlines():
        m = MOVW.match(line)
        if m:
            pending[m.group(3)] = (int(m.group(4), 16), int(m.group(1), 16))
            continue
        m = MOVT.match(line)
        if m:
            reg = m.group(3)
            if reg in pending:
                lohalf, addr = pending.pop(reg)
                val = (int(m.group(4), 16) << 16) | lohalf
                if lo <= val <= hi:
                    consts.append((addr, val))
            continue
    seen = {}
    for addr, val in consts:
        seen.setdefault(val, addr)
    print('%s: %d distinct constants in %s' % (args.module, len(seen), args.range))
    for val in sorted(seen):
        print('  %08x  first@0x%x' % (val, seen[val]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
