#!/usr/bin/env python3
"""Interactive A7R3 menu session: live frame viewer + on-screen key buttons.

Boots QEMU with the Sugilite software renderer and the frame dumper, opens the
base menu, then:
  * converts every new frame dump to <dir>/latest.png (served over HTTP),
  * exposes /key?c=<m|e|u|d|l|r|h|f|c> so the web page can inject keys, and
  * also forwards keystrokes typed in the terminal.

View in a browser:  http://localhost:<port>/

usage: live-menu.py [--port 8765] [--dir DIR] [--duration SEC] [--script "d,d,s"]
"""
import argparse
import json
import os
import re
import select
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

QEMU = "/root/qemu-ma1co/build-arm/arm-softmmu/qemu-system-arm"
FW = "/root/firmware/a7r3"
SOCK = "/tmp/a7r3live.sock"
CONV = "/mnt/f/project/a7r3/analysis/qemu_a7r3/raw_to_png_morton.py"
QUEUE = "0x7a0020"
KEYS = {
    "m": ("MENU", 0x1AB),
    "e": ("ENTER", 0x1AC),
    "u": ("UP", 0x3),
    "d": ("DOWN", 0x30),
    "l": ("LEFT", 0x15),
    "r": ("RIGHT", 0x1AF),
    "h": ("HALF", 0x1F1),
    "f": ("FULL", 0xA7),
    "c": ("REC", 0x1F0),
}

