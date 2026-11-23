#!/bin/sh
# In-guest publish probe: hardlink retention + remount-ro verification.
#
# Reproduces the a7r3-boot-runtime invariant on the real ext2 /usr:
#   remount rw -> verify -> linkat(old, retained) -> rename(new, target)
#   -> fsync -> remount ro -> verify.
# Target is /usr/lib/libIMDB.so, which im.elf has mapped: without the hardlink
# retention the old inode would have nlink==0 and remount-ro would fail EBUSY.
#
# Read-only for the file's content: the replacement is a byte-identical copy.
set -u
T=/usr/lib/libIMDB.so
R=$T.retained
N=/tmp/libIMDB.publish

echo "== mounts before"
grep ' /usr ' /proc/mounts

mount -o remount,rw /usr || { echo "REMOUNT_RW_FAIL"; exit 1; }
echo "== rw ok"
grep ' /usr ' /proc/mounts
ls -l "$T"

cp "$T" "$N" || { echo "CP_FAIL"; mount -o remount,ro /usr; exit 1; }
cmp "$T" "$N" && echo "COPY_MATCHES"

ln "$T" "$R" || { echo "LINK_FAIL"; mount -o remount,ro /usr; exit 1; }
echo "== after link (old inode pinned, nlink=2)"
ls -l "$T" "$R"

mv "$N" "$T" || { echo "RENAME_FAIL"; mount -o remount,ro /usr; exit 1; }
sync
echo "== after rename (target=new inode, retained=old inode)"
ls -l "$T" "$R"
cmp "$R" "$T" && echo "INODE_CONTENT_SAME"

if mount -o remount,ro /usr; then
    echo "REMOUNT_RO_OK"
else
    echo "REMOUNT_RO_FAIL"
fi
echo "== mounts after"
grep ' /usr ' /proc/mounts
echo "PUBLISH_PROBE_DONE"
