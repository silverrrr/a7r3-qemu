#!/bin/bash
# Verify the in-guest publish probe result offline: the replaced libIMDB.so and
# the retained old inode must both be byte-identical to the stock anchor.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
USR_OFF=$((524288 + 1531904 * 512))
M=/mnt/f/project/a7r3/analysis/tmp-usr-rw
R=/mnt/f/project/a7r3/dumps/nflasha15.bin
RM=/mnt/f/project/a7r3/analysis/tmp-usr-ro
STOCK=0f4d6355c0f8903714f8af506da7c4d357c9d66ed725fbc9f344fe82c928a321

mkdir -p "$M" "$RM"
umount "$M" 2>/dev/null; umount "$RM" 2>/dev/null
mount -o loop,ro,offset=$USR_OFF,sizelimit=314572800 "$IMG" "$M" || { echo IMG_MOUNT_FAIL; exit 1; }
mount -o loop,ro "$R" "$RM" || { echo DUMP_MOUNT_FAIL; exit 1; }

for f in lib/libIMDB.so lib/libIMDB.so.retained; do
    p="$M/$f"
    if [ -f "$p" ]; then
        got=$(sha256sum "$p" | cut -d' ' -f1)
        if [ "$got" = "$STOCK" ]; then
            printf '%-32s %6d B  MATCH stock\n' "$f" "$(stat -c %s "$p")"
        else
            printf '%-32s %6d B  DIFFER %s\n' "$f" "$(stat -c %s "$p")" "${got:0:16}"
        fi
    else
        printf '%-32s absent\n' "$f"
    fi
done

echo
echo "== stock file in the original dump"
sha256sum "$RM/lib/libIMDB.so" | cut -d' ' -f1
echo "== my /usr free space"
df -h "$M" | tail -1

umount "$M"; umount "$RM"
