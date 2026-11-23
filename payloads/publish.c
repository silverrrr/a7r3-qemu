/* A7R3 device-side publisher for the read-only ext2 /usr.
 *
 * Freestanding ARM32 (no libc, raw syscalls) so it can run from /setting or
 * /tmp without depending on the firmware's loader.  Implements the invariant
 * from a7r3-boot-runtime:
 *
 *   preimage check -> remount rw -> stage standby on the SAME fs
 *   -> linkat(target, retained) -> renameat(standby, target) -> fsync(dir)
 *   -> verify -> remount ro -> verify EROFS
 *
 * and rolls back on any error: the retained hardlink is renamed back over the
 * target, so /usr never stays half-published.
 *
 * Usage:
 *   publish.elf <mount> <target> <retained> <standby> <source> \
 *               <old_sha256> <new_sha256> <size>
 *
 * <source> may live on another filesystem (it is copied into <standby>);
 * <standby> must be on the target filesystem because renameat cannot cross
 * filesystems.  Exit code = stage number (0 = success), every step logs a line.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u32l;
typedef long s32;
#define SHA_MIN_TYPES 1

#define NULL ((void *)0)

#define SYS_exit     1
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_fsync    118
#define SYS_mount    21
#define SYS_renameat 329
#define SYS_linkat   330
#define SYS_unlinkat 328
#define SYS_fstat64  197
#define SYS_chmod    15

#define AT_FDCWD      (-100)
#define O_RDONLY      0
#define O_WRONLY      1
#define O_TRUNC       0x200
#define O_CREAT       0x40
#define O_DIRECTORY   0x10000

#define MS_REMOUNT    32
#define MS_RDONLY     1

#define STAGE_ARG           1
#define STAGE_REMOUNT_RW    2
#define STAGE_PREIMAGE      3
#define STAGE_STANDBY       4
#define STAGE_STANDBY_HASH  5
#define STAGE_LINK          6
#define STAGE_RENAME        7
#define STAGE_FSYNC         8
#define STAGE_VERIFY        9
#define STAGE_REMOUNT_RO    10
#define STAGE_READONLY_CHECK 11
#define STAGE_ROLLBACK      12
#define STAGE_MODE          13

static s32 sys1(s32 n, s32 a) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}
static s32 sys3(s32 n, s32 a, s32 b, s32 c) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    register s32 r1 __asm__("r1") = b;
    register s32 r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2)
                     : "memory", "cc");
    return r0;
}
static s32 sys4(s32 n, s32 a, s32 b, s32 c, s32 d) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    register s32 r1 __asm__("r1") = b;
    register s32 r2 __asm__("r2") = c;
    register s32 r3 __asm__("r3") = d;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3)
                     : "memory", "cc");
    return r0;
}
static s32 sys5(s32 n, s32 a, s32 b, s32 c, s32 d, s32 e) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    register s32 r1 __asm__("r1") = b;
    register s32 r2 __asm__("r2") = c;
    register s32 r3 __asm__("r3") = d;
    register s32 r4 __asm__("r4") = e;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2),
                     "r"(r3), "r"(r4) : "memory", "cc");
    return r0;
}

static void *mem_set(void *p, int c, u32 n) {
    u8 *b = p;
    while (n--) *b++ = (u8)c;
    return p;
}
static void *mem_copy(void *d, const void *s, u32 n) {
    u8 *a = d; const u8 *b = s;
    while (n--) *a++ = *b++;
    return d;
}
static int mem_eq(const void *a, const void *b, u32 n) {
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}
static u32 str_len(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

void *memcpy(void *d, const void *s, unsigned n) { return mem_copy(d, s, n); }
void *memset(void *d, int c, unsigned n) { return mem_set(d, c, n); }
int memcmp(const void *a, const void *b, unsigned n) { return mem_eq(a, b, n) ? 0 : 1; }

static void out(const char *s) { sys3(SYS_write, 1, (s32)s, (s32)str_len(s)); }
static void out_dec(int v) {
    char b[12]; int i = 11; b[i] = 0;
    if (v < 0) { out("-"); v = -v; }
    if (v == 0) { out("0"); return; }
    while (v && i) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
    out(b + i);
}
static void out_sha(const u8 *h) {
    static const char hx[] = "0123456789abcdef";
    char b[65]; int i;
    for (i = 0; i < 32; i++) {
        b[2 * i] = hx[h[i] >> 4];
        b[2 * i + 1] = hx[h[i] & 15];
    }
    b[64] = 0;
    out(b);
}

#include "sha256_min.h"
#include "journal_min.h"
#define SYS_getpid 20

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int parse_sha(const char *hex, u8 out32[32]) {
    int i;
    if (str_len(hex) != 64) return -1;
    for (i = 0; i < 32; i++) {
        int hi = hex_val(hex[2 * i]), lo = hex_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out32[i] = (u8)((hi << 4) | lo);
    }
    return 0;
}
static long hash_file(const char *path, u8 out32[32], long *size) {
    u8 buf[4096];
    struct sha s;
    long fd, n, total = 0;
    fd = sys3(SYS_open, (s32)path, O_RDONLY, 0);
    if (fd < 0) return fd;
    sha_init(&s);
    for (;;) {
        n = sys3(SYS_read, (s32)fd, (s32)buf, sizeof buf);
        if (n < 0) { sys1(SYS_close, (s32)fd); return n; }
        if (n == 0) break;
        sha_add(&s, buf, (u32)n);
        total += n;
    }
    sys1(SYS_close, (s32)fd);
    sha_end(&s, out32);
    if (size) *size = total;
    return 0;
}

/* Copy src -> dst (dst created/truncated), fsync the dst before returning. */
static long copy_file(const char *src, const char *dst) {
    u8 buf[4096];
    long in, outfd, n;
    in = sys3(SYS_open, (s32)src, O_RDONLY, 0);
    if (in < 0) return in;
    outfd = sys3(SYS_open, (s32)dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) { sys1(SYS_close, (s32)in); return outfd; }
    for (;;) {
        n = sys3(SYS_read, (s32)in, (s32)buf, sizeof buf);
        if (n < 0) { sys1(SYS_close, (s32)in); sys1(SYS_close, (s32)outfd); return n; }
        if (n == 0) break;
        if (sys3(SYS_write, (s32)outfd, (s32)buf, n) != n) {
            sys1(SYS_close, (s32)in); sys1(SYS_close, (s32)outfd); return -5;
        }
    }
    sys1(SYS_fsync, (s32)outfd);
    sys1(SYS_close, (s32)in);
    sys1(SYS_close, (s32)outfd);
    return 0;
}

