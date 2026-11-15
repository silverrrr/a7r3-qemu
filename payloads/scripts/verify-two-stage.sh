#!/bin/bash
# Verify a running boot: guest exception count, then open the overlay through
# the viewer and print the enumeration evidence.
#   verify-two-stage.sh [port]
set -u
PORT="${1:-8765}"
DIR=/mnt/f/project/a7r3/analysis/live
QDIR=/mnt/f/project/a7r3/analysis/qemu_a7r3

n=$(grep -ac 'Exception Monitor' "$DIR/console.log" 2>/dev/null)
echo "exceptions: ${n:-0}"
echo "stub: $(grep -a 'NM_BOOT' "$DIR/console.log" 2>/dev/null | tail -2 | tr '\n' ' ')"
curl -s "http://127.0.0.1:$PORT/nmclick?x=596&y=12" >/dev/null
echo "clicked the corner switch"
sleep 4
cd "$QDIR" || exit 1
python3 nm-log2.py | grep -E 'NM_items|NM_refl|NM_elf_base|NM_getsr|NM_FAIL|NM_NO|NM_C_nchain' | head -12
python3 nm-log2.py | grep -E 'NM_ROW i=0x0|NM_VALUE' | head -4
