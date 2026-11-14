#!/bin/bash
# Install a shim build as /usr/tool/libefence.so inside the existing eMMC image.
set -u
SRC="${1:?usage: update-image-shim.sh <shim.so>}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 1531904 * 512))
M=/tmp/img15rw
mkdir -p "$M"
umount "$M" 2>/dev/null || true
mount -o loop,rw,offset=$OFF,sizelimit=314572800 "$IMG" "$M" || { echo MOUNT_FAIL; exit 1; }
install -m 0755 "$SRC" "$M/tool/libefence.so"
sync
ls -la "$M/tool/libefence.so"
md5sum "$M/tool/libefence.so" "$SRC"
umount "$M"
