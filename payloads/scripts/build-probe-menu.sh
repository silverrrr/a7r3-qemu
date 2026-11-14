#!/bin/bash
# Build the M0 menu probe (A7R3, soft-float ARM, freestanding like the shim).
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -Wall -Wextra -Wno-unused-parameter \
  -Wl,--version-script=probe_menu.map \
  -o probe_menu.so probe_menu.c

echo "== probe_menu.so $(stat -c %s probe_menu.so) B"
file probe_menu.so
echo "-- dynamic symbols --"
readelf -sW --dyn-syms probe_menu.so | grep -E 'dlopen|UND|FUNC' | head -20
echo "-- NEEDED --"
readelf -d probe_menu.so | grep -E 'NEEDED|SONAME' || echo '   (none)'
echo "-- markers --"
for m in PM_ALIVE PM_ARM PM_SYM PM_ROOT PM_N PM_P; do
  printf '   %-9s %s\n' "$m" "$(grep -a -c "$m" probe_menu.so)"
done
