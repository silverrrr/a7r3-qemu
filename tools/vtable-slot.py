#!/usr/bin/env python3
"""Read a vtable slot from CautionConfig.so and name the function it points to.

usage: vtable-slot.py <lib.so> <vtable_vaddr> <byte_offset>
"""
import struct
import subprocess
import sys

OD = '/usr/lib/llvm-21/bin/llvm-objdump'


def main():
    lib, vt, off = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    data = open(lib, 'rb').read()
    # ELF section -> file offset mapping via readelf-like parse
    import subprocess as sp
    out = sp.run(['/usr/bin/readelf', '-S', '-W', lib], capture_output=True,
                 text=True).stdout
    secs = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[0].startswith('['):
            try:
                name = parts[1]
                addr = int(parts[3], 16)
                offset = int(parts[4], 16)
                size = int(parts[5], 16)
                secs.append((name, addr, offset, size))
            except (ValueError, IndexError):
                continue
    def to_file(vaddr):
        for name, addr, offset, size in secs:
            if addr <= vaddr < addr + size:
                return offset + (vaddr - addr), name
        return None, None
    fo, sname = to_file(vt + off)
    if fo is None:
        print('vtable address not in any section')
        return 1
    val = struct.unpack_from('<I', data, fo)[0]
    print('slot %s+0x%x -> 0x%08x (file 0x%x, section %s)'
          % (hex(vt), off, val, fo, sname))
    # print a few neighbour slots
    for k in range(-2, 3):
        f2 = fo + k * 4
        if 0 <= f2 < len(data) - 4:
            v = struct.unpack_from('<I', data, f2)[0]
            print('   slot%+d: 0x%08x' % (k, v))
    return 0


if __name__ == '__main__':
    sys.exit(main())
