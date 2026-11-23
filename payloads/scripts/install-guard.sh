#!/bin/bash
# Install guard.elf (and publish.elf) into the guest's /setting. QEMU stopped.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
SET_OFF=$((524288 + 18432 * 512))
S=/tmp/img2guard
P=/mnt/f/project/a7r3/analysis/payloads

mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || { echo SET_MOUNT_FAIL; exit 1; }
install -m 0755 "$P/publish.elf" "$S/publish.elf"
install -m 0755 "$P/guard.elf"   "$S/guard.elf"
sync
ls -l "$S/publish.elf" "$S/guard.elf"
umount "$S"
echo "installed /setting/publish.elf and /setting/guard.elf"