/* st_mode lives at offset 16 in the ARM EABI struct stat64. */
static int target_mode(const char *path) {
    u8 st[160];
    long fd = sys3(SYS_open, (s32)path, O_RDONLY, 0);
    int mode = 0;
    if (fd < 0) return 0;
    if (sys3(SYS_fstat64, (s32)fd, (s32)st, 0) == 0)
        mode = (int)st[16] | ((int)st[17] << 8) |
               ((int)st[18] << 16) | ((int)st[19] << 24);
    sys1(SYS_close, (s32)fd);
    return mode;
}


static int fsync_path_dir(const char *dir);

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
    long flags = MS_REMOUNT | (ro ? MS_RDONLY : 0);
    return (int)sys5(SYS_mount, 0, (s32)mp, 0, flags, 0);
}
static int is_readonly(const char *target) {
    long fd = sys3(SYS_open, (s32)target, O_WRONLY, 0);
    if (fd >= 0) { sys1(SYS_close, (s32)fd); return 0; }
    return (fd == -30) ? 1 : 0;      /* -EROFS */
}
static int fsync_path_dir(const char *dir) {
    long fd = sys3(SYS_open, (s32)dir, O_RDONLY | O_DIRECTORY, 0);
    int rc;
    if (fd < 0) return (int)fd;
    rc = (int)sys1(SYS_fsync, (s32)fd);
    sys1(SYS_close, (s32)fd);
    return rc;
}

