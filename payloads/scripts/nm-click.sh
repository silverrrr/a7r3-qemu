#!/bin/bash
# nm-click.sh X Y [SLEEP]  -- one overlay touch via the live viewer.
set -u
X="${1:?x}"; Y="${2:?y}"; S="${3:-1.5}"
curl -s "http://127.0.0.1:8765/nmclick?x=$X&y=$Y" || exit 1
echo
sleep "$S"
