#!/bin/bash
# Show the guest exception context from a captured serial log.
#   show-console-fault.sh /tmp/console.log [lines-before]
set -u
LOG="${1:-/tmp/console.log}"
BEFORE="${2:-40}"

if [ ! -f "$LOG" ]; then
    echo "no such log: $LOG"
    exit 1
fi
n=$(grep -a -n 'exception monitor' "$LOG" | head -1 | cut -d: -f1)
if [ -z "${n:-}" ]; then
    echo "no exception in $LOG"
    exit 0
fi
start=$((n - BEFORE))
[ "$start" -lt 1 ] && start=1
sed -n "${start},$((n + 2))p" "$LOG" | tr -d '\r'
