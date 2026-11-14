#!/bin/bash
# Disassemble a virtual-address window of an A7R3 /usr library.
# Usage: dis-lib.sh <lib> <vaddr-hex> [bytes] [thumb|arm]
set -u
LIB="${1:?lib path inside /usr, e.g. lib/libObj.so}"
ADDR="${2:?vaddr in hex, e.g. 0x719400}"
LEN="${3:-0x200}"
MODE="${4:-thumb}"
OD=/usr/lib/llvm-21/bin/llvm-objdump
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
M=/tmp/img15dis
mkdir -p "$M"
umount "$M" 2>/dev/null
mount -o loop,ro,offset=$((524288 + 1531904 * 512)),sizelimit=314572800 "$IMG" "$M" || exit 1
if [ "$MODE" = "thumb" ]; then
  TRIPLE=thumbv7-linux-gnueabi
else
  TRIPLE=armv7-linux-gnueabi
fi
"$OD" -d --triple="$TRIPLE" \
  --start-address="$ADDR" --stop-address=$((ADDR + LEN)) \
  "$M/$LIB" 2>&1 | tail -70
umount "$M"
