#!/bin/bash
# Rebuild nflasha15 with:
#   - bootin preload paths -> /usr/bin/{top,iperf}
#   - /usr/bin/top, /usr/bin/iperf, /usr/tool/libefence.so replaced
#   - /usr/lib/libmpr.so lock calls NOPed
#   - /usr/bin/qsi.elf = copy of ulogio.elf (bootin starts qsi.elf; A7R3 lacks it,
#     and ulogio is the service that feeds the alog ring)
# Usage: repack-usr.sh [data-dir]
set -euo pipefail
D="${1:-/root/firmware/a7r3}"
HOOK="${HOOK_SO:-/mnt/f/project/a7r3/analysis/payloads/gpu_shim_qemu.so}"
STOCK="$D/service-dump/nflasha15.stock.bin"
OUT="$D/service-dump/nflasha15.bin"
SIZE=314572800
TREE=/root/a7r3-usr-tree

[ -f "$STOCK" ] || { echo "missing $STOCK" >&2; exit 1; }

rm -rf "$TREE"
mkdir -p "$TREE"
echo "== rdump $STOCK"
debugfs -R "rdump / $TREE" "$STOCK" >/dev/null 2>&1
echo "   files: $(find "$TREE" -type f | wc -l)"

echo "== patch trees"
python3 - "$TREE" "$HOOK" <<'PY'
import shutil
import os
import sys
from pathlib import Path

tree = Path(sys.argv[1])
hook = Path(sys.argv[2]).read_bytes()

def patch(rel, off, data):
    p = tree / rel.lstrip('/')
    b = bytearray(p.read_bytes())
    b[off:off + len(data)] = data
    p.write_bytes(bytes(b))
    print('  %-24s off=0x%x len=%d' % (rel, off, len(data)))

real = tree / 'bin' / 'bootin.real'
bootin = tree / 'bin' / 'bootin.elf'
if bootin.exists() and not real.exists():
    bootin.rename(real)
    real.chmod(0o755)
patch('/bin/bootin.real', 0x1e2b, b'/usr/bin/top' + bytes(7))
patch('/bin/bootin.real', 0x212e, b'/usr/bin/iperf' + bytes(7))
# Force the "sim" flag (bit 2) that bootin turns into " target=qemu " on the
# im.elf command line. 0x9730: tst.w r2,#4 -> movs r2,#1 ; nop (equal length).
patch('/bin/bootin.real', 0x1730, b'\x01\x22\x00\xbf')
patch('/bin/top', 0, b'3p' + bytes(510))
patch('/bin/iperf', 0, b'/usr/tool/libefence.so' + bytes(512 - 23))
patch('/tool/libefence.so', 0, hook)
wrapper = tree / 'bin' / 'bootin.elf'
wrapper.write_text(
    "#!/bin/sh\n"
    "ifconfig lo 127.0.0.1 up 2>/dev/null || ip link set lo up 2>/dev/null\n"
    "orig=\"$@\"\n"
    "for spec in 'uipc 251 207' 'backup 251 208' 'dmpgles2 248 29' "
    "'stream 249 7' 'stream2 249 8' 'ldec 251 205'; do\n"
    "  set -- $spec\n"
    "  [ -e /dev/$1 ] || mknod /dev/$1 c $2 $3\n"
    "done\n"
    "set -- $orig\n"
    "exec /usr/bin/bootin.real \"$@\"\n"
)
wrapper.chmod(0o755)
print('  %-24s wrapper' % '/bin/bootin.elf')
for name in ():  # keep liro.ko: LIRO display engine consumer
    p = tree / 'kmod' / name
    if p.exists():
        p.unlink()
        print('  %-24s removed' % ('/kmod/' + name))

# grm_gles.ko: bypass the Sugilite clock-ready check (cmp r0,#0x15 -> mov r0,#0).
# The initrd's module list asks for /usr/kmod/grm_gles.zz, so install the
# patched module under that name.
ko = tree / 'kmod' / 'grm_gles.ko'
kb = bytearray(ko.read_bytes())
assert kb[0x310:0x314] == b'\x15\x00\x50\xe3', kb[0x310:0x314].hex()
kb[0x310:0x314] = b'\x00\x00\xb0\xe3'          # movs r0,#0 (sets Z)
assert kb[0x314:0x318] == b'\x00\xa8\x9d\x08', kb[0x314:0x318].hex()
kb[0x314:0x318] = b'\x00\xa8\x9d\xe8'          # ldm sp,{fp,sp,pc} (uncond)
ko.write_bytes(bytes(kb))
shutil.copyfile(ko, tree / 'kmod' / 'grm_gles.zz')
print('  %-24s clock bypass -> grm_gles.zz' % '/kmod/grm_gles.ko')

# liro.ko: bypass the LIRO hardware version check so the module can load in
# QEMU (cmp r0,#4 ; bne -> mov r0,#4 ; nop).  The module is kept in the image.
liro = tree / 'kmod' / 'liro.ko'
if liro.exists():
    lb = bytearray(liro.read_bytes())
    if lb[0x5f8:0x5fc] == b'\x04\x00\x50\xe3':
        lb[0x5f8:0x5fc] = b'\x04\x00\xa0\xe3'   # mov r0,#4
        lb[0x5fc:0x600] = b'\x00\x00\xa0\xe1'   # nop
        liro.write_bytes(bytes(lb))
        print('  %-24s liro version bypass' % '/kmod/liro.ko')
    else:
        print('  %-24s liro pattern mismatch' % '/kmod/liro.ko')

