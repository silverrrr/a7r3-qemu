#!/bin/bash
# Build the fail-closed install guard (static ARM32, raw syscalls).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
cd /mnt/f/project/a7r3/analysis/payloads

clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -static -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -O2 -fuse-ld=lld -I. \
  -Wall -Wextra -Wno-unused-parameter \
  -o guard.elf guard.c

echo "== guard.elf $(stat -c %s guard.elf) B"
file guard.elf 2>/dev/null || true
python3 "$HERE/patch-dt-relcount.py" guard.elf >/dev/null 2>&1 || true
