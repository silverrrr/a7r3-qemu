#!/bin/bash
# Build the "lite" payload: loads, checks the arming gate, starts nothing.
# Used to tell "dlopen of the payload crashes the app" apart from "what the
# payload does after loading crashes the app".
set -eu
cd /mnt/f/project/a7r3/analysis/payloads
LIBS=/mnt/f/project/a7r3/analysis/libs
EXTRA=""
if [ -f "$LIBS/libc-2.11.2.so" ]; then
    EXTRA="-L$LIBS -Wl,--no-as-needed -l:libc-2.11.2.so"
fi
clang --target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft \
  -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -DNM_LITE \
  -I. -Wall -Wno-unused-parameter \
  $EXTRA \
  -o newmenu_lite.so menu_overlay.c menu_shell.c
echo "== newmenu_lite.so $(stat -c %s newmenu_lite.so) B"
