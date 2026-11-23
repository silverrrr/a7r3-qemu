#!/bin/bash
# Run one command through the live viewer's guest shell and print its output.
#   run-guest-cmd.sh "command" [wait-seconds] [port]
set -u
CMD="${1:?usage: run-guest-cmd.sh \"command\" [wait] [port]}"
WAIT="${2:-20}"
PORT="${3:-8765}"
curl -sG "http://127.0.0.1:${PORT}/sh" \
     --data-urlencode "cmd=${CMD}" \
     --data-urlencode "wait=${WAIT}" \
 | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("out",""))'
