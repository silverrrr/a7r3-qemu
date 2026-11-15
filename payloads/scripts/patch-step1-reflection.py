#!/usr/bin/env python3
"""Step 1 of the reflection migration: replace only libobj_base() and
resolve_commit() with an own ELF symbol lookup, leaving the 456-item
enumeration (which still uses dlsym) untouched.

Why these two first: they are the two places that dlopen()/dlsym() on the hot
path (touch coordinates and the SR getters/setters).  If the payload has to work
when it was itself dlopen()ed, these must not call dlopen().

Rollback: git checkout analysis/payloads/menu_overlay.c (or the repo copy at
796ae5c).
"""
import sys

P = "/mnt/f/project/a7r3/analysis/payloads/menu_overlay.c"

HELPERS = r'''
/* --- own ELF symbol lookup (no dlopen/dlsym) -------------------------
 *
 * Finds a module by name in /proc/self/maps (lowest mapping = ELF header),
 * parses its dynamic section and returns base+st_value for a symbol.
 * Step 1: only libobj_base()/resolve_commit() use it.
 * ------------------------------------------------------------------ */

static unsigned long mod_base(const char *needle)
{
    static char buf[65536];
    int fd, n;
    char *p, *end;
    unsigned long best = 0;

    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return 0;
    n = 0;
    while (n < (int)sizeof(buf) - 1) {
        long r = sys3(SYS_read, fd, (long)(buf + n),
                      (long)(sizeof(buf) - 1 - (unsigned)n));
        if (r <= 0)
            break;
        n += (int)r;
    }
    sys2(SYS_close, fd, 0);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    p = buf;
    end = buf + n;
    while (p < end) {
        char *line = p;
        unsigned long lo = 0;
        const char *q;

        while (p < end && *p != '\n')
            p++;
        if (p < end)
            *p = 0;
        else
            p = end;
        if (str_has(line, needle)) {
            for (q = line; *q && *q != '-'; q++) {
                int d;
                char c = *q;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (c >= 'a' && c <= 'f')
                    d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    d = c - 'A' + 10;
                else
                    break;
                lo = (lo << 4) | (unsigned)d;
            }
            if (lo && (!best || lo < best))
                best = lo;
        }
        if (p >= end)
            break;
        p++;
    }
    return best;
}

struct mod_syms {
    unsigned long symtab, strtab, strsz, nchain;
};

static int mod_parse(unsigned long base, struct mod_syms *m)
{
    const unsigned char *eh = (const unsigned char *)base;
    unsigned e_phoff, e_phentsize, e_phnum, i;
    unsigned long dyn = 0, dyn_len = 0, hash = 0, gnu_hash = 0;
    unsigned char *z = (unsigned char *)m;

    for (i = 0; i < sizeof(*m); i++)
        z[i] = 0;
    if (!eh || !pm_readable(eh, 0x34))
        return -1;
    __builtin_memcpy(&e_phoff, eh + 0x1c, 4);
    __builtin_memcpy(&e_phentsize, eh + 0x2a, 2);
    __builtin_memcpy(&e_phnum, eh + 0x2c, 2);
    if (e_phnum > 64 || e_phentsize < 32)
        return -2;
    for (i = 0; i < e_phnum; i++) {
        const unsigned char *ph = eh + e_phoff + i * e_phentsize;
        unsigned type, vaddr, memsz;
        if (!pm_readable(ph, 32))
            return -3;
        __builtin_memcpy(&type, ph + 0, 4);
        __builtin_memcpy(&vaddr, ph + 8, 4);
        __builtin_memcpy(&memsz, ph + 20, 4);
        if (type == 2) {
            dyn = base + vaddr;
            dyn_len = memsz;
            break;
        }
    }
    if (!dyn || !pm_readable((const void *)dyn, (unsigned)dyn_len))
        return -4;
    for (i = 0; i + 8 <= dyn_len; i += 8) {
        unsigned tag, val;
        __builtin_memcpy(&tag, (const void *)(dyn + i), 4);
        __builtin_memcpy(&val, (const void *)(dyn + i + 4), 4);
        if (tag == 0)
            break;
        switch (tag) {
        case 4:  hash = val; break;
        case 5:  m->strtab = val; break;
        case 6:  m->symtab = val; break;
        case 10: m->strsz = val; break;
        case 0x6ffffef5UL: gnu_hash = val; break;
        default: break;
        }
    }
    if (m->symtab && !pm_readable((const void *)m->symtab, 16) &&
        pm_readable((const void *)(base + m->symtab), 16))
        m->symtab += base;
    if (m->strtab && !pm_readable((const void *)m->strtab, 8) &&
        pm_readable((const void *)(base + m->strtab), 8))
        m->strtab += base;
    if (!m->symtab || !m->strtab)
        return -5;
    if (hash && pm_readable((const void *)(hash + base), 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + base + 4), 4);
    else if (hash && pm_readable((const void *)hash, 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + 4), 4);
    if (m->nchain < 8 || m->nchain > 65536)
        m->nchain = 8192;
    (void)gnu_hash;
    return 0;
}

static unsigned long mod_sym(const char *needle, const char *name)
{
    struct mod_syms ms;
    unsigned long base;
    unsigned i;
    int nl = 0;

    if (!needle || !name)
        return 0;
    while (name[nl])
        nl++;
    base = mod_base(needle);
    if (!base)
        return 0;
    if (mod_parse(base, &ms) != 0)
        return 0;
    for (i = 1; i < ms.nchain; i++) {
        const unsigned char *sym = (const unsigned char *)(ms.symtab + i * 16);
        unsigned st_name, st_value;
        const char *s;
        int k;
        if (!pm_readable(sym, 16))
            break;
        __builtin_memcpy(&st_name, sym + 0, 4);
        __builtin_memcpy(&st_value, sym + 4, 4);
        if (!st_name || !st_value)
            continue;
        if (ms.strsz && st_name >= ms.strsz)
            continue;
        s = (const char *)(ms.strtab + st_name);
        if (!pm_readable(s, (unsigned)nl + 1))
            continue;
        for (k = 0; k < nl; k++)
            if (s[k] != name[k])
                break;
        if (k != nl || s[nl] != 0)
            continue;
        return base + st_value;
    }
    return 0;
}
'''

