#!/bin/bash
# Restore /usr/lib/libIMDB.so to mode 0555 in the QEMU image (QEMU stopped).
set -u
M=/mnt/f/project/a7r3/analysis/tmp-mode
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 1531904 * 512)),sizelimit=314572800 \
    /root/firmware/a7r3/qemu-emmc-2g.img "$M" || { echo MOUNT_FAIL; exit 1; }
chmod 0555 "$M/lib/libIMDB.so"
sync
ls -l "$M/lib/libIMDB.so"
umount "$M"
echo "mode restored"
