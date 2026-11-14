#!/bin/bash
# Convert the newest flip/render frame of a run to PNG.
# Usage: conv-last.sh <tag>
set -u
TAG="${1:-nmf1}"
cd /mnt/f/project/a7r3/analysis || exit 1
for pat in 'frame_7*.raw' 'frame_8*.raw'; do
    last=$(ls "$TAG-cmds"/$pat 2>/dev/null | sed 's/.*frame_//;s/\.raw//' | sort -n | tail -1)
    [ -n "$last" ] || continue
    python3 qemu_a7r3/raw2png.py "$TAG-cmds/frame_$last.raw" "${TAG}_$last.png" >/dev/null 2>&1 \
        && echo "ok ${TAG}_$last.png" || echo "convert failed $last"
done
