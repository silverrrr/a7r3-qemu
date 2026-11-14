#!/bin/bash
# Read a file the guest wrote to /setting (vfat) out of the eMMC image.
# Usage: read-guest-file.sh <name-in-/setting> [outfile]
set -u
NAME="${1:?usage: read-guest-file.sh <name> [outfile]}"
OUT="${2:-/tmp/$NAME}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/img2ro
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$M" || {
  echo MOUNT_FAIL; exit 1; }
ls -la "$M" | head -20
if [ -f "$M/$NAME" ]; then
  cp -f "$M/$NAME" "$OUT"
  echo "== copied $OUT $(stat -c %s "$OUT") B"
else
  echo "== /setting/$NAME not found"
fi
umount "$M"
