#!/usr/bin/env python3
"""Step 3a: remove every dlopen() from the payload.

Why: the two-stage loader (stub dlopen()s this payload) has always crashed at
0x00000000 inside the payload's own nested dlopen.  With the menu path already
dlopen-free (steps 1 and 2), only three places still called dlopen():

  * resolve_nodes()        - dlopen(CautionConfig, RTLD_NOLOAD) just to get a
                             handle for dlsym(); the base comes from maps now;
  * find_surface()         - dlopen(libObj) + dlsym(sys_getInstance), gated by
                             /setting/nm_present, replaced by mod_sym();
  * install_present_hook() - dlopen(libObj) + dlsym(GRM_bitmapCreate), replaced
                             by libobj_base() (which is reflection already).

It also moves the last dlsym(CautionConfig handle, ...) uses (sr_report/
self_test, both debug-only) to mod_sym, drops the dlsym fallback in the
enumeration, and removes the constructor's hard dlsym requirement (a direct
weak pthread_create reference is enough).

The remaining dlsym(RTLD_DEFAULT, ...) calls only look symbols up in libc/the
already loaded image -- they never load anything.

Rollback: git checkout analysis/payloads/menu_overlay.c
"""
import sys

P = "/mnt/f/project/a7r3/analysis/payloads/menu_overlay.c"

OLD = [
    # --- the weak dlopen declaration and the CautionConfig handle ---
    ('extern void *dlopen(const char *path, int flags) __attribute__((weak));\n', ''),
    ('static void *g_caution;            /* CautionConfig.so handle (dlopen) */\n', ''),
    ('typedef void *(*dlopen_fn2)(const char *, int);\n', ''),

    # --- enumeration: reflection only, no loader fallback ---
    ('''        /* st_value is the module-relative offset of the object, so the node
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
''',
     '''        /* st_value is the module-relative offset of the object, so the node
         * sits at base + st_value.  This is the only way addresses are made:
         * no dlopen()/dlsym() anywhere -- the payload has to work when it was
         * itself dlopen()ed by the two-stage loader (NOTES 66.9). */
        node_addr = base + (unsigned long)st_value;
        if (!node_ok(node_addr))
            continue;
        g_refl_hit++;
'''),

    # --- resolve_nodes: base from maps only ---
    ('''    if (g_item_count || g_tab_error)
        return;
    if (!dlopen || !dlsym) {
        out("NM_NO_DL\\n", 9);
        g_tab_error = 1;
        return;
    }
    pm_reload_maps();
    g_caution = dlopen("/usr/lib/CautionConfig.so", 5 /* RTLD_LAZY|RTLD_NOLOAD */);
    log_hex("NM_caution=", (unsigned long)g_caution);
    if (!g_caution) {
        out("NM_FAIL caution\\n", 16);
        g_tab_error = 1;
        return;
    }
    {
        void *any = dlsym(g_caution, "cmnViewSettingNodeRoot24pMode");
        log_hex("NM_sym_any=", (unsigned long)any);
        base = module_base_from_maps("CautionConfig.so");
        if (!base && any)
            base = elf_base_of((unsigned long)any);
    }
''',
     '''    if (g_item_count || g_tab_error)
        return;
    pm_reload_maps();
    /* the module base comes from /proc/self/maps: nothing is loaded here, so
     * this works the same whether we were preloaded or dlopen()ed */
    base = module_base_from_maps("CautionConfig.so");
'''),

    # --- sr_report (debug helper) ---
    ('''    log_utf8("NM_SR_TAG ", tag);
    if (!n || !g_caution)
        return;
    f_srsel = (getint_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode20getSRNumSelectedItemERi");
    f_sridx = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    f_state = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getStateByIndexEiRi");
    f_issel = (getint1_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode21isItemSelectedByIndexEi");
''',
     '''    log_utf8("NM_SR_TAG ", tag);
    if (!n)
        return;
    f_srsel = (getint_fn)mod_sym("/CautionConfig.so",
        "_ZN18CmnViewSettingNode20getSRNumSelectedItemERi");
    f_sridx = (getint2_fn)mod_sym("/CautionConfig.so",
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    f_state = (getint2_fn)mod_sym("/CautionConfig.so",
        "_ZN18CmnViewSettingNode15getStateByIndexEiRi");
    f_issel = (getint1_fn)mod_sym("/CautionConfig.so",
        "_ZN18CmnViewSettingNode21isItemSelectedByIndexEi");
'''),

    # --- self_test (debug gate) ---
    ('''    f_sridx = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
''',
     '''    f_sridx = (getint2_fn)mod_sym("/CautionConfig.so",
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
'''),

    # --- find_surface (nm_present gate) ---
    ('''static void find_surface(void)
{
    dlopen_fn2 dop = (dlopen_fn2)dlopen;
    void *obj;
    eng_inst_fn inst;
    void *eng;
    int i;

    if (g_surface || !dop)
        return;
    obj = dop("/usr/lib/libObj.so", 5);
    log_hex("NM_obj2=", (unsigned long)obj);
    if (!obj)
        return;
    inst = (eng_inst_fn)dlsym(obj, "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    eng = inst ? inst() : NULL;
''',
     '''static void find_surface(void)
{
    eng_inst_fn inst;
    void *eng;
    int i;

    if (g_surface)
        return;
    inst = (eng_inst_fn)mod_sym("/libObj.so",
        "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    log_hex("NM_eng_inst=", (unsigned long)inst);
    eng = inst ? inst() : NULL;
'''),

    # --- install_present_hook (nm_present gate) ---
    ('''static void install_present_hook(void)
{
    void *obj;
    dlopen_fn2 dop = (dlopen_fn2)dlopen;
    void *fn;
    void *orig = NULL;
    unsigned long base;
    int rc;

    if (g_present_tried)
        return;
    g_present_tried = 1;
    if (!dop)
        return;
    obj = dop("/usr/lib/libObj.so", 5);
    if (!obj)
        return;
    fn = dlsym(obj, "GRM_bitmapCreate");
    if (!fn)
        return;
    base = (unsigned long)fn - 0x636bd9UL;    /* GRM_bitmapCreate file vaddr */
    log_hex("NM_objbase=", base);
''',
     '''static void install_present_hook(void)
{
    void *orig = NULL;
    unsigned long base;
    int rc;

    if (g_present_tried)
        return;
    g_present_tried = 1;
    base = libobj_base();          /* reflection: no dlopen/dlsym */
    log_hex("NM_objbase=", base);
    if (!base)
        return;
'''),

    # --- constructor must not require dlsym to exist ---
    ('''    if (!dlsym) {
        out("NM_NO_DLSYM\\n", 12);
        return;
    }
''', ''),

    ('''    if (pthread_create)
        create = pthread_create;
    else
        create = (pthread_create_fn)dlsym(RTLD_DEFAULT, "pthread_create");
''',
     '''    if (pthread_create)
        create = pthread_create;
    else if (dlsym)
        create = (pthread_create_fn)dlsym(RTLD_DEFAULT, "pthread_create");
'''),
]


def main():
    s = open(P, encoding="utf-8").read()
    if "dlopen" not in s:
        print("already patched (no dlopen left)")
        return 0
    for i, (old, new) in enumerate(OLD):
        if old not in s:
            print("anchor %d missing:\n%s" % (i, old[:120]), file=sys.stderr)
            return 1
        s = s.replace(old, new, 1)
    open(P, "w", encoding="utf-8").write(s)
    print("step 3a applied: no dlopen() left in the payload")
    return 0


if __name__ == "__main__":
    sys.exit(main())
