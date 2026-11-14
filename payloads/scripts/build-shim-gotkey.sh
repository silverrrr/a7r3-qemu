#!/bin/bash
# Build the shim with the GOTKEY receive-proof hook.
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
OUT=/mnt/f/project/a7r3/analysis/payloads/gpu_shim_qemu_gotkey.so
clang --target=arm-linux-gnueabi -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -o "$OUT" gpu_shim_qemu.c
ls -la "$OUT"
file "$OUT"
