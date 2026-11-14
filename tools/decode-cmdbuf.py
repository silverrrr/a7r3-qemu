#!/usr/bin/env python3
"""Decode an A7R3 Sugilite 2D command list.

The list is an array of 32-bit words written by the GraphicsEngine as
(op << 16 | 0xf000, value) pairs.  A value of 0x00000281 is a terminator:
the next word points at a state block and the unit performs one draw.

Known ops (see A7R3_NOTES.md 17):

  0x0112  destination surface:  PA = value << 4
  0x0589  texture surface:      PA = value << 3
  0x0586
  0x0587  texture size:         w = value >> 16, h = value & 0xffff
  0x0049  drawing area:         w = value & 0xffff, h = value >> 16
  0x0047  rect edge 0 (centre coords, 16.16 fixed)
  0x0048  rect edge 1
  0x0105  fill colour (RGBA4444)
  0x0450..0x0454  uniforms (float[4] fanned out one word each)
  0x0216  constant/shader block pointer
  0x0202  shaded draw

usage: decode-cmdbuf.py FILE [--limit N] [--summary-only] [--raw]
"""
import argparse
import struct
import sys

OPS = {
    0x0112: "dest-surface",
    0x0589: "texture",
    0x0586: "tex-size",
    0x0587: "tex-size",
    0x0049: "area",
    0x0047: "rect0",
    0x0048: "rect1",
    0x0105: "colour",
    0x0450: "uniform0",
    0x0451: "uniform1",
    0x0452: "uniform2",
    0x0453: "uniform3",
    0x0454: "uniform4",
    0x0216: "const-block",
    0x0202: "draw",
    0x0000: "nested",
}

TERM = 0x00000281


def rect(w):
    """Decode a rect word: low 16 bits = x, high 16 bits = y (signed)."""
    x = struct.unpack("<h", struct.pack("<H", w & 0xffff))[0]
    y = struct.unpack("<h", struct.pack("<H", (w >> 16) & 0xffff))[0]
    return x, y


def decode(data, limit):
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    rows = []
    i = 0
    draws = 0
    surf = None
    area = None
    tex = None
    rects = []
    while i + 1 < len(words):
        w = words[i]
        if w == TERM:
            v = words[i + 1]
            rows.append((i, "unit-draw", w, v, "state-block ptr=%08x" % v))
            draws += 1
            i += 2
            continue
        if (w & 0xffff) != 0xf000:
            i += 1
            continue
        op = w >> 16
        v = words[i + 1]
        note = ""
        if op == 0x0112:
            surf = v << 4
            note = "PA=%08x" % surf
        elif op == 0x0589:
            tex = v << 3
            note = "PA=%08x" % tex
        elif op in (0x0586, 0x0587):
            note = "%dx%d" % (v >> 16, v & 0xffff)
        elif op == 0x0049:
            area = (v & 0xffff, v >> 16)
            note = "%dx%d" % area
        elif op in (0x0047, 0x0048):
            rx, ry = rect(v)
            rects.append((op, rx, ry))
            note = "(%d,%d)" % (rx, ry)
        elif op == 0x0105:
            note = "RGBA4444=%04x" % (v & 0xffff)
        elif 0x0450 <= op <= 0x0454:
            f = struct.unpack("<f", struct.pack("<I", v))[0]
            note = "float=%g" % f
        rows.append((i, OPS.get(op, "op%04x" % op), w, v, note))
        i += 2
        if len(rows) >= limit:
            break
    return words, rows, dict(draws=draws, surf=surf, area=area, tex=tex,
                             rects=rects)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--limit", type=int, default=100000)
    ap.add_argument("--summary-only", action="store_true")
    ap.add_argument("--raw", action="store_true")
    args = ap.parse_args()

    data = open(args.path, "rb").read()
    words, rows, info = decode(data, args.limit)

    print("file   %s  (%d bytes, %d words)" % (args.path, len(data), len(words)))
    print("ops    %d  (op<<16|0xf000 words, terminator 0x00000281)" % len(rows))
    print("draws  %d   dest-surface %s   area %s   texture %s" % (
        info["draws"],
        "0x%08x" % info["surf"] if info["surf"] is not None else "-",
        "%dx%d" % info["area"] if info["area"] else "-",
        "0x%08x" % info["tex"] if info["tex"] else "-"))
    if info["rects"]:
        edges = {}
        for op, rx, ry in info["rects"]:
            edges.setdefault(op, []).append((rx, ry))
        for op in sorted(edges):
            pts = edges[op]
            print("rect%s  %d entries, first=%s last=%s" % (
                "0" if op == 0x0047 else "1", len(pts), pts[0], pts[-1]))
    if not args.summary_only:
        print()
        for idx, name, w, v, note in rows:
            print("%04x  %-12s %08x %08x  %s" % (idx, name, w, v, note))
    if args.raw:
        print()
        for i in range(0, min(len(words), 296), 8):
            print("%04x: %s" % (i * 4,
                                " ".join("%08x" % w for w in words[i:i + 8])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