INDEX = """<!doctype html>
<html><head><meta charset="utf-8"><title>A7R3 live</title>
<style>
body{background:#111;color:#ddd;font-family:monospace;margin:0;padding:12px}
img{border:1px solid #444;image-rendering:pixelated;width:640px;height:480px;background:#000}
pre{color:#8c8;margin:6px 0}
.pad{display:grid;grid-template-columns:repeat(4,120px);gap:8px;margin:10px 0;max-width:520px}
button{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:10px 6px;font:14px monospace;cursor:pointer}
button:hover{background:#333}
button:active{background:#4a4a22}
.small{color:#888;font-size:12px;max-width:660px}
</style></head><body>
<h3>A7R3 QEMU live view</h3>
<img id="f" src="latest.png" onclick="touchClick(event)">
<script>
function touchClick(ev){
  const img=document.getElementById('f');
  const r=img.getBoundingClientRect();
  const x=Math.round((ev.clientX-r.left)*640/r.width);
  const y=Math.round((ev.clientY-r.top)*480/r.height);
  const s=document.getElementById('s');
  s.textContent='click '+x+','+y+' ...';
  fetch('/nmclick?x='+x+'&y='+y).then(r=>r.json()).then(d=>{
    s.textContent=JSON.stringify(d);
    setTimeout(function(){ img.src='latest.png?t='+Date.now(); }, 1500);
  }).catch(e=>{ s.textContent='click failed: '+e; });
}
</script>
<pre id="s">waiting for frames...</pre>
<div class="pad">
  <button onclick="press('u')">&#9650; UP</button>
  <button onclick="press('m')">MENU</button>
  <button onclick="press('e')">ENTER</button>
  <button onclick="press('d')">&#9660; DOWN</button>
  <button onclick="press('l')">&#9664; LEFT</button>
  <button onclick="press('r')">&#9654; RIGHT</button>
  <button onclick="press('h')">半按</button>
  <button onclick="press('f')">全按</button>
  <button onclick="press('c')">REC</button>
  <button onclick="snap()">截图</button>
</div>
<div class="small">键盘也可用：方向键 / Enter / m；每个按键都会经串口调用 guest 的
/usr/tool/inject-key。软件渲染较慢，按键后请等几十秒再刷新观察。</div>
<h4>新菜单叠层（直接驱动 /setting/nm_key）</h4>
<div class="pad">
  <button onclick="pressnm('u')">&#9650; 选择上</button>
  <button onclick="pressnm('e')">应用 +</button>
  <button onclick="pressnm('p')">应用 −</button>
  <button onclick="pressnm('d')">&#9660; 选择下</button>
  <button onclick="pressnm('c')">切换分类</button>
  <button onclick="pressnm('n')">打开二级页</button>
  <button onclick="pressnm('b')">MENU 返回</button>
  <button onclick="pressnm('l')">列选项</button>
  <button onclick="pressnm('t')">合成触摸</button>
  <button onclick="toggleOverlay()">叠层 开/关</button>
  <button onclick="pressnm('s')">自测</button>
</div>
<script>
function toggleOverlay(){
  fetch('/nmtoggle').then(r=>r.json()).then(d=>{
    document.getElementById('s').textContent =
      'overlay ' + (d.closed ? 'closed (原生菜单)' : 're-opened');
  }).catch(e=>{});
}
function pressnm(k){
  fetch('/nmkey?c='+k).then(r=>r.json()).then(d=>{
    document.getElementById('s').textContent =
      (d.ok?'overlay key ':'FAIL ')+d.nmkey;
  }).catch(e=>{});
}
</script>
<script>
function press(k){
  fetch('/key?c='+k).then(r=>r.json()).then(d=>{
    document.getElementById('s').textContent =
      (d.ok?'sent ':'FAIL ')+d.label+' '+d.code;
  }).catch(e=>{});
}
function snap(){ fetch('/snap').then(r=>r.json()).then(d=>{
  document.getElementById('s').textContent = d.ok?('saved '+d.snapshot):'snapshot failed';
});}
setInterval(function(){
  var i=document.getElementById('f');
  i.src='latest.png?t='+Date.now();
},1000);
addEventListener('keydown', function(e){
  var map={ArrowUp:'u',ArrowDown:'d',ArrowLeft:'l',ArrowRight:'r',Enter:'e',m:'m',M:'m'};
  if(map[e.key]){ e.preventDefault(); press(map[e.key]); }
});
</script></body></html>
"""


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, session=None, **kw):
        self.session = session
        super().__init__(*args, **kw)

    def log_message(self, *a):
        pass

    def _json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/sh":
            q = parse_qs(u.query)
            cmd = (q.get("cmd") or [""])[0]
            if not cmd:
                self._json({"ok": False, "error": "no cmd"})
                return
            try:
                wait = float((q.get("wait") or ["6"])[0])
            except ValueError:
                wait = 6.0
            base = len(self.session.console or b"")
            ok = self.session.send("\n" + cmd + "\n")
            time.sleep(max(1.0, min(wait, 30.0)))
            out = bytes((self.session.console or b"")[base:]).decode(
                "latin-1", "replace")
            # drop the periodic overlay noise, keep everything else (NM_* lines
            # are often exactly what we are debugging)
            noise = ("NM_OVERLAY", "NM_FLIP", "NM_KEYREAD", "NM_REPRESENT")
            lines = [ln for ln in out.splitlines()
                     if not ln.startswith(noise)]
            self._json({"ok": ok, "out": "\n".join(lines)[-6000:]})
            return
        if u.path == "/nmtoggle":
            self._json(self.session.nm_toggle())
            return
        if u.path == "/nmclick":
            q = parse_qs(u.query)
            try:
                x = int((q.get("x") or ["-1"])[0])
                y = int((q.get("y") or ["-1"])[0])
            except ValueError:
                x = y = -1
            self._json(self.session.nm_click(x, y))
            return
        if u.path == "/nmkey":
            c = (parse_qs(u.query).get("c") or [""])[0][:1].lower()
            if c in ("u", "d", "e", "p", "c", "l", "t", "s", "b", "n", "o", "x",
                     "j", "k"):
                # write then rename: the overlay may open the file the instant it
                # appears, so it must never see a half-written (empty) file
                ok = self.session.send(
                    "\n/bin/busybox sh -c 'echo %s > /tmp/nm_key.tmp; "
                    "/bin/busybox mv /tmp/nm_key.tmp /tmp/nm_key'\n" % c)
                self._json({"ok": ok, "nmkey": c})
            else:
                self._json({"ok": False, "nmkey": c})
            return
        if u.path == "/key":
            c = (parse_qs(u.query).get("c") or [""])[0][:1].lower()
            if c in KEYS:
                label, code = KEYS[c]
                ok = self.session.key(code, label)
                self._json({"ok": ok, "key": c, "label": label,
                            "code": "0x%x" % code})
            else:
                self._json({"ok": False, "label": "?", "code": "?"})
            return
        if u.path == "/touch":
            q = parse_qs(u.query)
            try:
                x = int((q.get("x") or ["0"])[0])
                y = int((q.get("y") or ["0"])[0])
            except ValueError:
                x = y = -1
            self._json(self.session.touch(x, y))
            return
        if u.path == "/snap":
            name = self.session.snap()
            self._json({"ok": bool(name), "snapshot": name or ""})
            return
        return super().do_GET()


