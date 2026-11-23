#!/usr/bin/env python3
"""Publisher: fall back to a second retained name when the first one exists."""
P = '/mnt/f/project/a7r3/analysis/payloads/publish.c'
s = open(P, encoding='utf-8').read()

s = s.replace("    const char *journal_dir = 0;\n",
              "    const char *journal_dir = 0;\n"
              "    const char *retained_use = retained;\n"
              "    char retained_alt[256];\n", 1)

old = '''    link_rc = sys5(SYS_linkat, AT_FDCWD, (s32)target, AT_FDCWD,
                   (s32)retained, 0);
    if (link_rc < 0) {
        say_stage("link", STAGE_LINK, (int)link_rc);
        goto rollback;
    }'''
new = '''    link_rc = sys5(SYS_linkat, AT_FDCWD, (s32)target, AT_FDCWD,
                   (s32)retained, 0);
    if (link_rc == -17) {
        /* A stale alias from an interrupted run must not be removed: it may be
         * the last reference to the inode im.elf maps.  Use a second name. */
        int n = 0;
        const char *r = retained;
        while (*r && n < 246) retained_alt[n++] = *r++;
        retained_alt[n++] = '.'; retained_alt[n++] = 'a';
        retained_alt[n++] = 'l'; retained_alt[n++] = 't';
        retained_alt[n] = 0;
        retained_use = retained_alt;
        link_rc = sys5(SYS_linkat, AT_FDCWD, (s32)target, AT_FDCWD,
                       (s32)retained_use, 0);
    }
    if (link_rc < 0) {
        say_stage("link", STAGE_LINK, (int)link_rc);
        goto rollback;
    }'''
assert old in s, 'link block not found'
s = s.replace(old, new, 1)

old2 = '''        rc = (int)sys4(SYS_renameat, AT_FDCWD, (s32)retained, AT_FDCWD,
                       (s32)target);'''
new2 = '''        rc = (int)sys4(SYS_renameat, AT_FDCWD, (s32)retained_use, AT_FDCWD,
                       (s32)target);'''
assert old2 in s, 'rollback rename not found'
s = s.replace(old2, new2, 1)

old3 = '''    if (linked) {
        sys3(SYS_unlinkat, AT_FDCWD, (s32)retained, 0);
        linked = 0;
    }'''
new3 = '''    if (linked) {
        sys3(SYS_unlinkat, AT_FDCWD, (s32)retained_use, 0);
        linked = 0;
    }'''
assert old3 in s, 'unlink block not found'
s = s.replace(old3, new3, 1)

open(P, 'w', encoding='utf-8').write(s)
print('publish.c: retained EEXIST fallback applied')
