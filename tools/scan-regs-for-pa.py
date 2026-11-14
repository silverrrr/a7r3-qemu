#!/usr/bin/env python3
"""Scan MMIO register dumps for display surface addresses.

Reads the dumps produced by analysis/dump_regs_probe.py (sugilite.bin,
liro.bin), decodes the known Sugilite blit registers, then lists every register
word that looks like a physical address of a surface:

  0x20000000-0x40000000  or  0x80000000-0x90000000, 2-byte aligned,
  and (for the "likely surface" list) 4 KiB aligned.

usage: scan-regs-for-pa.py sugilite.bin liro.bin
"""
import os
import struct
import sys

SUGI_REGS = {
    0x0C00: ('blit-src', 2),
    0x0C04: ('blit-dst', 2),
    0x0C08: ('blit-w/h', 0),
    0x0C0C: ('blit-stride', 0),
    0x0C10: ('blit-fmt', 0),
    0x0C18: ('blit-kick', 0),
    0x1800: ('cmd-kick/len', 0),
    0x1804: ('cmd-addr', 0),
}


def looks_like_pa(v):
    return (0x20000000 <= v < 0x40000000) or (0x80000000 <= v < 0x90000000)


def main():
    paths = sys.argv[1:]
    for path in paths:
        data = open(path, 'rb').read()
        print('== %s (%d bytes)' % (os.path.basename(path), len(data)))
        words = struct.unpack('<%dI' % (len(data) // 4), data[:len(data) // 4 * 4])

        if os.path.basename(path) == 'sugilite.bin':
            for off, (name, shift) in SUGI_REGS.items():
                i = off // 4
                if i >= len(words):
                    continue
                v = words[i]
                extra = ''
                if shift:
                    extra = ' -> PA 0x%08x' % ((v << shift) & 0xffffffff)
                print('  %-12s +0x%04x = %08x%s' % (name, off, v, extra))

        hits = []
        for i, v in enumerate(words):
            if looks_like_pa(v):
                hits.append((i * 4, v))
        print('  PA-like words: %d' % len(hits))
        for off, v in hits[:40]:
            print('    +0x%05x  %08x  %s' % (off, v,
                                             'page-aligned' if v % 4096 == 0 else ''))
        if len(hits) > 40:
            print('    ... %d more' % (len(hits) - 40))
    return 0


if __name__ == '__main__':
    sys.exit(main())
