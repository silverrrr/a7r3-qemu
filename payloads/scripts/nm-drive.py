#!/usr/bin/env python3
"""Drive the live viewer from the command line (no PowerShell quoting pain).

  nm-drive.py log [N]          print the tail of /tmp/newmenu.log
  nm-drive.py snap [name]      ask the viewer for a snapshot + pull latest.png
  nm-drive.py click X Y        synthetic touch at panel coords
  nm-drive.py key CH           overlay key (u d e p c n b l t s x o)
  nm-drive.py nkey NAME        native key through inject-key (MENU, ENTER, ...)
  nm-drive.py touch X Y        publisher-side touch emulation (native UI)
  nm-drive.py sh "cmd"         run a shell command in the guest

The viewer must be running on http://127.0.0.1:8765 (see see-menu.sh).
"""
import json
import os
import shutil
import sys
import time
import urllib.parse
import urllib.request

BASE = "http://127.0.0.1:8765"
LIVE = "/mnt/f/project/a7r3/analysis/live"
SHOTS = "/mnt/f/project/a7r3/analysis/shots"


def get(path, **q):
    url = BASE + path
    if q:
        url += "?" + urllib.parse.urlencode(q)
    with urllib.request.urlopen(url, timeout=180) as r:
        body = r.read()
    try:
        return json.loads(body)
    except ValueError:
        return {"raw": body[:400].decode("latin-1", "replace")}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "log":
        n = int(argv[2]) if len(argv) > 2 else 60
        out = get("/sh", cmd="cat /tmp/newmenu.log")["out"]
        lines = out.splitlines()
        # drop the echoed command line + prompt noise
        lines = [ln for ln in lines if not ln.startswith("/ #")]
        print("\n".join(lines[-n:]))
        return 0
    if cmd == "snap":
        name = argv[2] if len(argv) > 2 else "shot"
        get("/snap")
        os.makedirs(SHOTS, exist_ok=True)
        dst = os.path.join(SHOTS, name + ".png")
        shutil.copyfile(os.path.join(LIVE, "latest.png"), dst)
        print(dst)
        return 0
    if cmd == "click":
        x, y = int(argv[2]), int(argv[3])
        print(json.dumps(get("/nmclick", x=x, y=y)))
        return 0
    if cmd == "key":
        print(json.dumps(get("/nmkey", c=argv[2])))
        return 0
    if cmd == "nkey":
        print(json.dumps(get("/key", c=argv[2])))
        return 0
    if cmd == "touch":
        x, y = int(argv[2]), int(argv[3])
        print(json.dumps(get("/touch", x=x, y=y)))
        return 0
    if cmd == "sh":
        print(get("/sh", cmd=argv[2], wait=8)["out"][-4000:])
        return 0
    if cmd == "shq":
        # run a command and return only what it printed (marker delimited), so
        # the guest's noisy console output does not drown the answer
        inner = argv[2]
        out = get("/sh", cmd="echo ZZBEGIN; %s; echo ZZEND" % inner, wait=8)["out"]
        if "ZZBEGIN" in out and "ZZEND" in out:
            out = out.split("ZZBEGIN", 1)[1].rsplit("ZZEND", 1)[0]
        print(out.strip())
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