OLD_LIBOBJ = '''    void *obj, *fn;
    if (!dlopen || !dlsym)
        return 0;
    obj = dlopen("/usr/lib/libObj.so", 5);
    if (!obj)
        return 0;
    fn = dlsym(obj, "GRM_bitmapCreate");
    if (!fn)
        return 0;
    return (unsigned long)fn - LIBOBJ_GRM_CREATE;'''

NEW_LIBOBJ = '''    unsigned long fn = mod_sym("/libObj.so", "GRM_bitmapCreate");
    if (!fn)
        return 0;
    return fn - LIBOBJ_GRM_CREATE;'''

OLD_COMMIT = '''    g_commit_tried = 1;
    if (!g_caution && dlopen)
        g_caution = dlopen("/usr/lib/CautionConfig.so", 5);
    if (g_caution && dlsym) {
        g_commit_fn = (commit_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode22setItemSelectedByIndexEi");
        g_commit_sr = (commit_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode22setItemSelectedBySRNumEi");
        g_get_sr_idx = (getint2_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    }'''

NEW_COMMIT = '''    g_commit_tried = 1;
    /* no dlopen: look the methods up in the already mapped module */
    g_commit_fn = (commit_fn)mod_sym(
        "/CautionConfig.so", "_ZN18CmnViewSettingNode22setItemSelectedByIndexEi");
    g_commit_sr = (commit_fn)mod_sym(
        "/CautionConfig.so", "_ZN18CmnViewSettingNode22setItemSelectedBySRNumEi");
    g_get_sr_idx = (getint2_fn)mod_sym(
        "/CautionConfig.so", "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");'''


def main():
    s = open(P, encoding="utf-8").read()
    if "mod_sym" in s:
        print("already patched")
        return 0
    anchor = "static void resolve_commit(void)\n{"
    if anchor not in s:
        print("resolve_commit anchor missing", file=sys.stderr)
        return 1
    s = s.replace(anchor, HELPERS + "\n" + anchor, 1)
    for old, new in ((OLD_LIBOBJ, NEW_LIBOBJ), (OLD_COMMIT, NEW_COMMIT)):
        if old not in s:
            print("anchor missing:\n%s" % old[:60], file=sys.stderr)
            return 1
        s = s.replace(old, new, 1)
    open(P, "w", encoding="utf-8").write(s)
    print("step 1 applied (libobj_base + resolve_commit use reflection)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
