#!/usr/bin/env python3
r"""Post-link hardening for the A7R3 payload (ld-2.11.2 compatibility).

Two things the camera's glibc 2.11.2 loader does not survive, documented by the
a7r3-boot-runtime project and reproduced here:

  * DT_BIND_NOW (which we build with) plus relocation tables that are not
    contiguous lets ld-2.11.2 mis-read DT_RELCOUNT and overwrite GOT slots as
    R_ARM_RELATIVE, so the first PLT call jumps to 0.  Their invariant: every
    custom DSO ships DT_RELCOUNT = 0.
  * RWX PT_LOAD segments / executable PT_GNU_STACK are never acceptable.

Usage (from build-newmenu.sh):
    patch-dt-relcount.py <elf> [--require-contiguous]

Exit code 0 = patched and audited; 1 = an invariant is violated.
"""
from __future__ import annotations

import struct
import sys

DT_REL = 17
DT_RELSZ = 18
DT_JMPREL = 23
DT_RELCOUNT = 0x6FFFFFFA
DT_FLAGS = 0x1E
DT_FLAGS_1 = 0x6FFFFFFB
DT_BIND_NOW_F = 0x8
DT_NOW_1 = 0x1
PT_LOAD = 1
PT_GNU_STACK = 0x6474E551
PF_X = 0x1
PF_W = 0x2


def parse(raw: bytes):
    if raw[:4] != b"\x7fELF" or raw[4] != 1 or raw[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    (e_phoff, e_shoff, _flags, _ehsize, e_phentsize, e_phnum,
     e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<IIIHHHHHH", raw, 28)
    phdrs = []
    for i in range(e_phnum):
        p = struct.unpack_from("<8I", raw, e_phoff + i * 32)
        phdrs.append({"i": i, "type": p[0], "flags": p[6]})
    shdrs = []
    for i in range(e_shnum):
        sh = struct.unpack_from("<10I", raw, e_shoff + i * 40)
        shdrs.append({"name": sh[0], "type": sh[1], "flags": sh[2],
                      "addr": sh[3], "offset": sh[4], "size": sh[5]})
    shstr = shdrs[e_shstrndx]
    names = raw[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for s in shdrs:
        off = s["name"]
        end = names.find(b"\x00", off)
        s["sname"] = names[off:end].decode("ascii", "replace") if end >= 0 else ""
    dyn = next(s for s in shdrs if s["sname"] == ".dynamic")
    tags = []
    for i in range(0, dyn["size"], 8):
        tag, val = struct.unpack_from("<II", raw, dyn["offset"] + i)
        tags.append((dyn["offset"] + i, tag, val))
        if tag == 0:
            break
    return phdrs, shdrs, tags


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    require_contig = "--require-contiguous" in sys.argv
    if not args:
        print("usage: patch-dt-relcount.py <elf> [--require-contiguous]")
        return 2
    path = args[0]
    raw = bytearray(open(path, "rb").read())
    phdrs, shdrs, tags = parse(raw)

    values = {}
    for _off, tag, val in tags:
        values.setdefault(tag, val)

    bad = 0
    for p in phdrs:
        if p["type"] == PT_LOAD and (p["flags"] & (PF_W | PF_X)) == (PF_W | PF_X):
            print("FAIL: RWX PT_LOAD segment #%d" % p["i"])
            bad += 1
        if p["type"] == PT_GNU_STACK and (p["flags"] & PF_X):
            print("FAIL: executable PT_GNU_STACK")
            bad += 1

    rel, relsz = values.get(DT_REL), values.get(DT_RELSZ)
    jmp, count = values.get(DT_JMPREL), values.get(DT_RELCOUNT)
    bind_now = bool(values.get(DT_FLAGS, 0) & DT_BIND_NOW_F) or \
        bool(values.get(DT_FLAGS_1, 0) & DT_NOW_1)
    contiguous = None
    if None not in (rel, relsz, jmp):
        contiguous = (rel + relsz) == jmp

    print("before: RELCOUNT=%s REL=0x%x RELSZ=%d JMPREL=0x%x BIND_NOW=%s"
          % (count, rel or 0, relsz or 0, jmp or 0, bind_now))
    print("        relocations contiguous: %s"
          % ("yes" if contiguous else "NO (DT_REL+DT_RELSZ != DT_JMPREL)"))

    patched = 0
    for off, tag, _val in tags:
        if tag == DT_RELCOUNT:
            struct.pack_into("<I", raw, off + 4, 0)
            patched += 1
    if patched:
        open(path, "wb").write(bytes(raw))
    print("DT_RELCOUNT tags zeroed: %d" % patched)

    _ph, _sh, tags2 = parse(bytes(raw))
    after = {}
    for _off, tag, val in tags2:
        after.setdefault(tag, val)
    print("after : RELCOUNT=%s" % after.get(DT_RELCOUNT))

    if require_contig and contiguous is False:
        print("FAIL: --require-contiguous set but tables are not contiguous")
        bad += 1
    if after.get(DT_RELCOUNT) not in (0, None):
        print("FAIL: DT_RELCOUNT still non-zero")
        bad += 1
    print("result: %s" % ("OK" if bad == 0 else "FAILED (%d)" % bad))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
