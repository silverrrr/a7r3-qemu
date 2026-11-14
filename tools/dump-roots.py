#!/usr/bin/env python3
"""Decode the A7R3 menu-tree prototypes that live inside CautionConfig.so.

The ZV-E10 port proved the node layout:

    +0x00 vptr            +0x10 parent        +0x20 physical
    +0x04 children**      +0x14 depth         +0x24 state
    +0x08 count           +0x18 selected
    +0x0C properties*     +0x1C selectable      sizeof == 0x28

A7R3 exports hundreds of 40-byte `cmnViewSettingNodeRoot*` objects, i.e. one
prototype per menu page.  This script decodes each one, resolves its vptr to
the `_ZTV*` symbol (-> page class name) and its properties pointer to the
`cmnViewSettingPropert*` table, then prints the property records.  No runtime,
no device -- it is pure static data from our own /usr dump.
"""
import argparse
import io
import os
import struct
import sys

sys.path.insert(0, "/mnt/f/project/a7r3/analysis")
from ext2_locate import Ext2, DUMPS  # noqa: E402

from elftools.elf.elffile import ELFFile  # noqa: E402

NODE_FIELDS = ["vptr", "children", "count", "properties", "parent",
               "depth", "selected", "selectable", "physical", "state"]


def load(lib):
    fs = Ext2(os.path.join(DUMPS, "nflasha15.bin"))
    paths = {p.lstrip("/"): ino for ino, p in fs.walk().items()}
    data = fs.read_file(paths[lib])
    return data, ELFFile(io.BytesIO(data))


def vaddr_to_offset(elf, addr):
    for sec in elf.iter_sections():
        start = sec["sh_addr"]
        if start and start <= addr < start + sec["sh_size"]:
            return sec["sh_offset"] + (addr - start)
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        start = seg["p_vaddr"]
        if start <= addr < start + seg["p_memsz"]:
            return seg["p_offset"] + (addr - start)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", default="lib/CautionConfig.so")
    ap.add_argument("--filter", default="")
    ap.add_argument("--limit", type=int, default=8)
    ap.add_argument("--props", default="SetupMenuStart")
    args = ap.parse_args()

    data, elf = load(args.lib)
    symtab = elf.get_section_by_name(".dynsym")
    syms = {}
    roots = []
    vtables = {}
    for sym in symtab.iter_symbols():
        name = sym.name
        if not name:
            continue
        if sym["st_shndx"] in ("SHN_UNDEF", 0, None):
            continue
        syms.setdefault(sym["st_value"], name)
        if name.startswith("_ZTV"):
            vtables[sym["st_value"]] = (name, sym["st_size"])
        if name.startswith("cmnViewSettingNodeRoot") and args.filter in name:
            roots.append((sym["st_value"], sym["st_size"], name))
    roots.sort(key=lambda r: r[2])
    print("roots matched: %d (dynsyms %d, vtables %d)"
          % (len(roots), symtab.num_symbols(), len(vtables)))

    for value, size, name in roots[:args.limit]:
        off = vaddr_to_offset(elf, value)
        words = struct.unpack_from("<10I", data, off)
        vptr = words[0]
        vname = "?"
        for delta in (0, 8, -8):
            hit = vtables.get((vptr + delta) & 0xffffffff)
            if hit:
                vname = hit[0]
                break
        print("\n%s  (size %d)" % (name, size))
        for field, w in zip(NODE_FIELDS, words):
            extra = ""
            if field == "vptr":
                extra = "  -> " + vname
            elif w and (w & 0xff000000) in (0x00900000, 0x00a00000, 0x00800000) \
                    and w in syms:
                extra = "  -> " + syms[w]
            print("  +0x%02x %-11s %#010x%s" % (NODE_FIELDS.index(field) * 4,
                                                field, w, extra))

    # decode the properties table of the requested page
    target = [r for r in roots if args.props in r[2]]
    if not target:
        target = roots
    if target:
        value, size, name = target[0]
        off = vaddr_to_offset(elf, value)
        prop_ptr = struct.unpack_from("<I", data, off + 0x0C)[0]
        pname = syms.get(prop_ptr, "?")
        print("\n=== properties of %s ===" % name)
        print("  properties* = %#x  (%s)" % (prop_ptr, pname))
        poff = vaddr_to_offset(elf, prop_ptr)
        if poff is not None:
            recs, last = struct.unpack_from("<Ii", data, poff)
            print("  records = %#x  last = %d  (%s)"
                  % (recs, last, syms.get(recs, "?")))
            roff = vaddr_to_offset(elf, recs)
            if roff is not None:
                for i in range(min(last + 1, 24)):
                    key, kind, val = struct.unpack_from("<iiI", data, roff + 12 * i)
                    print("    [%d] key=%#010x kind=%d value=%#010x"
                          % (i, key & 0xffffffff, kind, val))


if __name__ == "__main__":
    main()
