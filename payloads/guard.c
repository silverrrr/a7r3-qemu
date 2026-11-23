/* A7R3 install guard: fail-closed rollback driven by the dual-slot journal.
 *
 * Reads <journal_dir>/state.a and state.b.  If the newest record is
 *   ACCEPTED  -> nothing to do
 *   FAILURE   -> nothing to do (already rolled back)
 *   STARTED   -> wait up to <wait_seconds> for <journal_dir>/ready (written by
 *                the resident component once the app is healthy); if it does
 *                not appear, restore the stock file and write FAILURE.
 *
 * Usage:
 *   guard.elf <journal_dir> <wait_seconds> <mount> <target> <retained>
 *             <standby> <stock> <stock_sha256> <stock_size>
 *
 * Freestanding ARM32, raw syscalls only.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u32l;
typedef long s32;

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
#define SYS_nanosleep 162

#define AT_FDCWD      (-100)
#define O_RDONLY      0
#define O_WRONLY      1
#define O_TRUNC       0x200
#define O_CREAT       0x40
#define O_DIRECTORY   0x10000

#define MS_REMOUNT    32
#define MS_RDONLY     1

#include "sha256_min.h"
#include "journal_min.h"

static s32 sys1(s32 n, s32 a) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}
static s32 sys2(s32 n, s32 a, s32 b) {
    register s32 r7 __asm__("r7") = n;
    register s32 r0 __asm__("r0") = a;
    register s32 r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory", "cc");
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
static void say(const char *stage, int rc) {
    out("GUARD stage="); out(stage); out(" rc="); out_dec(rc); out("\n");
}

static int join(char *buf, int cap, const char *a, const char *b) {
    int i = 0;
    while (a && *a && i < cap - 1) buf[i++] = *a++;
    while (b && *b && i < cap - 1) buf[i++] = *b++;
    buf[i] = 0;
    return 0;
}
static long read_all(const char *path, void *buf, long cap) {
    long fd = sys3(SYS_open, (s32)path, O_RDONLY, 0), n = 0;
    if (fd < 0) return -1;
    while (n < cap) {
        long r = sys3(SYS_read, (s32)fd, (s32)((u8 *)buf + n), cap - n);
        if (r <= 0) break;
        n += r;
    }
    sys1(SYS_close, (s32)fd);
    return n;
}
static int file_exists(const char *path) {
    long fd = sys3(SYS_open, (s32)path, O_RDONLY, 0);
    if (fd < 0) return 0;
    sys1(SYS_close, (s32)fd);
    return 1;
}
static int fsync_dir(const char *dir) {
    long fd = sys3(SYS_open, (s32)dir, O_RDONLY | O_DIRECTORY, 0);
    int rc;
    if (fd < 0) return (int)fd;
    rc = (int)sys1(SYS_fsync, (s32)fd);
    sys1(SYS_close, (s32)fd);
    return rc;
}
static int remount(const char *mp, int ro) {
    return (int)sys5(SYS_mount, 0, (s32)mp, 0, MS_REMOUNT | (ro ? MS_RDONLY : 0), 0);
}
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
static long copy_file(const char *src, const char *dst) {
    u8 buf[4096];
    long in = sys3(SYS_open, (s32)src, O_RDONLY, 0), outfd, n;
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
static int hash_file(const char *path, u8 out32[32]) {
    u8 buf[4096];
    struct sha s;
    long fd = sys3(SYS_open, (s32)path, O_RDONLY, 0), n;
    if (fd < 0) return (int)fd;
    sha_init(&s);
    for (;;) {
        n = sys3(SYS_read, (s32)fd, (s32)buf, sizeof buf);
        if (n < 0) { sys1(SYS_close, (s32)fd); return (int)n; }
        if (n == 0) break;
        sha_add(&s, buf, (u32)n);
    }
    sys1(SYS_close, (s32)fd);
    sha_end(&s, out32);
    return 0;
}
/* Read both slots.  Returns the newest seq; rec[0]/rec[1] keep the raw records
 * so the caller can inspect phase, seq and boot id of both. */
