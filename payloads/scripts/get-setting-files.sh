#!/bin/bash
# Copy files matching a glob from the guest /setting (vfat, img2) to a host dir.
#   get-setting-files.sh 'gsc_*' /mnt/f/project/a7r3/analysis/gsc1
set -u
PAT="${1:?usage: get-setting-files.sh <glob> <outdir>}"
OUT="${2:?usage: get-setting-files.sh <glob> <outdir>}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/img2get
mkdir -p "$M" "$OUT"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$M" || {
  echo MOUNT_FAIL; exit 1; }
n=0
for f in "$M"/$PAT; do
  [ -f "$f" ] || continue
  cp "$f" "$OUT/"
  n=$((n + 1))
done
umount "$M"
echo "copied $n file(s) to $OUT"
ls -la "$OUT" | head -30
