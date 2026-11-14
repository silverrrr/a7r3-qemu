#!/bin/bash
# Boot the A7R3 firmware with the M0 menu probe preloaded and collect PM_ lines.
# Retries the known boot race ("Entering exception monitor").
# Usage: run-probe-menu.sh [attempts] [seconds]
set -u
N="${1:-3}"
SECS="${2:-200}"
D=/root/firmware/a7r3
QEMU=/root/qemu-ma1co/build-arm/arm-softmmu/qemu-system-arm
OUT=/mnt/f/project/a7r3/analysis
LOG=$OUT/a7r3-probe-menu.log
cd "$D" || exit 1

for i in $(seq 1 "$N"); do
  echo "=== attempt $i $(date +%H:%M:%S) ==="
  rm -f "$LOG"
  # start each attempt with an empty guest-side log
  S=/tmp/img2clean
  mkdir -p "$S"
  umount "$S" 2>/dev/null || true
  mount -o loop,rw,offset=$((524288 + 18432 * 512)),sizelimit=20971520 \
        /root/firmware/a7r3/qemu-emmc-2g.img "$S" 2>/dev/null && {
    rm -f "$S/probe_menu.log"
    umount "$S"
  }
  rm -rf /tmp/probe-frames
  A7R3_LIRO_AUTOREADY=1 \
  A7R3_SUGI_EXEC=1 \
  A7R3_SUGI_CENTERSCALE=1 \
  A7R3_SURF_MORTON=1 \
  A7R3_NO_UNIT_DUMP=1 \
  A7R3_KICK_DUMP=1 \
  A7R3_BLIT_DUMP=1 \
  A7R3_SURF_DIR=/tmp/probe-frames \
  timeout -k 10 "$SECS" "$QEMU" -M cxd90045 -smp 2 \
    -kernel vmlinux-console.bin -initrd initrd-qemu.img \
    -drive if=mtd,file=qemu-emmc-2g.img,format=raw \
    -device loader,file=av-cam.bin,addr=0x035c6000,force-raw=on \
    -serial file:"$LOG" -display none -monitor none 2>/tmp/probe-menu-qemu.err

  fault=$(grep -c 'Entering exception monitor' "$LOG" 2>/dev/null || true)
  alive=$(grep -c 'PM_ALIVE' "$LOG" 2>/dev/null || true)
  arm=$(grep -c 'PM_ARM caution=0x[1-9a-f]' "$LOG" 2>/dev/null || true)
  ready=$(grep -c 'PM_TREE_READY' "$LOG" 2>/dev/null || true)
  begun=$(grep -c 'PM_DUMP_BEGIN' "$LOG" 2>/dev/null || true)
  echo "attempt $i: fault=${fault:-?} alive=${alive:-0} armed=${arm:-0} ready=${ready:-0} dump=${begun:-0}"
  cp "$LOG" "$OUT/a7r3-probe-menu-$i.log" 2>/dev/null
  # The guest also writes the log to /setting (vfat), which survives shutdown.
  bash "$OUT/qemu_a7r3/read-guest-file.sh" probe_menu.log \
       "$OUT/a7r3-probe-menu-$i.txt" >/dev/null 2>&1 || true
  if [ -s "$OUT/a7r3-probe-menu-$i.txt" ]; then
    echo "--- PM_ output (clean copy, $(wc -l < "$OUT/a7r3-probe-menu-$i.txt") lines) ---"
    grep -a 'PM_' "$OUT/a7r3-probe-menu-$i.txt" | head -80
    echo "--- end (clean: $OUT/a7r3-probe-menu-$i.txt) ---"
  fi
  if [ "${fault:-1}" -eq 0 ] && [ "${arm:-0}" -ge 1 ]; then
    exit 0
  fi
done
echo NO_GOOD_PROBE_RUN
exit 1
