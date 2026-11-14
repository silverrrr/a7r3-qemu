#!/usr/bin/env python3
"""Hexdump a symbol's code in an A7R3 /usr library (file image, no guest)."""
import io
import struct
import sys

sys.path.insert(0, "/mnt/f/project/a7r3/analysis")
from ext2_locate import Ext2, DUMPS  # noqa: E402

from elftools.elf.elffile import ELFFile  # noqa: E402


def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else "lib/CautionConfig.so"
    name = sys.argv[2] if len(sys.argv) > 2 else \
        "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 24

    fs = Ext2("/root/firmware/a7r3/service-dump/nflasha15.bin") \
        if len(sys.argv) > 4 else Ext2(DUMPS + "/nflasha15.bin")
    paths = {p.lstrip("/"): ino for ino, p in fs.walk().items()}
    data = fs.read_file(paths[lib])
    elf = ELFFile(io.BytesIO(data))
    syms = elf.get_section_by_name(".dynsym")
    addr = None
    for s in syms.iter_symbols():
        if s.name == name:
            addr = s["st_value"]
            break
    if addr is None:
        raise SystemExit("symbol not found: %s" % name)
    va = addr & ~1
    off = None
    for sec in elf.iter_sections():
        if sec["sh_addr"] and sec["sh_addr"] <= va < sec["sh_addr"] + sec["sh_size"]:
            off = sec["sh_offset"] + (va - sec["sh_addr"])
            print("section %s: vaddr %#x -> file %#x" % (sec.name, va, off))
            break
    raw = data[off:off + count]
    print("%s vaddr=%#x thumb=%d" % (name, va, addr & 1))
    print(" ".join("%02x" % b for b in raw))
    half = [struct.unpack_from("<H", raw, i)[0] for i in range(0, len(raw), 2)]
    print("halfwords:", " ".join("%04x" % h for h in half))


if __name__ == "__main__":
    main()
