#!/usr/bin/env python3
"""Dump the A7R3 menu-relevant dynamic symbols from /usr/lib/*.so.

Reads the /usr partition straight out of the eMMC image (no mount needed) by
walking the ext2 tree with analysis/ext2_locate.py, then parses .dynsym with
pyelftools.  Answers the M0 question: which CmnViewSettingNode / GRM entry
points exist, whether they are exported (DEF) or imported (UND), and at what
address -- the ZV-E10 port pins its hooks to exactly these.
"""
import argparse
import io
import os
import sys

sys.path.insert(0, "/mnt/f/project/a7r3/analysis")
from ext2_locate import Ext2, DUMPS  # noqa: E402

from elftools.elf.elffile import ELFFile  # noqa: E402

LIBS = [
    "lib/libObj.so",
    "lib/viewUnified2.so",
    "lib/CautionConfig.so",
    "lib/viewUnified7.so",
    "lib/libIMDB.so",
]

NEEDLES = (
    "CmnViewSettingNode",
    "cmnViewSettingNode",
    "GRM_",
    "GEN_Label",
    "_Znaj",
    "Application_System_Init",
)


def walk(fs):
    return {p.lstrip("/"): ino for ino, p in fs.walk().items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--needle", action="append",
                    help="substring to match (repeatable); default = the menu set")
    ap.add_argument("--lib", action="append",
                    help="library path inside /usr (repeatable); default = all")
    ap.add_argument("--def-only", action="store_true")
    args = ap.parse_args()
    needles = tuple(args.needle) if args.needle else NEEDLES
    libs = args.lib or LIBS
    fs = Ext2(os.path.join(DUMPS, "nflasha15.bin"))
    paths = walk(fs)
    for lib in libs:
        ino = paths.get(lib) or paths.get(lib.lstrip("/"))
        if not ino:
            print("%-26s MISSING" % lib)
            continue
        data = fs.read_file(ino)
        print("\n=== %s (%d B) ===" % (lib, len(data)))
        try:
            elf = ELFFile(io.BytesIO(data))
            symtab = elf.get_section_by_name(".dynsym")
        except Exception as exc:
            print("  parse failed: %s" % exc)
            continue
        if symtab is None:
            print("  no .dynsym")
            continue
        total = symtab.num_symbols()
        hits = 0
        for sym in symtab.iter_symbols():
            name = sym.name
            if not name:
                continue
            if not any(n in name for n in needles):
                continue
            shndx = sym["st_shndx"]
            kind = "DEF" if shndx not in ("SHN_UNDEF", 0, None) else "UND"
            if args.def_only and kind != "DEF":
                continue
            hits += 1
            print("  %-3s %-8s %#010x sz=%-7d %s"
                  % (kind, sym["st_info"]["type"], sym["st_value"],
                     sym["st_size"], name))
        print("  (%d/%d dynsyms matched)" % (hits, total))


if __name__ == "__main__":
    main()
