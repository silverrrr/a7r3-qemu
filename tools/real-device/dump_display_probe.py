#!/usr/bin/env python3
r"""Read-only display-path probe for the real A7R3 (service mode 0336).

Everything happens inside ONE service session and **nothing on the camera is
written** (the only writes go to the inserted SD card):

  1. text evidence on the card:
       iomem.txt      /proc/iomem      (reserved "framebuffer"-like regions)
       fb.txt         /proc/fb + /sys/class/graphics/* listing
       dmesg.txt      kernel messages mentioning lcd/disp/fb/grm
       mods.txt       /proc/modules
       applets.txt    /bin/busybox --list   (what we can use later)
       devmem.txt     ls -l /dev/mem + a 4-byte read test
  2. three candidate display buffers via /dev/mem (read-only):
       fb_render.bin  from 0x202E0000   (0x202E0200 is 0x200 into it)
       fb_flipA.bin   from 0x23A80000
       fb_flipB.bin   from 0x23B16000
     614400 bytes each (640x480x2)

Why: QEMU showed the UI presenting as 0x202E0200 -> {0x23A80000, 0x23B16000}.
The open question is which of those the real LCD scans out.  Comparing these
dumps with what is on the camera screen answers it.

Power the camera OFF after DONE (do not call auth.stop), then copy the files
from the card and decode them with:

  python3 analysis/qemu_a7r3/raw-device2png.py fb_flipA.bin /tmp/devA

usage: python dump_display_probe.py [--format-card]
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
COUNT = 640 * 480 * 2 // BLOCK          # 150 blocks = 614400 bytes
TARGETS = [
    ('fb_render.bin', 0x202E0000),      # render target 0x202E0200 at +0x200
    ('fb_flipA.bin', 0x23A80000),       # present buffer A
    ('fb_flipB.bin', 0x23B16000),       # present buffer B
]


def text_dump(shell, mountpoint, name, command, timeout=120):
    path = '%s/%s' % (mountpoint, name)
    cmd = '(%s) > %s 2>&1; /bin/busybox wc -c < %s' % (command, path, path)
    out = shell.run(cmd, timeout=timeout)
    size = None
    for line in out.replace('\r', '\n').split('\n'):
        s = line.strip()
        if s.isdigit():
            size = int(s)
    print('  %-12s %s bytes' % (name, size))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--format-card', action='store_true',
                    help='format Slot 1 as FAT32 if it is exFAT (erases the card)')
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

            print('\n--- text evidence ---')
            text_dump(shell, mp, 'iomem.txt', 'cat /proc/iomem')
            text_dump(shell, mp, 'fb.txt',
                      'cat /proc/fb 2>&1; ls -l /sys/class/graphics 2>&1; '
                      'for d in /sys/class/graphics/*; do echo "== $d"; '
                      'cat $d/name 2>&1; cat $d/stride 2>&1; '
                      'cat $d/virtual_size 2>&1; done')
            text_dump(shell, mp, 'dmesg.txt',
                      '/bin/busybox dmesg 2>&1')
            text_dump(shell, mp, 'mods.txt', 'cat /proc/modules')
            text_dump(shell, mp, 'applets.txt', '/bin/busybox --list')
            text_dump(shell, mp, 'devmem.txt',
                      'ls -l /dev/mem 2>&1; '
                      '/bin/busybox dd if=/dev/mem bs=4 count=1 skip=%d 2>&1 '
                      '| /bin/busybox wc -c'
                      % (0x202E0000 // 4))
            text_dump(shell, mp, 'cmdline.txt', 'cat /proc/cmdline; uname -a')

            print('\n--- display buffer reads (read-only) ---')
            for name, pa in TARGETS:
                skip = pa // BLOCK
                path = '%s/%s' % (mp, name)
                cmd = ('/bin/busybox dd if=/dev/mem of=%s bs=%d count=%d skip=%d '
                       '2>/dev/null; /bin/busybox wc -c < %s'
                       % (path, BLOCK, COUNT, skip, path))
                out = shell.run(cmd, timeout=300)
                size = None
                for line in out.replace('\r', '\n').split('\n'):
                    s = line.strip()
                    if s.isdigit():
                        size = int(s)
                print('  %-13s from 0x%08x -> %s bytes %s'
                      % (name, pa, size,
                         'OK' if size == 640 * 480 * 2 else 'SHORT/FAILED'))

            run(shell, 'final sync', '/bin/busybox sync; /bin/busybox sync')
            run(shell, 'unmount', 'umount %s 2>&1' % mp)
            print('\nDONE. Power the camera off, then copy the files from the card.')
            print('Do not call auth.stop(); power off directly.')
            return 0
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
