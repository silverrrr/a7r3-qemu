#!/bin/bash
# Print the current guest-side overlay/shim state (usr + setting partitions).
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
U=/tmp/img15state
S=/tmp/img2state
mkdir -p "$U" "$S"
umount "$U" 2>/dev/null; umount "$S" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 1531904 * 512)),sizelimit=314572800 "$IMG" "$U" || exit 1
mount -o loop,ro,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$S" || exit 1
echo "== /usr/tool"
ls -la "$U/tool" | grep -E 'newmenu|libefence|gpuscan|produce|open_view|log_skip' || true
echo "== libefence md5"
md5sum "$U/tool/libefence.so"
echo "== preload spec (/usr/bin/iperf)"
head -c 64 "$U/bin/iperf" | tr -d '\0'; echo
echo "== /setting gates"
ls -la "$S" | grep -E 'newmenu|nm_flip|nm_present|probe_hook|nm_key' || true
umount "$U"; umount "$S"
