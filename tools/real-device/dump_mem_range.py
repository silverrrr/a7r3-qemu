#!/usr/bin/env python3
r"""Read-only RAM range dump for the real A7R3 (service mode 0336).

Dumps a physical memory range through /dev/mem onto the inserted SD card in
chunks (default 0x20000000 + 64 MiB, 8 x 8 MiB files) and verifies every size.

Nothing on the camera is written; only reads.

Afterwards power the camera off, copy the chunks off the card and search them:

  python3 analysis/qemu_a7r3/find-ui-surface.py mem_*.bin

usage: python dump_mem_range.py [--base 0x20000000] [--size 0x4000000]
                                [--chunk 0x800000] [--prefix mem]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', type=lambda s: int(s, 0), default=0x20000000)
    ap.add_argument('--size', type=lambda s: int(s, 0), default=0x4000000)
    ap.add_argument('--chunk', type=lambda s: int(s, 0), default=0x800000)
    ap.add_argument('--prefix', default='mem')
    ap.add_argument('--format-card', action='store_true')
    args = ap.parse_args()

    for v, name in ((args.base, 'base'), (args.size, 'size'), (args.chunk, 'chunk')):
        if v % BLOCK:
            ap.error('%s must be a multiple of %d' % (name, BLOCK))

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

            total_ok = 0
            off = 0
            while off < args.size:
                n = min(args.chunk, args.size - off)
                pa = args.base + off
                name = '%s_%08x.bin' % (args.prefix, pa)
                path = '%s/%s' % (mp, name)
                cmd = ('/bin/busybox dd if=/dev/mem of=%s bs=%d count=%d skip=%d '
                       '2>/dev/null; /bin/busybox wc -c < %s'
                       % (path, BLOCK, n // BLOCK, pa // BLOCK, path))
                out = shell.run(cmd, timeout=600)
                size = None
                for line in out.replace('\r', '\n').split('\n'):
                    s = line.strip()
                    if s.isdigit():
                        size = int(s)
                ok = size == n
                print('  %-22s 0x%08x  %s bytes  %s'
                      % (name, pa, size, 'OK' if ok else 'SHORT/FAILED'))
                if ok:
                    total_ok += n
                off += n

            run(shell, 'final sync', '/bin/busybox sync; /bin/busybox sync')
            run(shell, 'unmount', 'umount %s 2>&1' % mp)
            print('\nread %d of %d bytes' % (total_ok, args.size))
            print('DONE. Power the camera off, then copy the mem_*.bin files.')
            return 0 if total_ok == args.size else 2
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
