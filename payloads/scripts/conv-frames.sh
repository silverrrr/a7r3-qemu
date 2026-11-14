#!/bin/bash
# Convert selected frames from a proof run to PNG.
# Usage: conv-frames.sh <tag> <n1> [n2 ...]
set -u
TAG="$1"; shift
cd /mnt/f/project/a7r3/analysis || exit 1
for n in "$@"; do
  f="${TAG}_frames/frame_${n}.raw"
  if [ ! -f "$f" ]; then
    echo "missing $f"
    continue
  fi
  if python3 qemu_a7r3/raw2png.py "$f" "${TAG}_${n}.png" >/dev/null 2>&1; then
    echo "ok $n -> ${TAG}_${n}.png"
  else
    echo "convert failed $n"
  fi
done
ls -la "${TAG}"_*.png 2>/dev/null
