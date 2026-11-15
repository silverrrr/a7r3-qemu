#!/bin/bash
# Show the overlay gates in the guest /setting partition and clean up any stale
# loop mounts left behind by an interrupted script.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
S=/tmp/img2flags

for d in /tmp/img2flags /tmp/img2nm /tmp/img2gate /mnt/img2x; do
    if mountpoint -q "$d" 2>/dev/null; then
        umount "$d" && echo "unmounted $d"
    fi
done

mkdir -p "$S"
if ! mountpoint -q "$S"; then
    mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || {
        echo MOUNT_FAIL; exit 1; }
fi

echo "== overlay gates in /setting"
for f in newmenu nm_flip nm_badge nm_kick nm_selftest nm_keyhook nm_nobadge; do
    if [ -e "$S/$f" ]; then echo "  on   $f"; else echo "  off  $f"; fi
done
umount "$S"
echo "== loop devices"
losetup -a | head -5
