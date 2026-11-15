#!/usr/bin/env python3
"""Give mod_parse a real symbol count: walk the GNU hash chain like the
enumeration does, instead of assuming 8192 entries.

Symptom it fixes: mod_sym logged "notfound n=0x2000", i.e. it scanned the
default 8192 symbols and never matched, because CautionConfig.so has more
dynamic symbols than that.
"""
import sys

P = "/mnt/f/project/a7r3/analysis/payloads/menu_overlay.c"

OLD = """    if (hash && pm_readable((const void *)(hash + base), 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + base + 4), 4);
    else if (hash && pm_readable((const void *)hash, 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + 4), 4);
    if (m->nchain < 8 || m->nchain > 65536)
        m->nchain = 8192;
    (void)gnu_hash;
    return 0;"""

NEW = """    if (hash && pm_readable((const void *)(hash + base), 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + base + 4), 4);
    else if (hash && pm_readable((const void *)hash, 8))
        __builtin_memcpy(&m->nchain, (const void *)(hash + 4), 4);
    if ((m->nchain < 8 || m->nchain > 65536) && gnu_hash) {
        unsigned long gh = gnu_hash;
        unsigned nbuckets, symoffset, bloom_size;
        if (!pm_readable((const void *)gh, 16) &&
            pm_readable((const void *)(base + gh), 16))
            gh += base;
        if (pm_readable((const void *)gh, 16)) {
            __builtin_memcpy(&nbuckets, (const void *)gh, 4);
            __builtin_memcpy(&symoffset, (const void *)(gh + 4), 4);
            __builtin_memcpy(&bloom_size, (const void *)(gh + 8), 4);
            if (nbuckets < 65536u && bloom_size < 65536u) {
                unsigned long buckets = gh + 16 + (unsigned long)bloom_size * 4;
                unsigned long chain = buckets + (unsigned long)nbuckets * 4;
                unsigned max_sym = 0, k;
                for (k = 0; k < nbuckets; k++) {
                    unsigned b;
                    if (!pm_readable((const void *)(buckets + k * 4), 4))
                        break;
                    __builtin_memcpy(&b, (const void *)(buckets + k * 4), 4);
                    if (b > max_sym)
                        max_sym = b;
                }
                if (max_sym >= symoffset) {
                    for (;;) {
                        unsigned c;
                        unsigned long o =
                            chain + (unsigned long)(max_sym - symoffset) * 4;
                        if (!pm_readable((const void *)o, 4))
                            break;
                        __builtin_memcpy(&c, (const void *)o, 4);
                        max_sym++;
                        if (c & 1u)
                            break;
                        if (max_sym > 200000u)
                            break;
                    }
                }
                m->nchain = max_sym;
            }
        }
    }
    if (m->nchain < 8 || m->nchain > 65536)
        m->nchain = 20000;
    return 0;"""


def main():
    s = open(P, encoding="utf-8").read()
    if "m->nchain = 20000" in s:
        print("already patched")
        return 0
    if OLD not in s:
        print("anchor missing", file=sys.stderr)
        return 1
    s = s.replace(OLD, NEW, 1)
    open(P, "w", encoding="utf-8").write(s)
    print("mod_parse now uses the GNU hash chain for the symbol count")
    return 0


if __name__ == "__main__":
    sys.exit(main())
