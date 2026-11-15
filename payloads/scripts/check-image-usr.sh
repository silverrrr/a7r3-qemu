#!/bin/bash
# Read-only consistency check of the guest /usr partition in the eMMC image.
# QEMU must be stopped.  (The two-stage installer writes /usr/bin/iperf and
# /usr/tool/newmenu.so; a check after any concurrent-write scare is cheap.)
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 1531904 * 512))
SIZE=314572800

if pgrep -f 'qemu-system-arm -M cxd90045' >/dev/null; then
    echo "QEMU is running; stop it first"
    exit 1
fi
command -v e2fsck >/dev/null || { echo "e2fsck not installed"; exit 1; }
L=$(losetup -f --show -o "$OFF" --sizelimit "$SIZE" "$IMG") || exit 1
echo "loop=$L"
e2fsck -fn "$L"
rc=$?
losetup -d "$L"
echo "e2fsck rc=$rc"
