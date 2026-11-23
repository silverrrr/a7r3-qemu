#!/usr/bin/env python3
"""Add journal support and an abort-without-rollback mode to publish.c."""
P = '/mnt/f/project/a7r3/analysis/payloads/publish.c'
s = open(P, encoding='utf-8').read()

# 1. include the journal helpers right after the SHA header
s = s.replace('#include "sha256_min.h"',
              '#include "sha256_min.h"\n#include "journal_min.h"\n'
              '#define SYS_getpid 20', 1)

# 2. small journal helpers before remount()
helpers = r'''
/* --- journal helpers (optional: only used when a journal dir is given) --- */
static int read_text(const char *path, char *buf, int cap) {
    long fd = sys3(SYS_open, (s32)path, O_RDONLY, 0);
    int n = 0;
    if (fd < 0) return -1;
    while (n < cap - 1) {
        long r = sys3(SYS_read, (s32)fd, (s32)(buf + n), (long)(cap - 1 - n));
        if (r <= 0) break;
        n += (int)r;
    }
    sys1(SYS_close, (s32)fd);
    buf[n] = 0;
    return n;
}
static void boot_id(char out[37]) {
    int i;
    for (i = 0; i < 36; i++) out[i] = 0;
    if (read_text("/proc/sys/kernel/random/boot_id", out, 37) < 0) {
        for (i = 0; i < 36; i++) out[i] = '0';
        out[36] = 0;
    }
    for (i = 0; i < 36; i++) {
        if (out[i] == '\n' || out[i] == 0) { out[i] = 0; break; }
    }
}
static int slot_file(char *buf, int cap, const char *dir, int second) {
    int i = 0;
    const char *a = "/state.a", *b = "/state.b";
    const char *suf = second ? b : a;
    if (!dir || !*dir) return -1;
    while (*dir && i < cap - 1) buf[i++] = *dir++;
    for (int k = 0; suf[k] && i < cap - 1; k++) buf[i++] = suf[k];
    buf[i] = 0;
    return 0;
}
static unsigned long journal_max_seq(const char *dir) {
    char path[256];
    u8 rec[J_BYTES];
    unsigned long best = 0, seq;
    int k;
    for (k = 0; k < 2; k++) {
        if (slot_file(path, sizeof path, dir, k)) continue;
        if (read_text(path, (char *)rec, J_BYTES) != J_BYTES) continue;
        if (j_check(rec, &seq, 0, 0, 0, 0) != 1) continue;
        if (seq > best) best = seq;
    }
    return best;
}
static int journal_write(const char *dir, unsigned long seq, u32 phase,
                         u32 code, const u8 old_sha[32], const u8 new_sha[32]) {
    char path[256], boot[37];
    u8 rec[J_BYTES];
    long fd;
    if (!dir || !*dir) return 0;
    if (slot_file(path, sizeof path, dir, (int)(seq & 1))) return -1;
    boot_id(boot);
    j_build(rec, seq, phase, code, (u32)sys1(SYS_getpid, 0), boot,
            old_sha, new_sha);
    fd = sys3(SYS_open, (s32)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (sys3(SYS_write, (s32)fd, (s32)rec, J_BYTES) != J_BYTES) {
        sys1(SYS_close, (s32)fd);
        return -1;
    }
    sys1(SYS_fsync, (s32)fd);
    sys1(SYS_close, (s32)fd);
    fsync_path_dir(dir);
    return 0;
}

static int remount(const char *mp, int ro) {
'''
s = s.replace('static int remount(const char *mp, int ro) {\n', helpers, 1)

# 3. usage / argc: allow 9..11 and parse journal dir
s = s.replace('''    if (argc != 9 && argc != 10) {
        out("PUB usage: <mount> <target> <retained> <standby> <source> "
            "<oldsha> <newsha> <size> [fail_after_stage]\\n");
        return STAGE_ARG;
    }
    if (argc == 10) {
        const char *p = argv[9];
        while (*p >= '0' && *p <= '9') {
            fail_after = fail_after * 10 + (*p++ - '0');
        }
        if (*p) return STAGE_ARG;
    }
''', '''    if (argc < 9 || argc > 11) {
        out("PUB usage: <mount> <target> <retained> <standby> <source> "
            "<oldsha> <newsha> <size> [fail_after_stage] [journal_dir]\\n");
        return STAGE_ARG;
    }
    if (argc >= 10) {
        const char *p = argv[9];
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') {
            fail_after = fail_after * 10 + (*p++ - '0');
        }
        if (*p) return STAGE_ARG;
        if (neg) fail_after = -fail_after;
    }
    journal_dir = (argc >= 11) ? argv[10] : 0;
''', 1)

# 4. declare journal_dir and seq locals
s = s.replace('''    int fail_after = 0;
    long link_rc;''', '''    int fail_after = 0;
    const char *journal_dir = 0;
    unsigned long jseq = 0;
    long link_rc;''', 1)

# 5. negative fail_after = abort without rollback
s = s.replace('#define MAYBE_FAIL(n) \\\n    do { \\\n        if (fail_after == (n)) { \\\n            say_stage("inject_fail", STAGE_ROLLBACK, -777); \\\n            goto rollback; \\\n        } \\\n    } while (0)',
              '#define MAYBE_FAIL(n) \\\n    do { \\\n        if (fail_after == (n)) { \\\n            say_stage("inject_fail", STAGE_ROLLBACK, -777); \\\n            goto rollback; \\\n        } \\\n        if (fail_after == -(n)) { \\\n            out("PUB abort_after_stage="); out_dec(n); \\\n            out(" (no rollback: simulating a power cut)\\n"); \\\n            sys1(SYS_exit, 200); \\\n        } \\\n    } while (0)')

# 6. journal STARTED before the link
s = s.replace('''    /* keep the mapped old inode reachable before replacing it */''',
              '''    if (journal_dir) {
        jseq = journal_max_seq(journal_dir) + 1;
        if (journal_write(journal_dir, jseq, J_STARTED, 0, want_old, want_new))
            say_stage("journal_started", STAGE_ROLLBACK, -1);
        else
            say_stage("journal_started", STAGE_ROLLBACK, 0);
    }

    /* keep the mapped old inode reachable before replacing it */''', 1)

# 7. journal ACCEPTED after the readonly check, FAILURE in rollback
s = s.replace('''    say_stage("readonly_check", STAGE_READONLY_CHECK, 0);
    out("PUB OK\\n");''', '''    say_stage("readonly_check", STAGE_READONLY_CHECK, 0);
    if (journal_dir)
        say_stage("journal_accepted", STAGE_ROLLBACK,
                  journal_write(journal_dir, jseq + 1, J_ACCEPTED, 0,
                                want_old, want_new));
    out("PUB OK\\n");''', 1)
s = s.replace('''    rc = remount(mp, 1);
    say_stage("rollback_remount_ro", STAGE_ROLLBACK, rc);
    return STAGE_ROLLBACK;''', '''    rc = remount(mp, 1);
    say_stage("rollback_remount_ro", STAGE_ROLLBACK, rc);
    if (journal_dir)
        say_stage("journal_failure", STAGE_ROLLBACK,
                  journal_write(journal_dir, jseq + 1, J_FAILURE, 1,
                                want_old, want_new));
    return STAGE_ROLLBACK;''', 1)

open(P, 'w', encoding='utf-8').write(s)
print('publish.c: journal + abort mode added')
