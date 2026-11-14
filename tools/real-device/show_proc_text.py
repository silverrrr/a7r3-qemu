#!/usr/bin/env python3
r"""Print small /proc files straight from the shell (no SD card needed).

usage: python show_proc_text.py [/proc/vmallocinfo ...]
"""

import os
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, 'analysis'))

from dump_partitions import ShellSession  # noqa: E402
from probe_jiritsu import connect  # noqa: E402
from pmca.commands.usb import importDriver  # noqa: E402

FILES = sys.argv[1:] or ['/proc/vmallocinfo']


def main():
    with importDriver('libusb') as driver:
        cam, auth = connect(driver)
        shell = ShellSession(cam)
        try:
            for path in FILES:
                print('\n===== %s' % path)
                try:
                    out = shell.run('cat %s 2>&1; echo __END__' % path, timeout=120)
                except Exception as exc:
                    print('FAILED: %r' % (exc,))
                    continue
                for line in out.replace('\r', '\n').split('\n'):
                    if line.strip() and '__A7R3_' not in line and '__END__' not in line:
                        print(line)
            return 0
        finally:
            try:
                shell.client.close()
            except Exception:
                pass
            time.sleep(1)


if __name__ == '__main__':
    sys.exit(main())
