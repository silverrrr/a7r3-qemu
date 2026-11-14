#!/usr/bin/env python3
"""Work out the address of libObj's "last touch position" struct.

callback_tp ends with

    ldr r3, [pc, #0x68]      ; lit2 at 0x3c3338
    ldr r3, [r5, r3]         ; r5 = GOT (computed at function entry)
    strd r10, r11, [r3]      ; store x, y

so the struct address is *(GOT + lit2), where GOT = lit1 + (pc at entry).  This
script reads those literals and prints the absolute slot so the payload can poll
x/y without patching any code.

usage: touch-struct-slot.py <libObj.so>
"""
import struct
import subprocess
import sys

VT = {'got_lit': 0x3c3330, 'slot_lit': 0x3c3338, 'pc_of_lit': 0x3c31d8}


def sections(lib):
    out = subprocess.run(['readelf', '-S', '-W', lib], capture_output=True,
                         text=True).stdout
    secs = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 7 and p[0].startswith('['):
            try:
                secs.append((p[1], int(p[3], 16), int(p[4], 16), int(p[5], 16)))
            except (ValueError, IndexError):
                continue
    return secs


def to_file(secs, vaddr):
    for name, addr, off, size in secs:
        if addr <= vaddr < addr + size:
            return off + (vaddr - addr), name
    return None, None


def main():
    lib = sys.argv[1]
    data = open(lib, 'rb').read()
    secs = sections(lib)

    def word(vaddr):
        fo, name = to_file(secs, vaddr)
        if fo is None:
            raise SystemExit('0x%x not mapped' % vaddr)
        return struct.unpack_from('<I', data, fo)[0]

    lit1 = word(VT['got_lit'])
    lit2 = word(VT['slot_lit'])
    got = (lit1 + VT['pc_of_lit']) & 0xffffffff
    slot = (got + lit2) & 0xffffffff
    print('lit1(GOT delta)  = 0x%08x' % lit1)
    print('lit2(slot delta) = 0x%08x' % lit2)
    print('GOT              = 0x%08x' % got)
    print('slot address     = 0x%08x' % slot)

    fo, name = to_file(secs, slot)
    print('slot section     = %s (file 0x%x)' % (name, fo if fo else -1))
    if fo is not None:
        print('slot word (file) = 0x%08x  <- relocation target'
              % struct.unpack_from('<I', data, fo)[0])

    # which variable does the relocation point at?
    rel = subprocess.run(['readelf', '-r', '-W', lib], capture_output=True,
                         text=True).stdout
    for line in rel.splitlines():
        p = line.split()
        if p and p[0].lstrip('0') and p[0][-8:].lower() == ('%08x' % slot):
            print('reloc: %s' % line.strip())
            break
    else:
        # try matching the printed offset column
        for line in rel.splitlines():
            if ('%08x' % slot) in line:
                print('reloc: %s' % line.strip())
                break
    return 0


if __name__ == '__main__':
    sys.exit(main())
