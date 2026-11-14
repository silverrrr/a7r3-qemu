#!/bin/bash
# Install the overlay and arm the self-test marker in /setting.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
S=/tmp/img2selftest
mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || {
  echo SET_MOUNT_FAIL; exit 1; }
: > "$S/newmenu"
: > "$S/nm_flip"
: > "$S/nm_selftest"
rm -f "$S/newmenu.log"
sync
ls -la "$S" | grep -E 'newmenu|nm_' || true
umount "$S"
echo "selftest armed"
