#!/usr/bin/env python3
"""Run the overlay and drive its file-based key channel to test value commits.

Feeds /setting/nm_key characters (u/d/c/e/p) through the serial shell, so the
overlay applies settings via CmnViewSettingNode::setItemSelectedByIndex and logs
NM_COMMIT / NM_BEFORE / NM_AFTER.

usage: run-overlay-commit.py [--tag cm1] [--menu 100] [--stop 175]
"""
import argparse
import os
import select
import shutil
import socket
import subprocess
import sys
import time

QEMU = "/root/qemu-ma1co/build-arm/arm-softmmu/qemu-system-arm"
FW = "/root/firmware/a7r3"
OUT = "/mnt/f/project/a7r3/analysis"
SOCK = "/tmp/a7r3cm.sock"
RAW = "/tmp/cm-frames"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="cm1")
    ap.add_argument("--menu", type=float, default=100.0)
    ap.add_argument("--stop", type=float, default=180.0)
    ap.add_argument("--keys", default="d:112,d:118,e:128,e:138,p:150",
                    help="comma list of <char>:<second>")
    args = ap.parse_args()

    shutil.rmtree(RAW, ignore_errors=True)
    os.makedirs(RAW, exist_ok=True)
    if os.path.exists(SOCK):
        os.unlink(SOCK)

    env = dict(os.environ)
    env.update({
        "A7R3_LIRO_AUTOREADY": "1",
        "A7R3_SUGI_EXEC": "1",
        "A7R3_SUGI_CENTERSCALE": "1",
        "A7R3_SURF_MORTON": "1",
        "A7R3_NO_UNIT_DUMP": "1",
        "A7R3_CMD_DEC": "1",
        "A7R3_BLIT_LOG": "1",
        "A7R3_SURF_DIR": RAW,
        "A7R3_SURF_TICK": "700",
        "A7R3_DUMP_PA": "0x202e0200",
        "A7R3_DUMP_PA2": "0x23b16000",
    })
    cmd = [QEMU, "-M", "cxd90045", "-smp", "2",
           "-kernel", "vmlinux-console.bin", "-initrd", "initrd-qemu.img",
           "-drive", "if=mtd,file=qemu-emmc-2g.img,format=raw",
           "-device", "loader,file=av-cam.bin,addr=0x035c6000,force-raw=on",
           "-serial", "unix:%s,server=on,wait=off" % SOCK,
           "-display", "none", "-monitor", "none"]
    err = open(os.path.join(OUT, "a7r3-%s-qemu.err" % args.tag), "wb")
    p = subprocess.Popen(cmd, cwd=FW, env=env, stdout=err, stderr=subprocess.STDOUT)
    console = bytearray()
    sock = None
    sent = set()

    def send(text):
        try:
            sock.sendall(text.encode("latin-1"))
        except OSError:
            pass

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            if sock is None:
                return
            try:
                r, _, _ = select.select([sock], [], [], max(0.0, end - time.time()))
            except (OSError, ValueError):
                return
            if not r:
                continue
            try:
                ch = sock.recv(65536)
            except OSError:
                return
            if not ch:
                return
            console.extend(ch)

    def key(ch, at):
        if at in sent:
            return
        sent.add(at)
        send("\n/bin/busybox sh -c 'echo %s > /setting/nm_key'\n" % ch)
        print("[*] key '%s' at %.0fs" % (ch, at), flush=True)

    try:
        deadline = time.time() + 60
        while time.time() < deadline and not os.path.exists(SOCK):
            time.sleep(0.5)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(120):
            try:
                sock.connect(SOCK)
                break
            except OSError:
                time.sleep(0.5)
        sock.setblocking(False)

        t0 = time.time()
        while True:
            now = time.time() - t0
            if now > args.stop:
                break
            pump(0.5)
            if "menu" not in sent and now >= args.menu:
                sent.add("menu")
                for _ in range(20):
                    if b"/ #" in console:
                        break
                    pump(1.0)
                send("\n/usr/tool/inject-key 0x7a0020 0x1ab 50 16 0\n")
                print("[*] MENU at %.0fs" % now, flush=True)
            for spec in args.keys.split(","):
                ch, sec = spec.split(":")
                key(ch, float(sec))
            if b"Entering exception monitor" in console:
                print("[!] guest faulted at %.0fs" % now, flush=True)
                break
        print("[*] stopping", flush=True)
    finally:
        try:
            with open(os.path.join(OUT, "a7r3-%s-serial.txt" % args.tag), "wb") as f:
                f.write(bytes(console))
        except OSError:
            pass
        dst = os.path.join(OUT, "%s-cmds" % args.tag)
        shutil.rmtree(dst, ignore_errors=True)
        try:
            shutil.copytree(RAW, dst)
        except OSError as exc:
            print("[!] frame copy failed: %s" % exc, flush=True)
        try:
            p.terminate()
            p.wait(timeout=20)
        except Exception:
            p.kill()
        err.close()

    log = os.path.join(OUT, "a7r3-%s-serial.txt" % args.tag)
    data = ""
    try:
        data = open(log, "rb").read().decode("latin-1", "replace")
    except OSError:
        pass
    for line in data.splitlines():
        if any(k in line for k in ("NM_COMMIT", "NM_BEFORE", "NM_AFTER",
                                   "NM_selected", "NM_commit", "NM_TITLE",
                                   "NM_VALUE")):
            print(line)
    print("[*] done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
