#!/bin/bash
# Hash the guest's libIMDB.so variants straight out of the QEMU image.
# QEMU must be stopped.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
USR_OFF=$((524288 + 1531904 * 512))
SET_OFF=$((524288 + 18432 * 512))
U=/mnt/f/project/a7r3/analysis/tmp-usrcmp
S=/mnt/f/project/a7r3/analysis/tmp-setcmp

mkdir -p "$U" "$S"
umount "$U" 2>/dev/null; umount "$S" 2>/dev/null
mount -o loop,ro,offset=$USR_OFF,sizelimit=314572800 "$IMG" "$U" || { echo USR_FAIL; exit 1; }
mount -o loop,ro,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || { echo SET_FAIL; exit 1; }

for p in "$U/lib/libIMDB.so" "$U/lib/libIMDB.so.retained" \
         "$U/lib/.libIMDB.standby" "$S/libIMDB.candidate"; do
    if [ -f "$p" ]; then
        printf '%-46s %8d  %s\n' "${p#$U}" "${p#$S}" 2>/dev/null || true
        printf '%-46s %8d  %s\n' "$(basename "$p")" "$(stat -c %s "$p")" \
            "$(sha256sum "$p" | cut -d' ' -f1)"
    else
        printf '%-46s absent\n' "$(basename "$p")"
    fi
done

echo "== inode numbers"
ls -li "$U/lib/libIMDB.so" "$U/lib/libIMDB.so.retained" 2>/dev/null || true
umount "$U"; umount "$S"
