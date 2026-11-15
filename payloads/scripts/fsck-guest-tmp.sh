#!/bin/bash
# Check (and fix) the guest /tmp partition's FAT because repeated hard shutdowns
# leave it dirty -> the guest mounts it read-only and the overlay's key channel
# (/tmp/nm_key) stops working.
#
#   fsck-guest-tmp.sh            check only
#   fsck-guest-tmp.sh fix        repair
#   fsck-guest-tmp.sh clean      repair + delete the logs that fill the 12 MB
#
# Why "clean" exists: /tmp is only 12 MB; hook.log (shim tracing) and
# newmenu.log grow across runs until the partition is full, which looks exactly
# like "the overlay ignores my clicks".
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
if [ "$MODE" = "check" ]; then
    fsck.vfat -n "$L"
else
    fsck.vfat -a "$L"
fi
rc=$?
losetup -d "$L"
echo "fsck rc=$rc"
[ "$rc" -eq 0 ] || exit 0

if [ "$MODE" = "clean" ]; then
    S=/tmp/tmpclean
    mkdir -p "$S"
    umount "$S" 2>/dev/null
    mount -o loop,rw,offset=$OFF,sizelimit=$SIZE "$IMG" "$S" || {
        echo CLEAN_MOUNT_FAIL; exit 1; }
    before=$(du -sk "$S" | cut -f1)
    rm -f "$S"/*.log "$S"/*.raw "$S"/nm_key "$S"/nm_key.tmp \
          "$S"/nm_touch "$S"/nm_touch.tmp 2>/dev/null
    after=$(du -sk "$S" | cut -f1)
    echo "clean: ${before} KiB -> ${after} KiB"
    sync
    umount "$S"
fi
exit 0
