#!/bin/bash
# Create or remove /setting/probe_hook (enables the M0 probe's code hook).
# Usage: set-probe-hook.sh on|off
set -u
MODE="${1:-on}"
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/img2rw
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 "$IMG" "$M" || {
  echo MOUNT_FAIL; exit 1; }
if [ "$MODE" = "on" ]; then
  : > "$M/probe_hook"
  echo "created /setting/probe_hook"
else
  rm -f "$M/probe_hook"
  echo "removed /setting/probe_hook"
fi
sync
ls -la "$M" | head -12
umount "$M"
