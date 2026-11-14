#!/bin/bash
# Diagnostic build at -O1: at -O2 clang drops the histogram/print blocks.
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
clang --target=arm-linux-gnueabi -shared -fPIC -O1 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -o gpu_shim_qemu_diag.so gpu_shim_qemu.c
ls -la gpu_shim_qemu_diag.so
echo "markers: GEMTIME=$(grep -a -c GEMTIME gpu_shim_qemu_diag.so) OSAL_HIST=$(grep -a -c OSAL_HIST gpu_shim_qemu_diag.so) OSAL_CALLER=$(grep -a -c OSAL_CALLER gpu_shim_qemu_diag.so)"