class Session:
    def __init__(self, directory):
        self.dir = directory
        self.sock = None
        self.console = None
        self.lock = threading.Lock()

    def attach(self, sock):
        self.sock = sock

    def send(self, text):
        with self.lock:
            if self.sock is None:
                return False
            try:
                self.sock.sendall(text.encode("latin-1"))
                return True
            except OSError:
                return False

    def status(self, text):
        try:
            with open(os.path.join(self.dir, "status.txt"), "w") as f:
                f.write(text)
        except OSError:
            pass

    def key(self, code, label):
        ok = self.send("\n/usr/tool/inject-key %s 0x%x 50 16 0\n" % (QUEUE, code))
        print("[*] web key %s 0x%x -> %s" % (label, code, "ok" if ok else "FAIL"),
              flush=True)
        self.status("sent %s 0x%x" % (label, code))
        return ok

    # --- overlay-aware click handling -----------------------------------
    # The new menu listens on /tmp/nm_key ('u'/'d'/'e'/'c'), not on the native
    # input queue, so a mouse click must be translated into those keys.
    NM_ROW_Y0 = 118
    NM_ROW_H = 52
    NM_ROWS = 6
    NM_TABS = 8
    NM_TAB_W = 58

    def nm_state(self):
        """Latest (category, selection) reported by the overlay."""
        data = bytes(self.console or b"")
        last = None
        for m in re.finditer(rb"NM_OVERLAY c=(\d+) s=(\d+)", data[-8192:]):
            last = m
        if not last:
            return None, None
        return int(last.group(1)), int(last.group(2))

    def nm_key(self, ch, wait=0.35):
        ok = self.send("\n/bin/busybox sh -c 'echo %s > /tmp/nm_key'\n" % ch)
        time.sleep(wait)
        return ok

    def nm_touch(self, x, y, wait=0.45):
        """Write a synthetic touch position and let the overlay read it.

        The overlay polls libObj's last-touch cell on the camera; under QEMU
        nothing writes that cell, so the payload also accepts 'x y' in
        /tmp/nm_touch, which exercises the *same* hit-testing code as a real
        finger (unlike the old host-side emulation).
        """
        ok = self.send(
            "\n/bin/busybox sh -c 'echo %d %d > /tmp/nm_touch.tmp; "
            "/bin/busybox mv /tmp/nm_touch.tmp /tmp/nm_touch; "
            "echo T > /tmp/nm_key'\n" % (x, y))
        time.sleep(wait)
        return ok

    def nm_visible(self):
        """True when the overlay reports itself visible (it starts closed)."""
        data = bytes(self.console or b"")
        tail = data[-16384:]
        i_open = max(tail.rfind(b"NM_SUMMON"), tail.rfind(b"NM_OPEN"),
                     tail.rfind(b"NM_SWITCH open=0x1"))
        i_close = max(tail.rfind(b"NM_CLOSE"), tail.rfind(b"NM_CLOSE_TAP"),
                      tail.rfind(b"NM_SWITCH open=0x0"),
                      tail.rfind(b"NM_BACK close"))
        if i_open < 0 and i_close < 0:
            return False                    # payload default: closed
        return i_open > i_close

    def nm_toggle(self):
        """Close the overlay (native screen comes back) or summon it again."""
        if self.nm_visible():
            self.nm_key("x")
            time.sleep(0.3)
            self.key(0x30, "DOWN")          # native key: force a repaint
            self.status("overlay closed")
            return {"ok": True, "closed": True}
        self.nm_key("o")
        self.status("overlay summoned")
        return {"ok": True, "closed": False}

    def nm_click(self, x, y):
        """A click on the panel.

        New menu visible  -> a real touch for the overlay (same hit-testing as
                             on the camera).
        New menu hidden   -> the *native* menu is a key-driven UI (A7R3 has no
                             touch navigation, see A7R3_NOTES 38), so fall back
                             to the click->key adapter: move the selection to the
                             clicked row, ENTER on the selected row.
        """
        if x < 0 or y < 0 or x > 639 or y > 479:
            return {"ok": False, "error": "bad coords"}
        # the top-right switch button always belongs to the overlay: it is the
        # summon gesture while closed and the "back to the native menu" control
        # while open, so a click there must never be translated into keys
        if x >= 584 and y <= 52:
            ok = self.nm_touch(x, y)
            self.status("nmclick %d,%d -> switch button" % (x, y))
            return {"ok": bool(ok), "action": "switch", "mode": "overlay",
                    "x": x, "y": y}
        if not self.nm_visible():
            res = self.touch(x, y)
            res["mode"] = "native"
            self.status("nmclick %d,%d -> native %s" % (x, y, res.get("action")))
            return res
        ok = self.nm_touch(x, y)
        self.status("nmclick %d,%d -> overlay touch" % (x, y))
        return {"ok": bool(ok), "action": "touch", "mode": "overlay",
                "x": x, "y": y}

    def touch(self, x, y):
        """Map a click to menu behaviour: switch tabs, move the selection to the
        clicked row, or press ENTER (also for empty areas / message boxes)."""
        if x < 0 or y < 0 or x > 639 or y > 479:
            return {"ok": False, "error": "bad coords"}
        try:
            import menu_rows
            act = menu_rows.classify(os.path.join(self.dir, "latest.png"), x, y)
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": "no frame: %r" % (exc,)}
        kind = act.get("kind")
        if kind == "none":
            return {"ok": True, "action": "none"}
        if kind == "enter":
            ok = self.key(0x1AC, "ENTER")
            self.status("touch %d,%d -> ENTER" % (x, y))
            return {"ok": ok, "action": "enter"}
        if kind == "move":
            delta = act["delta"]
            code = 0x30 if delta > 0 else 0x3
            label = "DOWN" if delta > 0 else "UP"
            one = "/usr/tool/inject-key %s 0x%x 50 16 0" % (QUEUE, code)
            self.send("; sleep 1; ".join([one] * abs(delta)) + "\n")
            self.status("touch %d,%d -> %s x%d (row %d -> %d)"
                        % (x, y, label, abs(delta), act["from"], act["to"]))
            return {"ok": True, "action": "move", "key": label,
                    "from": act["from"], "to": act["to"]}
        if kind == "page":
            code = 0x1AF if act["delta"] > 0 else 0x15
            self.send("/usr/tool/inject-key %s 0x%x 50 16 0\n" % (QUEUE, code))
            self.status("touch %d,%d -> page %+d" % (x, y, act["delta"]))
            return {"ok": True, "action": "page", "delta": act["delta"]}
        if kind == "tab":
            code = 0x1AF if act["delta"] > 0 else 0x15
            step_dir = 1 if act["delta"] > 0 else -1
            ups = int(act.get("ups", 1))
            for _ in range(ups):
                self.send("/usr/tool/inject-key %s 0x3 50 16 0\n" % QUEUE)
                time.sleep(2.5)
            cur_now = act["from"]
            steps = 0
            for _ in range(abs(act["delta"])):
                self.send("/usr/tool/inject-key %s 0x%x 50 16 0\n"
                          % (QUEUE, code))
                expect = cur_now + step_dir
                got = None
                for _ in range(10):
                    time.sleep(1.5)
                    got = self.current_tab()
                    if got == expect:
                        break
                if got != expect:
                    break
                cur_now = expect
                steps += 1
            self.send("/usr/tool/inject-key %s 0x30 50 16 0\n" % QUEUE)
            self.status("touch %d,%d -> tab %d -> %d (%d steps)"
                        % (x, y, act["from"], cur_now, steps))
            return {"ok": True, "action": "tab", "from": act["from"],
                    "to": cur_now, "steps": steps}

    def current_tab(self):
        try:
            import menu_rows
            idx, _bar = menu_rows.detect_tab(os.path.join(self.dir,
                                                          "latest.png"))
            return idx
        except Exception:  # noqa: BLE001
            return None
        return {"ok": False, "error": "unknown action"}

    def snap(self):
        src = os.path.join(self.dir, "latest.png")
        dst = os.path.join(self.dir, "snap_%s.png" % time.strftime("%H%M%S"))
        try:
            shutil.copyfile(src, dst)
            self.status("snapshot " + os.path.basename(dst))
            print("[*] snapshot -> %s" % dst, flush=True)
            return os.path.basename(dst)
        except OSError as e:
            print("[!] snapshot failed:", e, flush=True)
            return None


