#!/bin/bash
# Repair the guest /usr ext2 after an unclean rw mount (QEMU must be stopped).
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 1531904 * 512))
SIZE=314572800
if pgrep -f 'qemu-system-arm -M cxd90045' >/dev/null; then
    echo "QEMU is running; stop it first"; exit 1
fi
L=$(losetup -f --show -o "$OFF" --sizelimit "$SIZE" "$IMG") || exit 1
echo "loop=$L"
e2fsck -fy "$L" 2>&1 | tail -8
rc=$?
losetup -d "$L"
echo "e2fsck -fy rc=$rc"
