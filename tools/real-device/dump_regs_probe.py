#!/usr/bin/env python3
r"""Read-only register dump: Sugilite BAR + LIRO display engine (real A7R3).

Reads two MMIO windows through /dev/mem onto the SD card:
    sugilite.bin  0xf1000000  0x4000   (2D/blit engine BAR)
    liro.bin      0xf2a00000  0x23000  (LIRO display engine)

These are register reads of blocks that are active (the panel is showing a
picture), so no writes happen on the camera.  A hard hang here would only cost a
power cycle, so run it as its own single service session.

Afterwards power off, copy the two files and scan them for surface addresses:

  python3 analysis/qemu_a7r3/scan-regs-for-pa.py --files sugilite.bin liro.bin

usage: python dump_regs_probe.py
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

BLOCK = 4096
WINDOWS = [
    ('sugilite.bin', 0xF1000000, 0x00004000),
    ('liro.bin', 0xF2A00000, 0x00023000),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--format-card', action='store_true')
    args = ap.parse_args()

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

            total = 0
            for name, pa, size in WINDOWS:
                path = '%s/%s' % (mp, name)
                cmd = ('/bin/busybox dd if=/dev/mem of=%s bs=%d count=%d skip=%d '
                       '2>/dev/null; /bin/busybox wc -c < %s'
                       % (path, BLOCK, size // BLOCK, pa // BLOCK, path))
                out = shell.run(cmd, timeout=300)
                got = None
                for line in out.replace('\r', '\n').split('\n'):
                    s = line.strip()
                    if s.isdigit():
                        got = int(s)
                ok = got == size
                print('  %-13s 0x%08x +0x%05x -> %s bytes %s'
                      % (name, pa, size, got, 'OK' if ok else 'SHORT/FAILED'))
                if ok:
                    total += size

            run(shell, 'final sync', '/bin/busybox sync; /bin/busybox sync')
            run(shell, 'unmount', 'umount %s 2>&1' % mp)
            print('\nread %d bytes total' % total)
            print('DONE. Power the camera off, then copy the two files.')
            return 0 if total == sum(w[2] for w in WINDOWS) else 2
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
