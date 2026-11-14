#!/usr/bin/env python3
"""Probe the A7R3 UXC string container for readable text.

Looks for known menu labels (显示质量, 触摸操作, ...) in UTF-8, UTF-16LE and GBK,
then dumps a sample of decoded strings so the container layout can be worked out.

usage: probe-uxc-strings.py <string_xx.uxc> [--dump N]
"""
import sys

SAMPLES = ['显示质量', '触摸操作', '自动关机', '清洁模式', '画质', '我的菜单']


def main():
    path = sys.argv[1]
    n_dump = 40
    if '--dump' in sys.argv:
        n_dump = int(sys.argv[sys.argv.index('--dump') + 1])
    data = open(path, 'rb').read()
    print('%s: %d bytes, magic %r' % (path, len(data), data[:4]))

    for s in SAMPLES:
        for enc in ('utf-8', 'utf-16-le', 'gbk'):
            try:
                pat = s.encode(enc)
            except UnicodeEncodeError:
                continue
            i = data.find(pat)
            if i >= 0:
                print('  found %-12s %-9s at 0x%x' % (s, enc, i))

    # ASCII runs
    runs = []
    cur = bytearray()
    for b in data:
        if 0x20 <= b < 0x7f:
            cur.append(b)
        else:
            if len(cur) >= 6:
                runs.append((len(runs), bytes(cur)))
            cur.clear()
    print('  ascii runs >=6: %d' % len(runs))

    # UTF-16LE CJK runs
    cjk = []
    i = 0
    cur = []
    while i + 1 < len(data):
        cp = data[i] | (data[i + 1] << 8)
        if 0x4e00 <= cp <= 0x9fff or 0x3000 <= cp <= 0x30ff:
            cur.append(chr(cp))
            i += 2
            continue
        if len(cur) >= 3:
            cjk.append(''.join(cur))
        cur = []
        i += 1
    if len(cur) >= 3:
        cjk.append(''.join(cur))
    print('  utf16le CJK runs >=3: %d' % len(cjk))
    for s in cjk[:n_dump]:
        print('    %s' % s)
    print('  ascii sample:')
    for _n, s in runs[:n_dump]:
        print('    %s' % s.decode('latin-1'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
