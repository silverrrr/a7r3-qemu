#!/bin/bash
# Install the freestanding publisher into the guest's /setting (QEMU stopped).
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
SET_OFF=$((524288 + 18432 * 512))
S=/tmp/img2pubelf
SRC=/mnt/f/project/a7r3/analysis/payloads/publish.elf

mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || { echo SET_MOUNT_FAIL; exit 1; }
install -m 0755 "$SRC" "$S/publish.elf"
sync
ls -l "$S/publish.elf"
umount "$S"
echo "installed /setting/publish.elf"
