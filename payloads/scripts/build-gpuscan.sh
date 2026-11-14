#!/bin/bash
# Build, install and arm the live GPU-descriptor scanner (shim GSCAN mode).
#   build-gpuscan.sh [on|off]
# on  : patch source (once), build gpu_shim_qemu_gpuscan.so, install as
#       /usr/tool/libefence.so, create /usr/tool/gpuscan (enables scanning)
# off : reinstall the baseline gotkey shim and drop the marker
set -u
MODE="${1:-on}"
P=/mnt/f/project/a7r3/analysis/payloads
Q=/mnt/f/project/a7r3/analysis/qemu_a7r3
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
OFF=$((524288 + 1531904 * 512))
M=/tmp/img15gscan

mkdir -p "$M"
umount "$M" 2>/dev/null

if [ "$MODE" = "on" ]; then
  python3 "$Q/patch-shim-gpuscan.py" || exit 1
  cd "$P" || exit 1
  clang --target=arm-linux-gnueabi -shared -fPIC -O2 -fuse-ld=lld \
    -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -o gpu_shim_qemu_gpuscan.so gpu_shim_qemu.c || exit 1
  ls -la gpu_shim_qemu_gpuscan.so
  mount -o loop,rw,offset="$OFF",sizelimit=314572800 "$IMG" "$M" || { echo MOUNT_FAIL; exit 1; }
  install -m 0755 "$P/gpu_shim_qemu_gpuscan.so" "$M/tool/libefence.so"
  : > "$M/tool/gpuscan"
  sync
  md5sum "$M/tool/libefence.so" "$P/gpu_shim_qemu_gpuscan.so"
  ls -la "$M/tool/gpuscan" "$M/tool/libefence.so"
  umount "$M"
  echo "GSCAN armed"
else
  mount -o loop,rw,offset="$OFF",sizelimit=314572800 "$IMG" "$M" || { echo MOUNT_FAIL; exit 1; }
  install -m 0755 "$P/gpu_shim_qemu_gotkey.so" "$M/tool/libefence.so"
  rm -f "$M/tool/gpuscan"
  sync
  md5sum "$M/tool/libefence.so"
  umount "$M"
  echo "baseline shim restored"
fi