def convert(src, dst):
    tmp = dst[:-4] + "_tmp.png" if dst.endswith(".png") else dst + "_tmp.png"
    r = subprocess.run([sys.executable, CONV, src, tmp, "linear"],
                       capture_output=True)
    if r.returncode == 0 and os.path.exists(tmp) and os.path.getsize(tmp) > 0:
        os.replace(tmp, dst)
        return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--dir", default="/mnt/f/project/a7r3/analysis/live")
    ap.add_argument("--duration", type=int, default=0,
                    help="seconds to keep the session after the menu is ready (0 = until quit)")
    ap.add_argument("--script", default="",
                    help="comma separated keys sent after the menu is ready, e.g. d,d,e,s")
    args = ap.parse_args()

    rawdir = os.path.join(args.dir, "raw")
    shutil.rmtree(args.dir, ignore_errors=True)
    os.makedirs(rawdir, exist_ok=True)
    with open(os.path.join(args.dir, "index.html"), "w") as f:
        f.write(INDEX)
    with open(os.path.join(args.dir, "status.txt"), "w") as f:
        f.write("booting...")
    with open(os.path.join(args.dir, "latest.png"), "wb"):
        pass

    session = Session(args.dir)
    port = args.port
    srv = None
    for _ in range(20):
        try:
            srv = ThreadingHTTPServer(("0.0.0.0", port),
                                      partial(Handler, directory=args.dir,
                                              session=session))
            break
        except OSError:
            port += 1
    if srv is None:
        print("[!] no free HTTP port near %d" % args.port, flush=True)
        return 1
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    if os.path.exists(SOCK):
        os.unlink(SOCK)
    env = dict(os.environ)
    env.update({
        "A7R3_LIRO_AUTOREADY": "1",
        "A7R3_SUGI_EXEC": "1",
        "A7R3_SUGI_CENTERSCALE": "1",
        "A7R3_SURF_MORTON": "1",
        "A7R3_NO_UNIT_DUMP": "1",
        "A7R3_KICK_DUMP": "1",
        "A7R3_BLIT_DUMP": "1",
        "A7R3_SURF_DIR": rawdir,
    })
    # let the caller choose which surface to dump and how often (the overlay
    # writes the present buffers, so dumping 0x23b16000 shows the new menu)
    for _k in ("A7R3_SURF_TICK", "A7R3_DUMP_PA", "A7R3_DUMP_PA2",
               "A7R3_DUMP_PA3", "A7R3_CMD_DEC", "A7R3_BLIT_LOG"):
        if os.environ.get(_k):
            env[_k] = os.environ[_k]
    cmd = [QEMU, "-M", "cxd90045", "-smp", "2",
           "-kernel", "vmlinux-console.bin", "-initrd", "initrd-qemu.img",
           "-drive", "if=mtd,file=qemu-emmc-2g.img,format=raw",
           "-device", "loader,file=av-cam.bin,addr=0x035c6000,force-raw=on",
           "-serial", "unix:%s,server=on,wait=off" % SOCK,
           "-display", "none", "-monitor", "none"]
    qlog = open(os.path.join(args.dir, "qemu.out"), "wb")
    p = subprocess.Popen(cmd, cwd=FW, env=env, stdout=qlog,
                         stderr=subprocess.STDOUT)
    print("[*] QEMU pid %d; open http://localhost:%d/ in a browser" %
          (p.pid, port), flush=True)

    console = bytearray()

    def pump(seconds):
        # the serial console is drained by a dedicated reader thread
        time.sleep(seconds)

    def serial_reader():
        while not stop.is_set():
            try:
                r, _, _ = select.select([session.sock], [], [], 0.2)
            except (OSError, ValueError, TypeError):
                return
            if not r:
                continue
            try:
                ch = session.sock.recv(65536)
            except OSError:
                return
            if not ch:
                return
            console.extend(ch)

    stop = threading.Event()

    def frame_score(path):
        """How much is actually drawn: count pixels whose RGB is non-zero.

        The alpha nibble of an RGBA4444 pixel is always 0xf, so testing raw
        bytes for "non-zero" marks even a black screen as full.  Byte 0 is
        (G<<4)|A and byte 1 is (R<<4)|B, hence the mask.
        """
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return -1
        n = 0
        for i in range(0, len(data) - 1, 32):
            if (data[i] & 0xF0) or data[i + 1]:
                n += 1
        return n

    def pick_best_frame(window=4.0):
        """Pick the frame to show.

        The model dumps three surfaces per tick: the app's render target
        (frame_8xxx) and the two present buffers (frame_7xxx / frame_6xxx).
        What the *screen* shows is a present buffer, so those win whenever they
        hold something; the render target is only a fallback (it may contain just
        our corner badge, which is otherwise the newest "non-blank" frame and
        would make the view look black).
        """
        now = time.time()
        groups = {"present": [], "target": []}
        try:
            names = os.listdir(rawdir)
        except FileNotFoundError:
            return None, 0.0
        for n in names:
            if not n.startswith("frame_") or not n.endswith(".raw"):
                continue
            try:
                idx = int(n[6:10])
            except ValueError:
                continue
            p = os.path.join(rawdir, n)
            try:
                m = os.path.getmtime(p)
            except OSError:
                continue
            if now - m > window:
                continue
            key = "target" if idx >= 8000 else "present"
            groups[key].append((m, p))
        for key in ("present", "target"):
            cands = sorted(groups[key])
            for m, p in reversed(cands):
                if frame_score(p) > 100:
                    return p, m
        return None, 0.0

    def watch():
        last = None
        while not stop.is_set():
            best, mtime = pick_best_frame()
            if best and (best, mtime) != last:
                last = (best, mtime)
                if convert(best, os.path.join(args.dir, "latest.png")):
                    session.status("frame %s" % os.path.basename(best))
            time.sleep(0.7)

    threading.Thread(target=watch, daemon=True).start()

    try:
        sock = None
        deadline = time.time() + 60
        while time.time() < deadline:
            if os.path.exists(SOCK):
                try:
                    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    sock.connect(SOCK)
                    sock.setblocking(False)
                    break
                except OSError:
                    sock = None
            time.sleep(0.5)
        if sock is None:
            print("[!] no serial socket", flush=True)
            return 2
        session.attach(sock)
        session.console = console
        threading.Thread(target=serial_reader, daemon=True).start()

        print("[*] waiting for the guest shell", flush=True)
        t0 = time.time()
        while b"/ #" not in console and time.time() - t0 < 150:
            pump(1.0)
            if p.poll() is not None:
                print("[!] QEMU exited early", flush=True)
                return 3
        # mirror inject-serial.py exactly: probe, viewprobe, settle
        session.send("\n")
        session.send("echo PROBE_A7R3=$((1+1)); ls -l /usr/tool/inject-key\n")
        session.send("touch /usr/tool/viewprobe\n")
        session.send("echo PROBE_DONE_A7R3\n")
        pump(3.0)


        print("[*] waiting for the base menu (~1-3 min under the software renderer)",
              flush=True)
        t0 = time.time()
        ready = False
        while time.time() - t0 < 300:
            pump(1.0)
            if b"Entering exception monitor" in console:
                print("[!] guest faulted (known 0x152c38 race); rerun", flush=True)
                return 4
            if b"viewSettingMenu" in console:
                ready = True
                break
        print("[*] menu %s" % ("ready" if ready else "NOT detected (continuing)"),
              flush=True)
        session.status("menu ready" if ready else "menu not detected")

        def newest_frame_has_rows():
            try:
                import glob
                import subprocess as _sp
                import menu_rows
                raws = sorted(glob.glob(os.path.join(rawdir, "*.raw")),
                              key=os.path.getmtime)
                for cand in reversed(raws[-3:]):
                    try:
                        if os.path.getsize(cand) != 640 * 480 * 2:
                            continue
                    except OSError:
                        continue
                    png = "/tmp/live-check.png"
                    _sp.run([sys.executable, CONV, cand, png, "linear"],
                            capture_output=True)
                    if not os.path.exists(png):
                        _sp.run([sys.executable,
                                 "/mnt/f/project/a7r3/analysis/qemu_a7r3/raw2png.py",
                                 cand, png], capture_output=True)
                    rows, _s, _i = menu_rows.rows_and_selection(png)
                    if len(rows) >= 2:
                        return True
            except Exception:  # noqa: BLE001
                return False
            return False

        if ready:
            for attempt in range(3):
                if newest_frame_has_rows():
                    print("[*] good frame available", flush=True)
                    break
                print("[*] forcing a redraw (%d/3)" % (attempt + 1), flush=True)
                session.send("\n/usr/tool/inject-key 0x7a0020 0x30 50 16 0; "
                             "sleep 1; /usr/tool/inject-key 0x7a0020 0x3 50 16 0\n")
                pump(8.0)
            else:
                print("[!] still no usable frame; the web view may look black",
                      flush=True)
        print("[*] use the web buttons, or type m/e/u/d/l/r/h/f/c/s/q + Enter", flush=True)

        if args.script:
            for tok in [t.strip() for t in args.script.split(",") if t.strip()]:
                if tok == "s":
                    session.snap()
                elif tok in KEYS:
                    label, code = KEYS[tok]
                    session.key(code, label)
                pump(8.0)

        started = time.time()
        try:
            select.select([sys.stdin], [], [], 0)
            stdin_ok = True
        except (OSError, ValueError):
            stdin_ok = False
        while True:
            if args.duration and time.time() - started > args.duration:
                print("[*] duration reached", flush=True)
                break
            if p.poll() is not None:
                print("[*] QEMU exited", flush=True)
                break
            if not stdin_ok:
                pump(0.2)
                continue
            try:
                r, _, _ = select.select([sys.stdin], [], [], 1.0)
            except (OSError, ValueError):
                stdin_ok = False
                continue
            if not r:
                pump(0.2)
                continue
            line = sys.stdin.readline()
            if not line:
                # stdin closed (detached/background job): keep serving the web
                # view instead of exiting
                print("[*] stdin closed; web-only mode", flush=True)
                stdin_ok = False
                continue
            c = line.strip()[:1].lower()
            if not c:
                continue
            if c == "q":
                break
            if c == "s":
                session.snap()
                continue
            if c in KEYS:
                label, code = KEYS[c]
                session.key(code, label)
            else:
                print("[!] unknown key %r" % c, flush=True)
    finally:
        stop.set()
        try:
            session.sock.close()
        except Exception:
            pass
        try:
            p.send_signal(signal.SIGTERM)
            p.wait(timeout=15)
        except Exception:
            p.kill()
        qlog.close()
        srv.shutdown()
        print("[*] session dir: %s" % args.dir, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
