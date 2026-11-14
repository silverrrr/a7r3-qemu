#!/usr/bin/env python3
"""Extract the id -> UTF-8 string table from a Sony UXC string container.

Format (reverse-engineered from the A7R3 assets):

  header : 'uxc\\x07', then offsets/table, then records
  record : u32 id, UTF-8 text, NUL padding to 4 bytes

Setting title resids are 0x02xxxxxx; the strings live at id 0x1E|low24 in this
file, so `resid 0x0204E146 -> id 0x1E04E146 -> 版本`.

usage: uxc-strtable.py <string_xx.uxc> [--json out.json] [--resid 0x204e146]
"""
import argparse
import json
import struct
import sys


def looks_utf8_text(b):
    if not b:
        return False
    try:
        s = b.decode('utf-8')
    except UnicodeDecodeError:
        return False
    if not s:
        return False
    # reject control characters
    return all(ch >= ' ' or ch in '\t\n' for ch in s)


def parse(data):
    """Yield (id, text) records: u32 id followed by NUL-terminated UTF-8."""
    out = {}
    i = 16
    n = len(data)
    while i + 8 < n:
        rid = struct.unpack_from('<I', data, i)[0]
        # text starts at i+4
        j = data.find(b'\x00', i + 4, min(n, i + 4 + 4096))
        if j < 0:
            i += 4
            continue
        raw = data[i + 4:j]
        if raw and looks_utf8_text(raw):
            out[rid] = raw.decode('utf-8')
            i = (j + 4) & ~3
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--json')
    ap.add_argument('--resid', action='append', default=[])
    args = ap.parse_args()

    data = open(args.path, 'rb').read()
    table = parse(data)
    print('%s: %d entries' % (args.path, len(table)))
    ids = sorted(table)
    if ids:
        print('  id range 0x%08x..0x%08x' % (ids[0], ids[-1]))
    for r in args.resid:
        v = int(r, 0)
        sid = 0x1E000000 | (v & 0xFFFFFF)
        print('  resid %08x -> id %08x : %s'
              % (v, sid, table.get(sid, '<missing>')))
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump({('%08x' % k): v for k, v in table.items()}, f,
                      ensure_ascii=False, indent=0, sort_keys=True)
        print('  wrote %s' % args.json)
    return 0


if __name__ == '__main__':
    sys.exit(main())
