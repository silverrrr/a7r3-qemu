#!/bin/bash
# Extract the firmware's libc/libpthread/libdl out of the eMMC image's rootfs so
# the payload can be linked with "NEEDED libc.so.6" (needed when it is
# dlopen()ed at run time instead of preloaded -- see A7R3_NOTES 66).
#
# The extracted .so files are Sony firmware content: they stay in
# analysis/libs/ (git-ignored) and are never published.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
ROOTFS_OFF=$((524288 + 319488 * 512))
ROOTFS_SIZE=8388608
OUT=/mnt/f/project/a7r3/analysis/libs
M=/mnt/img7libs

mkdir -p "$OUT" "$M"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$ROOTFS_OFF,sizelimit=$ROOTFS_SIZE "$IMG" "$M" || {
    echo ROOTFS_MOUNT_FAIL; exit 1; }

for f in libc-2.11.2.so libpthread-2.11.2.so libdl-2.11.2.so; do
    if [ -f "$M/lib/$f" ]; then
        cp "$M/lib/$f" "$OUT/$f"
        echo "  $f"
    fi
done
umount "$M"
ls -l "$OUT"
echo "now run build-newmenu.sh (it picks the libs up automatically)"
