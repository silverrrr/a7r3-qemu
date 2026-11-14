#!/bin/bash
# Toggle overlay gate markers in the guest /setting partition.
#   set-overlay-gates.sh on|off
# on : /setting/newmenu (enable overlay), /setting/nm_flip (write flip buffers)
# off: remove both
set -u
MODE="${1:-on}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
S=/tmp/img2gate
mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || {
  echo SET_MOUNT_FAIL; exit 1; }
if [ "$MODE" = "on" ]; then
    : > "$S/newmenu"
    : > "$S/nm_flip"
else
    rm -f "$S/newmenu" "$S/nm_flip"
fi
rm -f "$S/newmenu.log"
sync
ls -la "$S" | grep -E 'newmenu|nm_flip' || true
umount "$S"
echo "gates $MODE"
