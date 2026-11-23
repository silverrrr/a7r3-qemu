#!/bin/bash
# Install the in-guest publish probe into /setting so the guest shell can run it.
# QEMU must be stopped.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
SET_OFF=$((524288 + 18432 * 512))
S=/tmp/img2pub
SRC=/mnt/f/project/a7r3/analysis/qemu_a7r3/publish_probe.sh

mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || { echo SET_MOUNT_FAIL; exit 1; }
install -m 0755 "$SRC" "$S/publish_probe.sh"
sync
ls -l "$S/publish_probe.sh"
umount "$S"
echo "installed /setting/publish_probe.sh"
