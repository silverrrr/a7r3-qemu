#!/bin/bash
# Build the new-menu overlay library (freestanding, soft-float ARM).
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -I. \
  -Wall -Wno-unused-parameter \
  -o newmenu.so menu_overlay.c menu_shell.c

echo "== newmenu.so $(stat -c %s newmenu.so) B"
file newmenu.so
echo "-- sections --"
size -A newmenu.so 2>/dev/null | grep -E 'text|rodata|data|bss' || true
echo "-- markers --"
for m in NM_ALIVE NM_OVERLAY NM_RENDER_FAIL NM_PANEL_FAIL; do
  printf '   %-14s %s\n' "$m" "$(grep -a -c "$m" newmenu.so)"
done
