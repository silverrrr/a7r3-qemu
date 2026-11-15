#!/usr/bin/env python3
"""Print the overlay log lines that belong to the most recent app start."""
import json
import urllib.parse
import urllib.request

url = "http://127.0.0.1:8765/sh?" + urllib.parse.urlencode(
    {"cmd": "cat /tmp/newmenu.log", "wait": 8})
with urllib.request.urlopen(url, timeout=120) as r:
    out = json.loads(r.read()).get("out", "")

lines = [ln for ln in out.splitlines() if ln.startswith("NM_")]
idx = 0
for i, ln in enumerate(lines):
    if ln.strip() == "NM_ALIVE":
        idx = i
recent = lines[idx:]
skip = ("NM_OVERLAY", "NM_FLIP", "NM_KEYREAD", "NM_REPRESENT")
print("lines since last NM_ALIVE: %d" % len(recent))
for ln in recent:
    if any(k in ln for k in skip):
        continue
    print(ln)
