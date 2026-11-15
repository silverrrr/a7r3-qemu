#!/bin/bash
# Install the two-stage loader:
#     /usr/tool/nm_boot.so   tiny stub (preload entry)
#     /setting/newmenu.so    the real overlay  <- "SD card" placeholder
#     /setting/nm_payload.conf  first line = payload path
#
# The stub waits up to 20 s for the payload path to appear, then dlopen()s it.
# Missing payload (pulled card) or /setting/nm_off => the camera boots stock.
#
# usage: install-nm-boot.sh [on|off|status]
#   on     build+install stub, move the overlay out of /usr, point conf at it
#   off    restore the single-stage install (overlay back in /usr)
#   status show what is installed
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
USR_OFF=$((524288 + 1531904 * 512))
SET_OFF=$((524288 + 18432 * 512))
P=/mnt/f/project/a7r3/analysis/payloads
M=/tmp/img15boot
S=/tmp/img2boot
STUB="$P/nm_boot.so"
OVERLAY="$P/newmenu.so"

mode="${1:-status}"

mount_usr() {
    mkdir -p "$M"
    umount "$M" 2>/dev/null
    mount -o loop,rw,offset=$USR_OFF,sizelimit=314572800 "$IMG" "$M" || return 1
}

mount_set() {
    mkdir -p "$S"
    umount "$S" 2>/dev/null
    mount -o loop,rw,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || return 1
}

set_preload() {
    # the firmware reads the preload list from the first 512 bytes of iperf
    python3 - "$M/bin/iperf" "$1" <<'PY'
import sys
path, spec = sys.argv[1], sys.argv[2]
data = bytearray(open(path, 'rb').read())
if len(data) < 512:
    data += bytes(512 - len(data))
data[:512] = spec.encode() + bytes(512 - len(spec))
open(path, 'wb').write(bytes(data))
print("  preload -> %s" % spec)
PY
}

case "$mode" in
status)
    mount_usr || { echo USR_MOUNT_FAIL; exit 1; }
    echo "== /usr/tool"
    ls -l "$M/tool" 2>/dev/null | grep -E 'nm_boot|newmenu' || echo "  (neither)"
    head -c 80 "$M/bin/iperf" | tr -d '\0' | sed 's/^/  preload: /'
    echo
    umount "$M"
    mount_set || { echo SET_MOUNT_FAIL; exit 1; }
    echo "== /setting"
    for f in newmenu newmenu.so nm_payload.conf nm_off nm_badge nm_flip; do
        if [ -e "$S/$f" ]; then echo "  present $f"; else echo "  absent  $f"; fi
    done
    umount "$S"
    ;;

on)
    [ -f "$STUB" ] || { echo "missing $STUB (run build-nm-boot.sh)"; exit 1; }
    [ -f "$OVERLAY" ] || { echo "missing $OVERLAY"; exit 1; }
    mount_set || { echo SET_MOUNT_FAIL; exit 1; }
    install -m 0755 "$OVERLAY" "$S/newmenu.so"
    printf '/setting/newmenu.so\n' > "$S/nm_payload.conf"
    rm -f "$S/nm_off"
    ls -l "$S" | grep -E 'newmenu|nm_' || true
    sync
    umount "$S"

    mount_usr || { echo USR_MOUNT_FAIL; exit 1; }
    install -m 0755 "$STUB" "$M/tool/nm_boot.so"
    rm -f "$M/tool/newmenu.so"          # the payload no longer lives in /usr
    set_preload "/usr/tool/nm_boot.so:/usr/tool/libefence.so"
    sync
    md5sum "$M/tool/nm_boot.so" "$STUB"
    umount "$M"
    echo "two-stage loader installed (payload: /setting/newmenu.so)"
    ;;

off)
    mount_usr || { echo USR_MOUNT_FAIL; exit 1; }
    [ -f "$OVERLAY" ] && install -m 0755 "$OVERLAY" "$M/tool/newmenu.so"
    set_preload "/usr/tool/newmenu.so:/usr/tool/libefence.so"
    rm -f "$M/tool/nm_boot.so"
    sync
    umount "$M"
    echo "restored single-stage install"
    ;;

*)
    echo "usage: $0 [on|off|status]"
    exit 1
    ;;
esac
