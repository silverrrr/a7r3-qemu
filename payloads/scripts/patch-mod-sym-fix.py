#!/usr/bin/env python3
"""Fix step 1's reflection calls: they found nothing because /proc/self/maps was
never (re)loaded for this path -- the enumeration calls pm_reload_maps() first,
resolve_commit()/libobj_base() did not, so pm_readable() rejected the module's
pages and mod_sym returned 0.

Also log why a lookup failed, so the next boot tells us instead of guessing.
"""
import sys

P = "/mnt/f/project/a7r3/analysis/payloads/menu_overlay.c"

PROTO = """static unsigned long mod_base(const char *needle)"""
PROTO_NEW = """static void pm_reload_maps(void);

static unsigned long mod_base(const char *needle)"""

OLD_SYM = """    base = mod_base(needle);
    if (!base)
        return 0;
    if (mod_parse(base, &ms) != 0)
        return 0;"""

NEW_SYM = """    /* the maps cache may predate the firmware mapping its libraries; the
     * enumeration path reloads it, so do the same here (once) */
    {
        static int reloaded;
        if (!reloaded) {
            reloaded = 1;
            pm_reload_maps();
        }
    }
    base = mod_base(needle);
    if (!base) {
        log_hex("NM_MODSYM nobase ", (unsigned long)needle);
        return 0;
    }
    {
        int rc = mod_parse(base, &ms);
        if (rc != 0) {
            log_hex("NM_MODSYM noparse rc=", (unsigned long)rc);
            log_hex("NM_MODSYM base=", base);
            return 0;
        }
    }"""

OLD_END = """        return base + st_value;
    }
    return 0;
}"""

NEW_END = """        return base + st_value;
    }
    log_hex("NM_MODSYM notfound n=", (unsigned long)ms.nchain);
    return 0;
}"""


def main():
    s = open(P, encoding="utf-8").read()
    if "NM_MODSYM nobase" in s:
        print("already patched")
        return 0
    for old, new in ((PROTO, PROTO_NEW), (OLD_SYM, NEW_SYM), (OLD_END, NEW_END)):
        if old not in s:
            print("anchor missing:\n%s" % old[:60], file=sys.stderr)
            return 1
        s = s.replace(old, new, 1)
    open(P, "w", encoding="utf-8").write(s)
    print("mod_sym now reloads maps and logs failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
