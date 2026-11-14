#!/bin/bash
# Print the partition table of the eMMC image using sfdisk on an offset loop.
set -u
IMG=/root/firmware/a7r3/qemu-emmc-2g.img
L=$(losetup -f --show -o 524288 "$IMG") || exit 1
sfdisk -d "$L"
losetup -d "$L"
