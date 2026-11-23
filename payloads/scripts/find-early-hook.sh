#!/bin/bash
# Does A7R3 have an early shell hook (/etc/profile -> /tmp/profile)?
# Read-only: mounts the dumps and greps for profile references.
set -u
D=/mnt/f/project/a7r3/dumps
E=/mnt/f/project/a7r3/analysis/tmp-etc-ro
M=/mnt/f/project/a7r3/analysis/tmp-misc-ro

mkdir -p "$E" "$M"
umount "$E" 2>/dev/null; umount "$M" 2>/dev/null
mount -o loop,ro "$D/nflasha10.bin" "$E" || { echo ETC_MOUNT_FAIL; exit 1; }

echo "== /etc-style partition (nflasha10) top level"
ls -la "$E" | sed -n '1,25p'
echo
echo "== files named profile / rc / init in it"
find "$E" -maxdepth 2 \( -name '*profile*' -o -name 'rc*' -o -name 'init*' \) 2>/dev/null | head -20

echo
echo "== any file mentioning /tmp/profile"
grep -rla '/tmp/profile' "$E" 2>/dev/null | head

echo
echo "== grep /tmp/profile in the binaries"
for f in nflasha7 nflasha15 nflasha5 nflasha1; do
    p="$D/$f.bin"
    [ -f "$p" ] || continue
    n=$(grep -a -c '/tmp/profile' "$p" 2>/dev/null || true)
    printf '%-11s /tmp/profile hits: %s\n' "$f" "${n:-0}"
done

echo
echo "== profile strings inside im.elf / bootin.elf (via /usr mount)"
mount -o loop,ro "$D/nflasha15.bin" "$M" 2>/dev/null
for b in "$M/bin/im.elf" "$M/bin/bootin.elf"; do
    [ -f "$b" ] || continue
    echo "-- $(basename "$b")"
    strings -a "$b" | grep -E 'profile|/etc/' | sort -u | head -12
done
umount "$M" 2>/dev/null

umount "$E"