# libIMDB: mask the USB/power/hotplug/Sensapi/MediaPwrMgr services that have no
# QEMU hardware.  Same qemu-profile trick as zve10/a7c2.
imdb = Path('/mnt/f/project/a7r3/analysis/extracted/nflasha15_x/libIMDB.patched.so').read_bytes()
p = tree / 'lib' / 'libIMDB.so'
old = p.read_bytes()
p.write_bytes(imdb)
print('  %-24s %d -> %d bytes' % ('/lib/libIMDB.so', len(old), len(imdb)))

# Experimental switch: the shim only calls InputService::activate when this
# marker exists, so the stable image can be restored by removing it.
# A7R3_KICKS=0 builds a clean baseline for tracing.
(tree / 'tool').mkdir(parents=True, exist_ok=True)
if os.environ.get('A7R3_KICKS', '1') == '1':
    (tree / 'tool' / 'ui_kick').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/ui_kick')
    (tree / 'tool' / 'uikey').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/uikey')
    (tree / 'tool' / 'vukick').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/vukick')
    (tree / 'tool' / 'view_kick').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/view_kick')
    (tree / 'tool' / 'root_kick').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/root_kick')
    (tree / 'tool' / 'config_kick').write_bytes(b'1')
    print('  %-24s enabled' % '/tool/config_kick')
PY

echo "== patch libmpr (NOP flash-log lock)"
python3 - "$TREE/lib/libmpr.so" <<'PY'
import struct
import sys

p = sys.argv[1]
b = bytearray(open(p, 'rb').read())
SITES = [(0x3340f2, 0x70ad9c), (0x334148, 0x70af3c), (0x33416c, 0x70ad9c),
         (0x334188, 0x70af3c), (0x3341c0, 0x70af3c)]
NOP = b'\x00\xbf\x00\xbf'


def bl_target(off):
    hw1, = struct.unpack_from('<H', b, off)
    hw2, = struct.unpack_from('<H', b, off + 2)
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0xD000:
        return None
    S = (hw1 >> 10) & 1
    J1 = (hw2 >> 13) & 1
    J2 = (hw2 >> 11) & 1
    I1 = 1 - (J1 ^ S)
    I2 = 1 - (J2 ^ S)
    imm = (S << 24) | (I1 << 23) | (I2 << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
    if imm & (1 << 25):
        imm -= 1 << 26
    return (off + 4 + imm) & 0xFFFFFFFF


for off, expect in SITES:
    got = bl_target(off)
    if got != expect:
        print('  skip 0x%x (bl -> %r)' % (off, got))
        continue
    b[off:off + 4] = NOP
    print('  nop  0x%x' % off)
open(p, 'wb').write(bytes(b))
PY

echo "== add qsi.elf (copy of ulogio.elf)"
if [ -f "$TREE/bin/ulogio.elf" ]; then
    cp -f "$TREE/bin/ulogio.elf" "$TREE/bin/qsi.elf"
    chmod 755 "$TREE/bin/qsi.elf"
    echo "   qsi.elf $(stat -c %s "$TREE/bin/qsi.elf") B"
else
    echo "   WARNING: ulogio.elf not found" >&2
fi

echo "== add guest key injector (/usr/tool/inject-key)"
IK=/mnt/f/project/a7r3/analysis/payloads/inject-key
if [ -f "$IK" ]; then
    mkdir -p "$TREE/tool"
    install -m 0755 "$IK" "$TREE/tool/inject-key"
    echo "   /tool/inject-key $(stat -c %s "$TREE/tool/inject-key") B"
else
    echo "   WARNING: $IK not built" >&2
fi

echo "== nflasha10 (/tmp): use the real dump with /images fonts"
P10="$D/service-dump/nflasha10.bin"
if [ -f "$D/service-dump/nflasha10.real.bin" ]; then
    cp -f "$D/service-dump/nflasha10.real.bin" "$P10"
    echo "   $P10 <- real dump $(stat -c %s "$P10") B"
else
    rm -f "$P10"
    truncate -s 12582912 "$P10"
    mkfs.vfat -F 16 -n CAMERA_ETC "$P10" >/dev/null
    echo "   WARNING: real nflasha10 not found, using empty vfat" >&2
fi

if [ "${GEMDUMP:-0}" = "1" ]; then
    mkdir -p "$TREE/tool"
    : > "$TREE/tool/gem_dump"
    echo "   gem_dump flag installed"
fi

if [ "${NOKEYINJ:-0}" = "1" ]; then
    mkdir -p "$TREE/tool"
    : > "$TREE/tool/no_keyinj"
    echo "   no_keyinj control flag installed"
fi

if [ "${VIEWPROBE:-0}" = "1" ]; then
    mkdir -p "$TREE/tool"
    : > "$TREE/tool/viewprobe"
    echo "   viewprobe flag installed"
fi

echo "== mke2fs -d"
rm -f "$OUT"
truncate -s $SIZE "$OUT"
mke2fs -q -t ext2 -b 1024 -d "$TREE" -F "$OUT"
echo "   $OUT $(stat -c %s "$OUT") B"


bash /mnt/f/project/a7r3/analysis/qemu_a7r3/build-a7r3-emmc.sh "$D"
