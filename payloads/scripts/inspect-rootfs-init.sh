#!/bin/bash
# What does A7R3's rootfs start, and is there a hookable shell script?
set -u
D=/mnt/f/project/a7r3/dumps
R=/mnt/f/project/a7r3/analysis/tmp-root-ro

mkdir -p "$R"
umount "$R" 2>/dev/null
mount -o loop,ro "$D/nflasha7.bin" "$R" || { echo MOUNT_FAIL; exit 1; }

echo "== rootfs top level"
ls -la "$R" | sed -n '1,20p'
echo
echo "== /etc"
ls -la "$R/etc" 2>/dev/null | sed -n '1,25p'
echo
echo "== /sbin"
ls -la "$R/sbin" 2>/dev/null | head -12
echo
echo "== scripts under /etc (shell)"
find "$R/etc" -maxdepth 2 -type f 2>/dev/null | head -20
echo
echo "== who mentions profile"
grep -rla 'profile' "$R" 2>/dev/null | head -10
echo
echo "== init-ish binaries and their startup strings"
for b in "$R"/sbin/init* "$R"/bin/sh "$R"/bin/ash; do
    [ -f "$b" ] || continue
    echo "-- $(basename "$b")"
    strings -a "$b" | grep -E '/etc/|profile|rc\.|init\.d|startup' | sort -u | head -15
done
umount "$R"
