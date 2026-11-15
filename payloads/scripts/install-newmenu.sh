#!/bin/bash
# Install the new-menu overlay and make it preload before the shim.
# Usage: install-newmenu.sh [on|off]   (default on: also creates the gate file)
set -u
MODE="${1:-on}"
SRC=/mnt/f/project/a7r3/analysis/payloads/newmenu.so
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
USR_OFF=$((524288 + 1531904 * 512))
SET_OFF=$((524288 + 18432 * 512))
M=/tmp/img15nm
S=/tmp/img2nm

[ -f "$SRC" ] || { echo "missing $SRC (run build-newmenu.sh)"; exit 1; }

mkdir -p "$M"
umount "$M" 2>/dev/null || true
mount -o loop,rw,offset=$USR_OFF,sizelimit=314572800 "$IMG" "$M" || { echo MOUNT_FAIL; exit 1; }
install -m 0755 "$SRC" "$M/tool/newmenu.so"
python3 - "$M/bin/iperf" "/usr/tool/newmenu.so:/usr/tool/libefence.so" <<'PY'
import sys
path, spec = sys.argv[1], sys.argv[2]
data = bytearray(open(path, 'rb').read())
if len(data) < 512:
    data += bytes(512 - len(data))
data[:512] = spec.encode() + bytes(512 - len(spec))
open(path, 'wb').write(bytes(data))
print("  preload -> %s" % spec)
PY
sync
md5sum "$M/tool/newmenu.so" "$SRC"
umount "$M"

mkdir -p "$S"
umount "$S" 2>/dev/null || true
mount -o loop,rw,offset=$SET_OFF,sizelimit=20971520 "$IMG" "$S" || { echo SET_MOUNT_FAIL; exit 1; }
rm -f "$S/newmenu.log"
if [ "$MODE" = "on" ]; then
    : > "$S/newmenu"
    # corner switch button over the native screen (verified not to disturb the
    # app's own drawing, see A7R3_NOTES 64); remove the file to turn it off
    : > "$S/nm_badge"
    echo "created /setting/newmenu and /setting/nm_badge"
else
    rm -f "$S/newmenu"
    echo "removed /setting/newmenu"
fi
sync
umount "$S"
