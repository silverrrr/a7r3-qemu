#!/usr/bin/env python3
"""Make the machine model dump *three* surfaces per tick.

Why: the app alternates its present targets and some views (a caution dialog)
only land in one of them, so dumping a single fixed PA shows a black screen even
though the panel is fine.  A7R3_DUMP_PA2 was dumped only on odd ticks and only
one address; this patch dumps A7R3_DUMP_PA (0x202E0200), A7R3_DUMP_PA2 and
A7R3_DUMP_PA3 on every tick, into frame_8000/7000/6000 series.

Run: python3 patch-bionz-3pa.py [--revert]
"""
import os
import sys

SRC = "/root/qemu-ma1co/hw/arm/bionz.c"
MARK = "A7R3_DUMP_PA3"

HELPER = '''
/* Dump one configured surface (A7R3_DUMP_PA<n>) with the same tiling the panel
 * uses.  Kept separate from cxd90045_sugi_surface_dump() so several surfaces
 * can be captured per tick: the app alternates present targets and some views
 * only show up in one of them. */
static void cxd90045_sugi_dump_pa_env(const char *envname, unsigned idx)
{
    const char *dir = getenv("A7R3_SURF_DIR");
    const char *s = getenv(envname);
    static unsigned char buf[640 * 480 * 2];
    static unsigned char lin[640 * 480 * 2];
    char path[256];
    FILE *f;
    hwaddr pa;
    unsigned x, y;

    if (!dir || !s || !*s) {
        return;
    }
    pa = (hwaddr)strtoull(s, NULL, 0);
    if (!cxd90045_sugi_pa_ok(pa, sizeof(buf))) {
        return;
    }
    cpu_physical_memory_read(pa, buf, sizeof(buf));
    if (getenv("A7R3_SURF_MORTON")) {
        for (y = 0; y < 480; y++) {
            for (x = 0; x < 640; x++) {
                unsigned yy = 479u - y;
                unsigned z = (x & 1) | ((yy & 1) << 1) | ((x & 2) << 1) |
                             ((yy & 2) << 2) | ((x & 4) << 2) |
                             ((yy & 4) << 3);
                unsigned off = (((yy >> 3) * 80 + (x >> 3)) * 64 + z) * 2;
                lin[y * 1280 + x * 2] = buf[off];
                lin[y * 1280 + x * 2 + 1] = buf[off + 1];
            }
        }
        memcpy(buf, lin, sizeof(buf));
    }
    snprintf(path, sizeof(path), "%s/frame_%04u.raw", dir, idx);
    f = fopen(path, "wb");
    if (f) {
        fwrite(buf, 1, sizeof(buf), f);
        fclose(f);
    }
}
'''

OLD_TICK = '''    {
        const char *s2 = getenv("A7R3_DUMP_PA2");
        if (s2 && *s2 && (t->seq & 1)) {'''

NEW_TICK = '''    {
        /* every configured surface, every tick (see cxd90045_sugi_dump_pa_env) */
        cxd90045_sugi_surface_dump(8000 + (t->seq % 500));
        cxd90045_sugi_dump_pa_env("A7R3_DUMP_PA2", 7000 + (t->seq % 500));
        cxd90045_sugi_dump_pa_env("A7R3_DUMP_PA3", 6000 + (t->seq % 500));
    }
    if (0) {
        const char *s2 = getenv("A7R3_DUMP_PA2");
        if (s2 && *s2 && (t->seq & 1)) {'''


def main():
    revert = "--revert" in sys.argv
    src = open(SRC, encoding="utf-8").read()
    if revert:
        if MARK not in src:
            print("nothing to revert")
            return 0
        # restore the original branch by cutting the dead code we added
        i = src.index('    {\n        /* every configured surface, every tick')
        j = src.index('    t->seq++;')
        src = src[:i] + src[j:]
        src = src.replace(HELPER, "")
        open(SRC, "w", encoding="utf-8").write(src)
        print("reverted")
        return 0

    if MARK in src:
        print("already patched")
        return 0
    if OLD_TICK not in src:
        print("anchor not found", file=sys.stderr)
        return 1
    src = src.replace(OLD_TICK, NEW_TICK, 1)
    # insert the helper before the tick callback
    anchor = "static void cxd90045_sugi_tick_cb(void *opaque)"
    src = src.replace(anchor, HELPER + "\n" + anchor, 1)
    open(SRC, "w", encoding="utf-8").write(src)
    print("patched %s" % SRC)
    return 0


if __name__ == "__main__":
    sys.exit(main())
