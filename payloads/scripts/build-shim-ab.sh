#!/bin/bash
# Build both shim variants for an A/B comparison of the texture sampler:
#   gpu_shim_qemu_old.so  : per-pixel double division (baseline)
#   gpu_shim_qemu_gotkey.so : 16.16 fixed-point stepping (optimised)
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
COMMON="--target=arm-linux-gnueabi -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables"
clang $COMMON -DUI_GEMINI_OLD_SAMPLING -o gpu_shim_qemu_old.so gpu_shim_qemu.c
clang $COMMON -o gpu_shim_qemu_gotkey.so gpu_shim_qemu.c
ls -la gpu_shim_qemu_old.so gpu_shim_qemu_gotkey.so
