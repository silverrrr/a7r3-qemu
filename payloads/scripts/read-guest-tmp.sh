#!/bin/bash
# Read a file the guest wrote to /tmp (vfat) out of the eMMC image.
# QEMU must be stopped first.  Usage: read-guest-tmp.sh <name-in-/tmp> [outfile]
set -u
NAME="${1:?usage: read-guest-tmp.sh <name> [outfile]}"
OUT="${2:-/tmp/$NAME}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/imgtmp_ro
if pgrep -f 'qemu-system-arm -M cxd90045' >/dev/null; then
    echo "QEMU is running; stop it first"
    exit 1
fi
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 374784 * 512)),sizelimit=20971520 "$IMG" "$M" || {
    echo MOUNT_FAIL; exit 1; }
if [ -f "$M/$NAME" ]; then
    cp -f "$M/$NAME" "$OUT"
    echo "== copied $OUT $(stat -c %s "$OUT") B"
else
    echo "== /tmp/$NAME not found; /tmp holds:"
    ls -la "$M" | tail -n +4
fi
umount "$M"