struct slot {
    int valid;
    unsigned long seq;
    u32 phase;
    char boot[37];
};

static void slot_boot(const u8 *rec, char out[37]) {
    int i;
    for (i = 0; i < 36; i++) out[i] = (char)rec[36 + i];
    out[36] = 0;
}

static unsigned long read_slots(const char *dir, struct slot s[2]) {
    char path[256];
    u8 rec[J_BYTES];
    unsigned long best = 0;
    int k;
    for (k = 0; k < 2; k++) {
        s[k].valid = 0;
        s[k].seq = 0;
        s[k].phase = 0;
        s[k].boot[0] = 0;
        join(path, sizeof path, dir, k ? "/state.b" : "/state.a");
        if (read_all(path, rec, J_BYTES) != J_BYTES) continue;
        if (j_check(rec, &s[k].seq, &s[k].phase, 0, 0, 0) != 1) continue;
        s[k].valid = 1;
        slot_boot(rec, s[k].boot);
        if (s[k].seq > best) best = s[k].seq;
    }
    return best;
}

static int boot_equal(const char *a, const char *b) {
    int i;
    for (i = 0; i < 36; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static void current_boot(char out[37]) {
    int i;
    for (i = 0; i < 36; i++) out[i] = 0;
    if (read_all("/proc/sys/kernel/random/boot_id", out, 37) < 0) {
        for (i = 0; i < 36; i++) out[i] = '0';
    }
    for (i = 0; i < 36; i++)
        if (out[i] == '\n' || out[i] == 0) { out[i] = 0; break; }
}

static int write_slot(const char *dir, unsigned long seq, u32 phase, u32 code) {
    char path[256], boot[37];
    u8 rec[J_BYTES];
    long fd;
    int i;
    join(path, sizeof path, dir, (seq & 1) ? "/state.b" : "/state.a");
    for (i = 0; i < 36; i++) boot[i] = 0;
    if (read_all("/proc/sys/kernel/random/boot_id", boot, 37) < 0) {
        for (i = 0; i < 36; i++) boot[i] = '0';
    }
    for (i = 0; i < 36; i++) if (boot[i] == '\n' || boot[i] == 0) { boot[i] = 0; break; }
    j_build(rec, seq, phase, code, 0, boot, 0, 0);
    fd = sys3(SYS_open, (s32)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (sys3(SYS_write, (s32)fd, (s32)rec, J_BYTES) != J_BYTES) {
        sys1(SYS_close, (s32)fd);
        return -1;
    }
    sys1(SYS_fsync, (s32)fd);
    sys1(SYS_close, (s32)fd);
    fsync_dir(dir);
    return 0;
}

int run(int argc, char **argv) __attribute__((noinline));

int run(int argc, char **argv) {
    const char *jdir, *mount, *target, *retained, *standby, *stock;
    u8 want_stock[32], got[32];
    unsigned long seq;
    u32 phase = 0, code = 0;
    int found = 0, waited = 0, wait_max, want_mode = 0;
    char ready[256], retained2[256];
    int rc;

    if (argc != 8 && argc != 9) {
        out("GUARD usage: <journal> <wait_s> <mount> <target> <retained> "
            "<standby> <stock> [mode]\n");
        return 2;
    }
    jdir = argv[1];
    wait_max = 0;
    { const char *p = argv[2]; while (*p >= '0' && *p <= '9') wait_max = wait_max * 10 + (*p++ - '0'); }
    mount = argv[3]; target = argv[4]; retained = argv[5];
    standby = argv[6]; stock = argv[7];
    if (argc == 9) {
        const char *p = argv[8];
        while (*p >= '0' && *p <= '7') want_mode = want_mode * 8 + (*p++ - '0');
        if (*p) return 2;
    }

    /* the expected hash is the hash of the stock copy itself, so callers do not
     * have to carry it around */
    rc = (int)hash_file(stock, want_stock);
    if (rc) { say("stock_hash", rc); return 2; }

    {
        struct slot s[2];
        struct slot *latest, *prev = 0;
        char now_boot[37];
        int retry_ok = 0;

        seq = read_slots(jdir, s);
        if (s[0].valid && s[1].valid) {
            latest = (s[0].seq >= s[1].seq) ? &s[0] : &s[1];
            prev = (latest == &s[0]) ? &s[1] : &s[0];
        } else if (s[0].valid) {
            latest = &s[0];
        } else if (s[1].valid) {
            latest = &s[1];
        } else {
            say("no_journal", 0);
            return 0;
        }
        found = 1;
        phase = latest->phase;
        say(phase == J_ACCEPTED ? "accepted" : phase == J_FAILURE ? "failure"
                                                                  : "started", 0);
        if (phase == J_ACCEPTED || phase == J_FAILURE) return 0;

        /* Narrowed retry: only if the previous record was ACCEPTED, the newest
         * STARTED is its direct successor, and all three boot ids differ. */
        if (prev && prev->valid && prev->phase == J_ACCEPTED &&
            latest->seq == prev->seq + 1) {
            current_boot(now_boot);
            retry_ok = !boot_equal(prev->boot, latest->boot) &&
                       !boot_equal(now_boot, latest->boot) &&
                       !boot_equal(now_boot, prev->boot);
        }
        say(retry_ok ? "retry_allowed" : "retry_denied", 0);
        if (!retry_ok)
            wait_max = 0;   /* fail-closed: roll back right away */
    }

    join(ready, sizeof ready, jdir, "/ready");
    while (waited < wait_max) {
        if (file_exists(ready)) break;
        {
            struct timespec32 { long tv_sec; long tv_nsec; } ts;
            ts.tv_sec = 1;
            ts.tv_nsec = 0;
            sys2(SYS_nanosleep, (s32)&ts, 0);
        }
        waited++;
    }
    if (waited < wait_max) {
        out("GUARD ready_seen\n");
        rc = remount(mount, 1);
        say("accept_remount_ro", rc);
        rc = write_slot(jdir, seq + 1, J_ACCEPTED, 0);
        say("accept", rc);
        return rc ? 1 : 0;
    }

    /* no ready barrier: restore the stock file, keeping the old inode alive */
    rc = remount(mount, 0);
    say("remount_rw", rc);
    if (rc && rc != -16) return 1;
    /* Never unlink a stale retained alias here: it may be the only reference to
     * the inode im.elf still maps, and dropping it to nlink 0 would make the
     * later MS_RDONLY remount fail EBUSY.  Use a separate alias instead. */
    join(retained2, sizeof retained2, retained, ".rb");
    sys3(SYS_unlinkat, AT_FDCWD, (s32)retained2, 0);   /* our own leftover */
    rc = (int)copy_file(stock, standby);
    say("standby", rc);
    if (rc) goto fail;
    {
        int mode = want_mode ? want_mode : target_mode(target);
        if (mode > 0) sys3(SYS_chmod, (s32)standby, mode & 07777, 0);
    }
    if (sys5(SYS_linkat, AT_FDCWD, (s32)target, AT_FDCWD, (s32)retained2, 0) < 0) {
        say("link", -1);
        goto fail;
    }
    if (sys4(SYS_renameat, AT_FDCWD, (s32)standby, AT_FDCWD, (s32)target) < 0) {
        say("rename", -1);
        goto fail;
    }
    fsync_dir(mount);
    rc = (int)hash_file(target, got);
    if (rc || !mem_eq(got, want_stock, 32)) { say("verify", -1); goto fail; }
    say("verify", 0);
    rc = remount(mount, 1);
    say("remount_ro", rc);
    rc = write_slot(jdir, seq + 1, J_FAILURE, 1);
    say("journal_failure", rc);
    out("GUARD ROLLED_BACK\n");
    return 0;

fail:
    sys3(SYS_unlinkat, AT_FDCWD, (s32)standby, 0);
    remount(mount, 1);
    say("failed", 1);
    return 1;
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "ldr r0, [sp]\n"
        "add r1, sp, #4\n"
        "bl  run\n"
        "mov r7, #1\n"
        "svc #0\n"
        "1: b 1b\n");
}
