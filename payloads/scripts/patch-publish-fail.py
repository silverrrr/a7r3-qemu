#!/usr/bin/env python3
"""Add mode preservation and fail-after-stage injection to publish.c."""
P = '/mnt/f/project/a7r3/analysis/payloads/publish.c'
s = open(P, encoding='utf-8').read()

# 1. macro right before the run() definition
anchor = 'int run(int argc, char **argv) {'
macro = '''#define MAYBE_FAIL(n) \\
    do { \\
        if (fail_after == (n)) { \\
            say_stage("inject_fail", STAGE_ROLLBACK, -777); \\
            goto rollback; \\
        } \\
    } while (0)

int run(int argc, char **argv) {'''
assert anchor in s
s = s.replace(anchor, macro, 1)

# 2. preserve mode right after the standby copy succeeded
old = '''    staged = 1;
    rc = (int)hash_file(standby, got, &size);'''
new = '''    staged = 1;
    {
        int mode = target_mode(target);
        if (mode > 0) {
            int mrc = (int)sys3(SYS_chmod, (s32)standby, mode & 07777, 0);
            say_stage("mode", STAGE_MODE, mrc);
            if (mrc) goto rollback;
        }
    }
    MAYBE_FAIL(STAGE_STANDBY);
    rc = (int)hash_file(standby, got, &size);'''
assert old in s
s = s.replace(old, new, 1)

# 3. injections after the later stages
s = s.replace('''    say_stage("standby_hash", STAGE_STANDBY_HASH, 0);''',
              '''    say_stage("standby_hash", STAGE_STANDBY_HASH, 0);
    MAYBE_FAIL(STAGE_STANDBY_HASH);''', 1)
s = s.replace('''    linked = 1;
    say_stage("link", STAGE_LINK, 0);''',
              '''    linked = 1;
    say_stage("link", STAGE_LINK, 0);
    MAYBE_FAIL(STAGE_LINK);''', 1)
s = s.replace('''    renamed = 1; staged = 0;
    say_stage("rename", STAGE_RENAME, 0);''',
              '''    renamed = 1; staged = 0;
    say_stage("rename", STAGE_RENAME, 0);
    MAYBE_FAIL(STAGE_RENAME);''', 1)
s = s.replace('''    say_stage("fsync", STAGE_FSYNC, rc);
    if (rc) goto rollback;''',
              '''    say_stage("fsync", STAGE_FSYNC, rc);
    if (rc) goto rollback;
    MAYBE_FAIL(STAGE_FSYNC);''', 1)
s = s.replace('''    say_stage("verify", STAGE_VERIFY, 0);''',
              '''    say_stage("verify", STAGE_VERIFY, 0);
    MAYBE_FAIL(STAGE_VERIFY);''', 1)

open(P, 'w', encoding='utf-8').write(s)
print('publish.c: mode preservation + fail injection added')
