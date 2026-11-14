#!/bin/bash
# Install the M0 menu probe into the eMMC image and put it FIRST in LD_PRELOAD
# (so its versioned dlopen interposer wins; it forwards to the shim's).
# Usage: install-probe-menu.sh [preload-spec]
#   default preload-spec = /usr/tool/probe_menu.so:/usr/tool/libefence.so
set -u
SPEC="${1:-/usr/tool/probe_menu.so:/usr/tool/libefence.so}"
SRC=/mnt/f/project/a7r3/analysis/payloads/probe_menu.so
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 1531904 * 512))
M=/tmp/img15rw

[ -f "$SRC" ] || { echo "missing $SRC (run build-probe-menu.sh)"; exit 1; }
[ ${#SPEC} -lt 500 ] || { echo "preload spec too long"; exit 1; }

mkdir -p "$M"
umount "$M" 2>/dev/null || true
mount -o loop,rw,offset=$OFF,sizelimit=314572800 "$IMG" "$M" || { echo MOUNT_FAIL; exit 1; }

install -m 0755 "$SRC" "$M/tool/probe_menu.so"

# /bin/iperf holds the preload path that patched bootin reads.
[ -f "$M/bin/iperf.orig" ] || cp -f "$M/bin/iperf" "$M/bin/iperf.orig"
python3 - "$M/bin/iperf" "$SPEC" <<'PY'
import sys

path, spec = sys.argv[1], sys.argv[2]
data = bytearray(open(path, 'rb').read())
if len(data) < 512:
    data = data + bytes(512 - len(data))
data[:512] = spec.encode() + bytes(512 - len(spec))
open(path, 'wb').write(bytes(data))
print('  preload -> %s' % spec)
PY

sync
ls -la "$M/tool/probe_menu.so" "$M/bin/iperf"
md5sum "$M/tool/probe_menu.so" "$SRC"
echo "--- /bin/iperf ---"
head -c 120 "$M/bin/iperf" | tr -d '\0'
echo
umount "$M"

# Clear /setting/probe_menu.log so each run starts with a fresh log.
S=/tmp/img2clean
mkdir -p "$S"
umount "$S" 2>/dev/null || true
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" && {
    rm -f "$S/probe_menu.log"
    echo "cleared /setting/probe_menu.log"
    umount "$S"
}
