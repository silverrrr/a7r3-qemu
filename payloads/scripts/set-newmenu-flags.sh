#!/bin/bash
# Create/remove the payload's feature gates in the guest /setting partition.
#
#   set-newmenu-flags.sh                    list them
#   set-newmenu-flags.sh keyhook on|off     hook the MENU key (back)
#   set-newmenu-flags.sh badge on|off       corner switch button over the native UI
#   set-newmenu-flags.sh selftest on|off    boot-time value-change self test
#   set-newmenu-flags.sh flip on|off        write the present (flip) buffers
#
# The payload reads these at runtime, so no rebuild is needed:
#   /setting/newmenu     overlay on        /setting/nm_flip    flip-buffer writes
#   /setting/nm_badge    corner button on the *native* screen (default OFF:
#                        writing the native render target during a dialog made
#                        the dialog come out black, see A7R3_NOTES 62.9)
#   /setting/nm_kick     on close, poke the native UI with DOWN+UP so it repaints
#                        instead of restoring the grabbed frame (default OFF)
#   /setting/nm_selftest boot self test
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
S=/tmp/img2flags
mkdir -p "$S"
umount "$S" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || {
  echo SET_MOUNT_FAIL; exit 1; }

name="${1:-}"; mode="${2:-}"
if [ -z "$name" ]; then
    ls -la "$S" | grep -E 'newmenu|nm_' || true
    umount "$S"
    exit 0
fi

case "$name" in
    keyhook)  file=nm_keyhook; invert=0 ;;
    badge)    file=nm_badge;   invert=0 ;;
    kick)     file=nm_kick;    invert=0 ;;
    selftest) file=nm_selftest; invert=0 ;;
    flip)     file=nm_flip;     invert=0 ;;
    overlay)  file=newmenu;     invert=0 ;;
    *) echo "unknown flag: $name"; umount "$S"; exit 1 ;;
esac

if [ "$mode" = "on" ]; then
    if [ "$invert" = "1" ]; then rm -f "$S/$file"; else : > "$S/$file"; fi
elif [ "$mode" = "off" ]; then
    if [ "$invert" = "1" ]; then : > "$S/$file"; else rm -f "$S/$file"; fi
else
    echo "usage: $0 <flag> on|off"; umount "$S"; exit 1
fi

sync
ls -la "$S" | grep -E 'newmenu|nm_' || true
umount "$S"
echo "$name $mode"
