#!/usr/bin/env python3
"""Resolve a vtable slot in a stripped .so through its dynamic relocations.

.data.rel.ro holds zeros in the file; the loader fills them from .rel.dyn, so the
pointer value must be read from the relocation entry instead.

usage: vtable-slot2.py <lib.so> <vtable_vaddr> <byte_offset>
"""
import re
import subprocess
import sys


def main():
    lib, vt, off = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    target = vt + off
    # dynamic symbol table: address -> name
    syms = {}
    nm = subprocess.run(['nm', '-D', '--defined-only', lib],
                        capture_output=True, text=True).stdout
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                a = int(parts[0], 16)
            except ValueError:
                continue
            syms[a] = parts[2]
    # relocations
    rel = subprocess.run(['readelf', '-r', '-W', lib],
                         capture_output=True, text=True).stdout
    hits = []
    for line in rel.splitlines():
        m = re.match(r'([0-9a-f]+)\s+\S+\s+\S+\s+([0-9a-f]+)?\s*(\S+)?', line)
        if not m:
            continue
        try:
            roff = int(m.group(1), 16)
        except ValueError:
            continue
        if roff == target:
            hits.append(line.strip())
    print('vtable slot 0x%08x' % target)
    if not hits:
        print('  no relocation at that offset')
        return 1
    for h in hits:
        print('  %s' % h)
        parts = h.split()
        # R_ARM_ABS32 target usually printed as symbol name + addend
        for p in parts[3:]:
            if p in syms.values():
                print('    -> %s' % p)
    return 0


if __name__ == '__main__':
    sys.exit(main())
