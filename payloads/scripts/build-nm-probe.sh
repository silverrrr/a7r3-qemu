#!/bin/bash
# Build the minimal dlopen() probe library (see nm_probe.c).
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin \
  -I. \
  -o nm_probe.so nm_probe.c
echo "== nm_probe.so $(stat -c %s nm_probe.so) B"
readelf -d nm_probe.so | grep -E 'NEEDED|INIT' || true
