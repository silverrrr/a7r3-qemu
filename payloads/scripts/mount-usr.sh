#!/bin/bash
# Ensure the real /usr partition is mounted read-only at /tmp/libx.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/libx
mkdir -p "$M"
if ! mountpoint -q "$M"; then
    umount "$M" 2>/dev/null
    mount -o loop,ro,offset=784859136,sizelimit=314572800 "$IMG" "$M" || {
        echo MOUNT_FAIL; exit 1; }
fi
exit 0
