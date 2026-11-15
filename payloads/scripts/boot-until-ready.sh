#!/bin/bash
# Start the QEMU viewer, retrying the known ~1/3 boot race.
#
#   boot-until-ready.sh [attempts] [port]
#
# Leaves the viewer running on success (exit 0) and prints the URL.
set -u
ATTEMPTS="${1:-4}"
PORT="${2:-8765}"
QDIR=/mnt/f/project/a7r3/analysis/qemu_a7r3
LOG=/mnt/f/project/a7r3/analysis/live/boot_retry.log

cd "$QDIR" || exit 1
mkdir -p /mnt/f/project/a7r3/analysis/live
: > "$LOG"

for i in $(seq 1 "$ATTEMPTS"); do
    echo "== attempt $i/$ATTEMPTS" | tee -a "$LOG"
    pkill -f 'qemu-system-arm -M cxd90045' 2>/dev/null
    sleep 2
    env A7R3_SURF_TICK=300 \
        A7R3_DUMP_PA=0x202e0200 \
        A7R3_DUMP_PA2=0x23a80000 \
        A7R3_DUMP_PA3=0x23b16000 \
        nohup python3 live-menu.py --port "$PORT" --duration 3600 \
        </dev/null >>"$LOG" 2>&1 &
    pid=$!
    ok=0
    for t in $(seq 1 120); do
        sleep 1
        code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/" 2>/dev/null)
        if [ "$code" = "200" ]; then
            sleep 40          # let the app reach its menu
            code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/")
            if [ "$code" = "200" ]; then
                ok=1
                break
            fi
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
    done
    if [ "$ok" = "1" ]; then
        echo "== viewer ready on http://localhost:$PORT/ (attempt $i)" | tee -a "$LOG"
        exit 0
    fi
    echo "== attempt $i failed, retrying" | tee -a "$LOG"
    kill "$pid" 2>/dev/null
    pkill -f 'qemu-system-arm -M cxd90045' 2>/dev/null
    sleep 3
done

echo "== all attempts failed"
exit 1
