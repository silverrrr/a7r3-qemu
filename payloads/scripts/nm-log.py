#!/usr/bin/env python3
"""Print the guest /tmp/newmenu.log with the noisy periodic lines removed."""
import json
import sys
import urllib.parse
import urllib.request

cmd = sys.argv[1] if len(sys.argv) > 1 else "cat /tmp/newmenu.log"
pat = sys.argv[2] if len(sys.argv) > 2 else ""
url = "http://127.0.0.1:8765/sh?" + urllib.parse.urlencode({"cmd": cmd})
with urllib.request.urlopen(url, timeout=180) as r:
    data = json.loads(r.read())
out = data.get("out", "")
lines = [ln for ln in out.splitlines()
         if ln.startswith("NM_") and not ln.startswith("NM_OVERLAY")
         and "NM_FLIP" != ln.strip()]
if pat:
    lines = [ln for ln in lines if pat in ln]
print("\n".join(lines[-40:]))
