#!/usr/bin/env python3
r"""Read-only /proc text probe (real A7R3 service mode).

Collects the kernel's own view of its mappings and devices onto the SD card:

  vmallocinfo.txt   /proc/vmallocinfo   -> every ioremap with phys= address
  interrupts.txt    /proc/interrupts
  devices.txt       /proc/devices + /proc/misc
  kallsyms.txt      /proc/kallsyms      (empty if kptr_restrict)
  debugfs.txt       ls -R /sys/kernel/debug (if mounted)
  dt.txt            /proc/device-tree presence + root listing

Nothing on the camera is written.  usage: python dump_text_probe2.py
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

ITEMS = [
    ('vmallocinfo.txt', 'cat /proc/vmallocinfo'),
    ('interrupts.txt', 'cat /proc/interrupts'),
    ('devices.txt', 'cat /proc/devices; echo ---; cat /proc/misc'),
    ('kallsyms.txt', 'cat /proc/kallsyms'),
    ('debugfs.txt', 'ls -l /sys/kernel/debug 2>&1; '
                    'cat /proc/mounts 2>&1 | /bin/busybox tail -n 30'),
    ('dt.txt', 'ls -l /proc/device-tree 2>&1; ls -l /sys/firmware/devicetree 2>&1'),
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
            for name, cmd in ITEMS:
                path = '%s/%s' % (mp, name)
                full = '(%s) > %s 2>&1; /bin/busybox wc -c < %s' % (cmd, path, path)
                out = shell.run(full, timeout=300)
                size = None
                for line in out.replace('\r', '\n').split('\n'):
                    s = line.strip()
                    if s.isdigit():
                        size = int(s)
                print('  %-16s %s bytes' % (name, size))

            run(shell, 'final sync', '/bin/busybox sync; /bin/busybox sync')
            run(shell, 'unmount', 'umount %s 2>&1' % mp)
            print('\nDONE. Power the camera off, then copy the txt files.')
            return 0
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
