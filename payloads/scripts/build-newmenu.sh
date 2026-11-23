#!/bin/bash
# Build the new-menu overlay library (freestanding, soft-float ARM).
#
# If the firmware's libc has been extracted to analysis/libs/ (see
# extract-fw-libs.sh), link against it so the .so gets
#     NEEDED libc.so.6
# That entry is what lets the loader resolve dlsym/dlopen/pthread_create when
# the library is dlopen()ed at run time (two-stage loader, A7R3_NOTES 66).
# Without it the library still builds and works as a plain LD_PRELOAD.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
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
  -I. \
  -Wall -Wno-unused-parameter \
  -Wl,-z,now \
  $EXTRA \
  -o newmenu.so menu_overlay.c menu_shell.c

# ld-2.11.2 on this camera mis-reads DT_RELCOUNT when the relocation tables are
# not contiguous under DT_BIND_NOW, which makes the first PLT call jump to 0
# (root cause of the two-stage pc=0 crash; see a7r3-boot-runtime).  Their
# invariant: custom DSOs ship DT_RELCOUNT = 0 and never RWX.
python3 "$HERE/patch-dt-relcount.py" newmenu.so

echo "== newmenu.so $(stat -c %s newmenu.so) B"
file newmenu.so
echo "-- dependencies --"
readelf -d newmenu.so | grep -E 'NEEDED|SONAME' || echo "   (none)"
echo "-- sections --"
size -A newmenu.so 2>/dev/null | grep -E 'text|rodata|data|bss' || true
echo "-- markers --"
for m in NM_ALIVE NM_OVERLAY NM_RENDER_FAIL NM_PANEL_FAIL; do
  printf '   %-14s %s\n' "$m" "$(grep -a -c "$m" newmenu.so)"
done
