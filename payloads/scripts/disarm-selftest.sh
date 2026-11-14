#!/bin/bash
# Remove the self-test marker so the overlay will not auto-commit settings.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
S=/tmp/img2disarm
mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || exit 1
rm -f "$S/nm_selftest"
sync
ls -la "$S" | grep -E 'newmenu|nm_'
umount "$S"
echo "selftest disarmed"
