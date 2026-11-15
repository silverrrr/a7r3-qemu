#!/bin/bash
# Build nm_boot.so -- the tiny preload stub that loads the overlay from a
# configurable path (SD card / /setting) and stays inert when it is missing.
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -I. \
  -Wall -Wno-unused-parameter \
  -o nm_boot.so nm_boot.c

echo "== nm_boot.so $(stat -c %s nm_boot.so) B"
file nm_boot.so
size -A nm_boot.so 2>/dev/null | grep -E 'text|rodata|data|bss' || true
