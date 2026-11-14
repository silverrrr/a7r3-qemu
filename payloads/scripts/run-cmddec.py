#!/usr/bin/env python3
"""Boot A7R3 with the model command-list decoder and capture menu frames.

The model prints (stderr):
  CMDDEC  ... per executed Sugilite command list: surface/area/rect/term count
  CMDBLIT ... per blit attempt: src/dst/size/fmt

This script boots, injects MENU, moves the selection, and stops, keeping
  analysis/<tag>-qemu.err   model log (CMDDEC / CMDBLIT / UDLOG)
  analysis/<tag>-serial.txt guest console
  analysis/<tag>-cmds/      cmdbuf_*.bin dumps (A7R3_CMD_DUMP)

usage: run-cmddec.py [--tag cd1] [--menu 100] [--keys 118] [--stop 150]
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
import select

QEMU = "/root/qemu-ma1co/build-arm/arm-softmmu/qemu-system-arm"
FW = "/root/firmware/a7r3"
OUT = "/mnt/f/project/a7r3/analysis"
SOCK = "/tmp/a7r3cd.sock"
RAW = "/tmp/cd-frames"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="cd1")
    ap.add_argument("--menu", type=float, default=100.0)
    ap.add_argument("--keys", type=float, default=118.0)
    ap.add_argument("--stop", type=float, default=150.0)
    ap.add_argument("--frames", action="store_true",
                    help="also dump panel frames (KICK_DUMP/BLIT_DUMP)")
    ap.add_argument("--dump-pa", default=None,
                    help="model surface dump address (A7R3_DUMP_PA)")
    ap.add_argument("--dump-pa2", default=None,
                    help="second dump address, alternating ticks (A7R3_DUMP_PA2)")
    ap.add_argument("--tick", default=None,
                    help="periodic dump interval in ms (A7R3_SURF_TICK)")
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
        "A7R3_CMD_DUMP": "1",
        "A7R3_UD_LOG": "1",
        "A7R3_SURF_DIR": RAW,
    })
    if args.frames:
        env["A7R3_KICK_DUMP"] = "1"
        env["A7R3_BLIT_DUMP"] = "1"
    if args.dump_pa:
        env["A7R3_DUMP_PA"] = args.dump_pa
    if args.dump_pa2:
        env["A7R3_DUMP_PA2"] = args.dump_pa2
    if args.tick:
        env["A7R3_SURF_TICK"] = args.tick
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
            pump(1.0)
            if ("menu" not in sent) and now >= args.menu:
                sent.add("menu")
                # wait for a shell prompt before sending
                for _ in range(20):
                    if b"/ #" in console:
                        break
                    pump(1.0)
                send("\n/usr/tool/inject-key 0x7a0020 0x1ab 50 16 0\n")
                print("[*] MENU at %.0fs" % now, flush=True)
            if ("keys" not in sent) and now >= args.keys:
                sent.add("keys")
                for code in ("0x30", "0x30", "0x3"):
                    send("\n/usr/tool/inject-key 0x7a0020 %s 50 16 0\n" % code)
                    pump(3.0)
                print("[*] keys at %.0fs" % (time.time() - t0), flush=True)
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

    log = os.path.join(OUT, "a7r3-%s-qemu.err" % args.tag)
    try:
        data = open(log, "rb").read().decode("latin-1", "replace")
    except OSError:
        data = ""
    print("[*] CMDDEC %d / CMDBLIT %d / UDLOG %d / nopad %d" % (
        data.count("CMDDEC"), data.count("CMDBLIT"),
        data.count("UDLOG"), data.count("Entering exception monitor")), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
