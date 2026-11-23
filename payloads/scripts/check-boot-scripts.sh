#!/bin/bash
# Does anything on A7R3 execute a shell script at boot (a hookable rc/profile)?
set -u
M=/mnt/f/project/a7r3/analysis/tmp-usr-ro
D=/mnt/f/project/a7r3/dumps
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,ro "$D/nflasha15.bin" "$M" || { echo MOUNT_FAIL; exit 1; }

for b in bootin.elf im.elf launch_shell.elf; do
    p="$M/bin/$b"
    [ -f "$p" ] || continue
    echo "== $b"
    strings -a "$p" | grep -E '\.sh$|/etc/|/tmp/|profile|rc\.local|init\.d' \
        | sort -u | head -20
done

echo
echo "== any *.sh in /usr that looks boot-related"
find "$M" -maxdepth 2 -name '*.sh' 2>/dev/null | head -20

umount "$M"
