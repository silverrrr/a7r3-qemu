#!/usr/bin/env python3
"""Inspect the embedded payload of a Sony .ltt font container.

The .ltt header is ASCII key=value lines; `embeddedoffset` points at the real
font data.  This prints what that data looks like (TTF header, zlib, custom) and
tries a few decompressions.

usage: ltt-probe.py <file.ltt>
"""
import re
import sys
import zlib


def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    head = data[:4096].decode('latin-1', 'replace')
    kv = dict(re.findall(r'([a-z/0-9_]+)\s*=\s*([^\n]+)\n', head))
    print('file %s (%d bytes)' % (path, len(data)))
    for k in ('name', 'fileversion', 'embeddedoffset', 'embeddedsize',
              'name_offset', 'name_size', 'grouptypesoffset'):
        if k in kv:
            print('  %-18s %s' % (k, kv[k].strip()))
    try:
        off = int(kv['embeddedoffset'])
        size = int(kv['embeddedsize'])
    except (KeyError, ValueError):
        print('no embedded payload')
        return 1
    blob = data[off:off + size]
    print('payload at 0x%x, %d bytes, head=%s' % (off, len(blob), blob[:16].hex(' ')))
    magic = blob[:4]
    if magic in (b'\x00\x01\x00\x00', b'OTTO', b'ttcf'):
        print('  -> plain TrueType/OpenType')
    elif blob[:2] == b'\x78\x9c' or blob[:2] == b'\x78\xda':
        try:
            out = zlib.decompress(blob)
            print('  -> zlib, %d bytes, head=%s' % (len(out), out[:16].hex(' ')))
        except Exception as exc:
            print('  zlib failed: %r' % (exc,))
    elif len(blob) > 8 and blob[0] == 0x1f and blob[1] == 0x8b:
        print('  -> gzip')
    else:
        # count printable ascii and look for table tags
        tags = [t for t in (b'cmap', b'glyf', b'loca', b'head', b'hhea',
                            b'maxp', b'name', b'CFF ', b'EBLC', b'EBDT')
                if t in blob[:0x2000]]
        print('  -> unknown; table tags in first 8K: %s' % [t.decode() for t in tags])
        print('  first 64: %s' % blob[:64].hex(' '))
    return 0


if __name__ == '__main__':
    sys.exit(main())
