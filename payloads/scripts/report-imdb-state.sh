#!/bin/bash
# Report the image's libIMDB.so state (mode, sha, leftover aliases).
set -u
M=/mnt/f/project/a7r3/analysis/tmp-chk
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 1531904 * 512)),sizelimit=314572800 \
    /root/firmware/a7r3/qemu-emmc-2g.img "$M" || { echo MOUNT_FAIL; exit 1; }
ls -l "$M/lib/libIMDB.so"
sha256sum "$M/lib/libIMDB.so" | cut -d' ' -f1
ls "$M"/lib/libIMDB.so.* 2>/dev/null || echo "(no aliases)"
umount "$M"