static void say_stage(const char *name, int stage, int rc) {
    out("PUB stage="); out(name);
    out(" code="); out_dec(stage);
    out(" rc="); out_dec(rc); out("\n");
}

#define MAYBE_FAIL(n) \
    do { \
        if (fail_after == (n)) { \
            say_stage("inject_fail", STAGE_ROLLBACK, -777); \
            goto rollback; \
        } \
        if (fail_after == -(n)) { \
            out("PUB abort_after_stage="); out_dec(n); \
            out(" (no rollback: simulating a power cut)\n"); \
            sys1(SYS_exit, 200); \
        } \
    } while (0)

int run(int argc, char **argv) {
    const char *mp, *target, *retained, *standby, *source;
    u8 want_old[32], want_new[32], got[32];
    long want_size, size = 0;
    int renamed = 0, linked = 0, staged = 0;
    int fail_after = 0;
    const char *journal_dir = 0;
    const char *retained_use = retained;
    char retained_alt[256];
    unsigned long jseq = 0;
    long link_rc;
    int rc;

    /* read-only mode: publish.elf hash <path> */
    if (argc == 3 && argv[1][0] == 'h' && argv[1][1] == 'a') {
        u8 h[32];
        long n = 0;
        int rc2 = (int)hash_file(argv[2], h, &n);
        if (rc2) { out("PUB hash error "); out_dec(rc2); out("\n"); return 1; }
        out("HASH "); out_sha(h); out(" size="); out_dec((int)n); out("\n");
        return 0;
    }
    if (argc < 9 || argc > 11) {
        out("PUB usage: <mount> <target> <retained> <standby> <source> "
            "<oldsha> <newsha> <size> [fail_after_stage] [journal_dir]\n");
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
    mp = argv[1]; target = argv[2]; retained = argv[3];
    standby = argv[4]; source = argv[5];
    if (parse_sha(argv[6], want_old) || parse_sha(argv[7], want_new)) {
        out("PUB bad sha argument\n");
        return STAGE_ARG;
    }
    want_size = 0;
    {
        const char *p = argv[8];
        if (!*p) return STAGE_ARG;
        while (*p) {
            if (*p < '0' || *p > '9') return STAGE_ARG;
            want_size = want_size * 10 + (*p++ - '0');
        }
    }

    rc = remount(mp, 0);
    say_stage("remount_rw", STAGE_REMOUNT_RW, rc);
    if (rc && rc != -16) return STAGE_REMOUNT_RW;   /* -16 = EBUSY: already rw */

    /* preimage: target must still be the exact bytes we were built against */
    rc = (int)hash_file(target, got, &size);
    if (rc || size != want_size || !mem_eq(got, want_old, 32)) {
        say_stage("preimage", STAGE_PREIMAGE, rc ? rc : -1);
        out("PUB target sha="); out_sha(got); out(" size="); out_dec((int)size);
        out("\n");
        remount(mp, 1);
        return STAGE_PREIMAGE;
    }
    say_stage("preimage", STAGE_PREIMAGE, 0);

    /* stage the new bytes on the target filesystem (renameat cannot cross it) */
    rc = (int)copy_file(source, standby);
    say_stage("standby", STAGE_STANDBY, rc);
    if (rc) goto rollback;
    staged = 1;
    {
        int mode = target_mode(target);
        if (mode > 0) {
            int mrc = (int)sys3(SYS_chmod, (s32)standby, mode & 07777, 0);
            say_stage("mode", STAGE_MODE, mrc);
            if (mrc) goto rollback;
        }
    }
    MAYBE_FAIL(STAGE_STANDBY);
    rc = (int)hash_file(standby, got, &size);
    if (rc || size != want_size || !mem_eq(got, want_new, 32)) {
        say_stage("standby_hash", STAGE_STANDBY_HASH, rc ? rc : -1);
        goto rollback;
    }
    say_stage("standby_hash", STAGE_STANDBY_HASH, 0);
    MAYBE_FAIL(STAGE_STANDBY_HASH);

    if (journal_dir) {
        jseq = journal_max_seq(journal_dir) + 1;
        if (journal_write(journal_dir, jseq, J_STARTED, 0, want_old, want_new))
            say_stage("journal_started", STAGE_ROLLBACK, -1);
        else
            say_stage("journal_started", STAGE_ROLLBACK, 0);
    }

    /* keep the mapped old inode reachable before replacing it */
    /* linkat takes five arguments (olddirfd, oldpath, newdirfd, newpath, flags) */
    link_rc = sys5(SYS_linkat, AT_FDCWD, (s32)target, AT_FDCWD,
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
    }
    linked = 1;
    say_stage("link", STAGE_LINK, 0);
    MAYBE_FAIL(STAGE_LINK);

    if (sys4(SYS_renameat, AT_FDCWD, (s32)standby, AT_FDCWD, (s32)target) < 0) {
        say_stage("rename", STAGE_RENAME, -1);
        goto rollback;
    }
    renamed = 1; staged = 0;
    say_stage("rename", STAGE_RENAME, 0);
    MAYBE_FAIL(STAGE_RENAME);

    rc = fsync_path_dir(mp);
    say_stage("fsync", STAGE_FSYNC, rc);
    if (rc) goto rollback;
    MAYBE_FAIL(STAGE_FSYNC);

    rc = (int)hash_file(target, got, &size);
    if (rc || size != want_size || !mem_eq(got, want_new, 32)) {
        say_stage("verify", STAGE_VERIFY, rc ? rc : -1);
        goto rollback;
    }
    say_stage("verify", STAGE_VERIFY, 0);
    MAYBE_FAIL(STAGE_VERIFY);

    rc = remount(mp, 1);
    say_stage("remount_ro", STAGE_REMOUNT_RO, rc);
    if (rc) goto rollback;
    if (!is_readonly(target)) {
        say_stage("readonly_check", STAGE_READONLY_CHECK, -1);
        return STAGE_READONLY_CHECK;
    }
    say_stage("readonly_check", STAGE_READONLY_CHECK, 0);
    if (journal_dir)
        say_stage("journal_accepted", STAGE_ROLLBACK,
                  journal_write(journal_dir, jseq + 1, J_ACCEPTED, 0,
                                want_old, want_new));
    out("PUB OK\n");
    return 0;

rollback:
    if (renamed) {
        rc = (int)sys4(SYS_renameat, AT_FDCWD, (s32)retained_use, AT_FDCWD,
                       (s32)target);
        say_stage("rollback_rename", STAGE_ROLLBACK, rc);
        fsync_path_dir(mp);
        renamed = 0; linked = 0;
    }
    if (staged) {
        sys3(SYS_unlinkat, AT_FDCWD, (s32)standby, 0);
        staged = 0;
    }
    if (linked) {
        sys3(SYS_unlinkat, AT_FDCWD, (s32)retained_use, 0);
        linked = 0;
    }
    rc = remount(mp, 1);
    say_stage("rollback_remount_ro", STAGE_ROLLBACK, rc);
    if (journal_dir)
        say_stage("journal_failure", STAGE_ROLLBACK,
                  journal_write(journal_dir, jseq + 1, J_FAILURE, 1,
                                want_old, want_new));
    return STAGE_ROLLBACK;
}

int run(int argc, char **argv) __attribute__((noinline));

/* Naked entry: never let the compiler build a stack frame before we read the
 * kernel's initial stack (argc at [sp], argv at sp+4). */
__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "ldr r0, [sp]\n"
        "add r1, sp, #4\n"
        "bl  run\n"
        "mov r7, #1\n"
        "svc #0\n"
        "1: b 1b\n");
}
