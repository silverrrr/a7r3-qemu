#!/bin/bash
# Convert selected frames of a run dir to PNG: conv-list.sh <dir> <pngprefix> <n...>
set -u
DIR="${1:?dir}"; PRE="${2:?prefix}"; shift 2
cd /mnt/f/project/a7r3/analysis || exit 1
for n in "$@"; do
    f="$DIR/frame_${n}.raw"
    [ -f "$f" ] || { echo "missing $f"; continue; }
    python3 qemu_a7r3/raw2png.py "$f" "${PRE}_${n}.png" >/dev/null 2>&1 \
        && echo "ok $n -> ${PRE}_${n}.png" || echo "convert failed $n"
done
