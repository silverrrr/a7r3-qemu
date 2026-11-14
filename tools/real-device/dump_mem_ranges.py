#!/usr/bin/env python3
r"""Read-only multi-range RAM dump for the real A7R3 (service mode 0336).

Dumps several physical ranges in one session, in chunks, onto the SD card and
verifies every size.  Nothing on the camera is written except the card.

usage: python dump_mem_ranges.py --ranges 0x3000000:0x1000000,0x18800000:0x1000000
                                 [--chunk 0x800000] [--prefix rmem]
"""

import argparse
import os
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, 'analysis'))

from dump_to_sdcard import prepare_card, run  # noqa: E402
from dump_partitions import ShellSession  # noqa: E402
from probe_jiritsu import connect  # noqa: E402
from pmca.commands.usb import importDriver  # noqa: E402

BLOCK = 65536


def parse_ranges(text):
    out = []
    for part in text.split(','):
        base_s, size_s = part.split(':')
        out.append((int(base_s, 0), int(size_s, 0)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ranges', required=True)
    ap.add_argument('--chunk', type=lambda s: int(s, 0), default=0x800000)
    ap.add_argument('--prefix', default='rmem')
    ap.add_argument('--format-card', action='store_true')
    args = ap.parse_args()

    ranges = parse_ranges(args.ranges)
    for base, size in ranges:
        for v, name in ((base, 'base'), (size, 'size'), (args.chunk, 'chunk')):
            if v % BLOCK:
                ap.error('%s (0x%x) must be a multiple of %d' % (name, v, BLOCK))

    with importDriver('libusb') as driver:
        cam, auth = connect(driver)
        shell = ShellSession(cam)
        try:
            target = prepare_card(shell, args.format_card)
            if target is None:
                print('\nNo card could be mounted read-write. Nothing was written.')
                return 1
            node, mp = target
            print('\n=== mount point %s (%s)' % (mp, node))

            ok_total = 0
            want_total = sum(size for _base, size in ranges)
            for base, size in ranges:
                off = 0
                while off < size:
                    n = min(args.chunk, size - off)
                    pa = base + off
                    name = '%s_%08x.bin' % (args.prefix, pa)
                    path = '%s/%s' % (mp, name)
                    cmd = ('/bin/busybox dd if=/dev/mem of=%s bs=%d count=%d '
                           'skip=%d 2>/dev/null; /bin/busybox wc -c < %s'
                           % (path, BLOCK, n // BLOCK, pa // BLOCK, path))
                    out = shell.run(cmd, timeout=600)
                    got = None
                    for line in out.replace('\r', '\n').split('\n'):
                        s = line.strip()
                        if s.isdigit():
                            got = int(s)
                    ok = got == n
                    print('  %-22s 0x%08x  %s bytes  %s'
                          % (name, pa, got, 'OK' if ok else 'SHORT/FAILED'))
                    if ok:
                        ok_total += n
                    off += n

            run(shell, 'final sync', '/bin/busybox sync; /bin/busybox sync')
            run(shell, 'unmount', 'umount %s 2>&1' % mp)
            print('\nread %d of %d bytes' % (ok_total, want_total))
            print('DONE. Power the camera off, then copy the files.')
            return 0 if ok_total == want_total else 2
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
