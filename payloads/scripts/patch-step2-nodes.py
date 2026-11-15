#!/usr/bin/env python3
"""Step 2 of the reflection migration: the 456-item enumeration stops calling
dlsym() for every symbol and computes the node address itself as
base + st_value (which is exactly what the loader bound for those exported
.bss objects).

dlsym() is kept as a last-resort fallback (and counted in the log as
NM_refl_fb) so a symbol the relocation moved elsewhere can never make the menu
smaller -- but the normal path no longer touches dlopen/dlsym.

Rollback: git checkout analysis/payloads/menu_overlay.c
"""
import sys

P = "/mnt/f/project/a7r3/analysis/payloads/menu_overlay.c"

HELPER = r'''
/* Is `node_addr` really a CmnViewSettingNode?  Used by the enumeration below:
 * a bad st_value would hand us garbage that only crashes later, when the menu
 * draws it, so every address is validated before it is accepted. */
static int node_ok(unsigned long node_addr)
{
    void *children = NULL;
    unsigned vptr = 0;
    int count = 0, c2;

    if (!pm_readable((const void *)node_addr, sizeof(struct node)))
        return 0;
    __builtin_memcpy(&vptr, (const void *)node_addr, 4);
    if (!plausible((const void *)(unsigned long)vptr))
        return 0;
    /* `count` must look like an option count and the children array has to be
     * readable: this filters out any symbol whose address is not a node */
    __builtin_memcpy(&children, (const void *)(node_addr + 4), 4);
    __builtin_memcpy(&count, (const void *)(node_addr + 8), 4);
    if (count < 0 || count > 200)
        return 0;
    if (count > 0 && !pm_readable(children, (unsigned)count * 4u))
        return 0;
    /* every child has to be a readable node as well */
    for (c2 = 0; c2 < count; c2++) {
        unsigned long cp = 0;
        __builtin_memcpy(&cp, (const void *)((const char *)children + c2 * 4), 4);
        if (!plausible((const void *)cp) ||
            !pm_readable((const void *)cp, sizeof(struct node)))
            return 0;
    }
    return 1;
}
'''

OLD_LOOP = '''        /* the object itself must be readable and start with a plausibly
         * relocated vtable pointer -- otherwise a bad st_value would hand us a
         * garbage node that crashes the app later, when the menu draws it.
         * dlsym() is authoritative here: it returns the address the loader
         * actually bound, so we never have to trust our own st_value math. */
        {
            void *sym_addr = g_caution ? dlsym(g_caution, name) : NULL;
            if (!sym_addr)
                continue;
            node_addr = (unsigned long)sym_addr;
        }
        if (!pm_readable((const void *)node_addr, sizeof(struct node)))
            continue;
        __builtin_memcpy(&vptr, (const void *)node_addr, 4);
        if (!plausible((const void *)(unsigned long)vptr))
            continue;
        {
            /* `count` must look like an option count and the children array has
             * to be readable: this filters out any symbol whose address is not
             * really a node (which would crash the menu later) */
            void *children = NULL;
            int count = 0;
            int c2;
            __builtin_memcpy(&children, (const void *)(node_addr + 4), 4);
            __builtin_memcpy(&count, (const void *)(node_addr + 8), 4);
            if (count < 0 || count > 200)
                continue;
            if (count > 0 && !pm_readable(children, (unsigned)count * 4u))
                continue;
            /* every child has to be a readable node as well */
            for (c2 = 0; c2 < count; c2++) {
                unsigned long cp = 0;
                __builtin_memcpy(&cp, (const void *)((const char *)children + c2 * 4), 4);
                if (!plausible((const void *)cp) ||
                    !pm_readable((const void *)cp, sizeof(struct node)))
                    break;
            }
            if (count > 0 && c2 < count)
                continue;
        }
'''

NEW_LOOP = '''        /* st_value is the module-relative offset of the object, so the node
         * sits at base + st_value -- the same address dlsym() used to return
         * for these exported .bss objects.  No dlopen/dlsym on the normal
         * path any more; the loader lookup survives only as a fallback for a
         * symbol whose relocated address does not validate. */
        node_addr = base + (unsigned long)st_value;
        if (node_ok(node_addr)) {
            g_refl_hit++;
        } else {
            void *sym_addr =
                (dlsym && g_caution) ? dlsym(g_caution, name) : NULL;
            if (!sym_addr)
                continue;
            node_addr = (unsigned long)sym_addr;
            g_refl_fb++;
            if (!node_ok(node_addr))
                continue;
        }
'''

OLD_DECL = '''        unsigned long node_addr;
        unsigned vptr;
        int k, nul = 0, bad = 0;'''
NEW_DECL = '''        unsigned long node_addr;
        int k, nul = 0, bad = 0;'''

OLD_COUNTERS = '''static struct menu_item g_items[MAX_ITEMS];
static int g_item_count;'''
NEW_COUNTERS = '''static struct menu_item g_items[MAX_ITEMS];
static int g_item_count;
static int g_refl_hit;             /* nodes found via base + st_value */
static int g_refl_fb;              /* nodes that needed the dlsym fallback */'''

OLD_LOG = '''    rc = collect_items(base);
    log_hex("NM_items=", (unsigned long)rc);'''
NEW_LOG = '''    rc = collect_items(base);
    log_hex("NM_items=", (unsigned long)rc);
    log_hex("NM_refl=", (unsigned long)g_refl_hit);
    log_hex("NM_refl_fb=", (unsigned long)g_refl_fb);'''

OLD_COMMENT = '''/* CautionConfig.so exports 456 cmnViewSettingNodeRoot* objects (all in */
/* .bss, so dlsym() returns the node itself).  Instead of a hard-coded  */'''
NEW_COMMENT = '''/* CautionConfig.so exports 456 cmnViewSettingNodeRoot* objects (all in */
/* .bss, so base+st_value is the node itself).  Instead of a hard-coded  */'''


def main():
    s = open(P, encoding="utf-8").read()
    if "node_ok" in s:
        print("already patched")
        return 0
    anchor = "/* walk the dynamic section of the module `base` and collect our nodes */"
    if anchor not in s:
        print("collect_items anchor missing", file=sys.stderr)
        return 1
    s = s.replace(anchor, HELPER + "\n" + anchor, 1)
    for old, new in ((OLD_LOOP, NEW_LOOP), (OLD_DECL, NEW_DECL),
                     (OLD_COUNTERS, NEW_COUNTERS), (OLD_LOG, NEW_LOG),
                     (OLD_COMMENT, NEW_COMMENT)):
        if old not in s:
            print("anchor missing:\n%s" % old[:70], file=sys.stderr)
            return 1
        s = s.replace(old, new, 1)
    open(P, "w", encoding="utf-8").write(s)
    print("step 2 applied (enumeration uses base + st_value)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
