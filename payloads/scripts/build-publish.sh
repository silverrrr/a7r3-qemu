#!/bin/bash
# Build the freestanding A7R3 publisher (static ARM32, raw syscalls).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
cd /mnt/f/project/a7r3/analysis/payloads

clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -static -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -O2 -fuse-ld=lld \
  -Wall -Wextra -Wno-unused-parameter \
  -o publish.elf publish.c

echo "== publish.elf $(stat -c %s publish.elf) B"
file publish.elf 2>/dev/null || true
readelf -h publish.elf | grep -E 'Type|Entry|Machine'
readelf -l publish.elf | grep -E 'INTERP|LOAD|GNU_STACK' || true
python3 "$HERE/patch-dt-relcount.py" publish.elf || true
