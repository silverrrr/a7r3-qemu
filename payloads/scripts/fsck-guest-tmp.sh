#!/bin/bash
# Check (and fix) the guest /tmp partition's FAT because repeated hard shutdowns
# leave it dirty -> the guest mounts it read-only and the overlay's key channel
# (/tmp/nm_key) stops working.
#
#   fsck-guest-tmp.sh            check only
#   fsck-guest-tmp.sh fix        repair
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 374784 * 512))
SIZE=20971520
MODE="${1:-check}"

command -v fsck.vfat >/dev/null || { echo "fsck.vfat (dosfsck) not installed"; exit 1; }
if pgrep -f 'qemu-system-arm -M cxd90045' >/dev/null; then
    echo "QEMU is running; stop it first"
    exit 1
fi

L=$(losetup -f --show -o "$OFF" --sizelimit "$SIZE" "$IMG") || exit 1
echo "loop=$L"
if [ "$MODE" = "fix" ]; then
    fsck.vfat -a "$L"
else
    fsck.vfat -n "$L"
fi
rc=$?
losetup -d "$L"
echo "fsck rc=$rc"
exit 0
