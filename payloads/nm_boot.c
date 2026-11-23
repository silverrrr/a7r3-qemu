/* nm_boot.c -- tiny preload stub that loads the real overlay from a
 * configurable path (e.g. the SD card) and is completely inert otherwise.
 *
 * Why: on the A7R3 nothing in the boot chain touches the SD card before im.elf
 * runs, so an LD_PRELOAD entry can never point at the card directly.  What *is*
 * possible is a two stage load:
 *
 *   /usr/tool/nm_boot.so        (few KB, installed on internal storage)
 *     -> waits for the payload path to appear (the card is mounted later)
 *     -> dlopen()s it; if it is missing or broken, does nothing at all
 *
 * Pull the card (or create /setting/nm_off) and the camera boots the stock UI,
 * because this stub has no UI code of its own.
 *
 * Config:  /setting/nm_payload.conf  -- first line is the payload path
 *          (default /setting/newmenu.so, i.e. "fake SD" under QEMU)
 * Switch:  /setting/nm_off           -- exists => never load
 *
 * Crash history (2026-11-15):
 *   v1 called dlsym() from the preload constructor -> `page translation fault
 *   at 0x00000000` (the weak symbol is still NULL that early, so the call jumps
 *   to 0).  v2 blocked the constructor for 5 s and still faulted (holding up
 *   the loader is not acceptable either).  v3 uses a thread created through a
 *   *weak direct reference* to pthread_create, guarded for NULL, and never
 *   calls dlsym at all.
 *
 * Logs to /tmp/nm_boot.log and to the console (fd 1) while booting.
 */
typedef unsigned long usize;
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long u32l;

#include <stddef.h>
#include <stdint.h>

extern void *dlopen(const char *path, int flags) __attribute__((weak));
extern int pthread_create(unsigned long *tid, const void *attr,
                          void *(*fn)(void *), void *arg)
    __attribute__((weak));

#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_read 3
#define SYS_nanosleep 162
#define SYS_fork 2
#define SYS_execve 11
#define SYS_exit 1
#define SHA_MIN_TYPES 1
#include "journal_min.h"

#define O_TRUNC_CREAT 0x241

struct timespec32 {
    long tv_sec;
    long tv_nsec;
};

static long sys2(long nr, long a, long b)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long sys1(long nr, long a)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys4(long nr, long a, long b, long c, long d)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3)
                     : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

/* ------------------------------------------------------------------ */

static int g_log = -1;
static int g_log_tried;

static int log_open(void)
{
    if (g_log >= 0)
        return g_log;
    if (g_log_tried)
        return g_log;
    g_log_tried = 1;
    g_log = (int)sys3(SYS_open, (long)"/tmp/nm_boot.log", O_TRUNC_CREAT, 0644);
    return g_log;
}

static void out(const char *s, int n)
{
    /* console first: during boot this is where a preload constructor crash
     * becomes visible */
    sys3(SYS_write, 1, (long)s, n);
    if (log_open() >= 0)
        sys3(SYS_write, g_log, (long)s, n);
}

static void log_hex(const char *prefix, unsigned long v)
{
    char b[48];
    char t[16];
    int n = 0, i = 0;
    while (*prefix && n < 24)
        b[n++] = *prefix++;
    b[n++] = '0';
    b[n++] = 'x';
    if (!v) {
        b[n++] = '0';
    } else {
        while (v && i < 16) {
            t[i++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        }
        while (i > 0)
            b[n++] = t[--i];
    }
    b[n++] = '\n';
    out(b, n);
}

static void log_str(const char *s)
{
    int n = 0;
    while (s[n] && n < 120)
        n++;
    out(s, n);
}

static int file_exists(const char *path)
{
    long fd = sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0)
        return 0;
    sys2(SYS_close, fd, 0);
    return 1;
}

static void sleep_ms(unsigned ms)
{
    struct timespec32 ts;
    ts.tv_sec = (long)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    sys2(SYS_nanosleep, (long)&ts, 0);
}

static int comm_is_im(void)
{
    char buf[32];
    long fd = sys3(SYS_open, (long)"/proc/self/comm", 0, 0);
    long n;
    if (fd < 0)
        return 0;
    n = sys3(SYS_read, fd, (long)buf, (long)sizeof(buf) - 1);
    sys2(SYS_close, fd, 0);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    return buf[0] == 'i' && buf[1] == 'm' && buf[2] == '.' &&
           buf[3] == 'e' && buf[4] == 'l' && buf[5] == 'f';
}

/* first line of /setting/nm_payload.conf, trimmed; 1 on success */
static int read_conf(char *dst, int cap)
{
    char buf[160];
    long fd, n;
    int i = 0, k;

    fd = sys3(SYS_open, (long)"/setting/nm_payload.conf", 0, 0);
    if (fd < 0)
        return 0;
    n = sys3(SYS_read, fd, (long)buf, (long)sizeof(buf) - 1);
    sys2(SYS_close, fd, 0);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    while (i < n && i < cap - 1 && buf[i] != '\n' && buf[i] != '\r' &&
           buf[i] != ' ')
        i++;
    if (i == 0)
        return 0;
    for (k = 0; k < i; k++)
        dst[k] = buf[k];
    dst[i] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */

#define DEFAULT_PATH "/setting/newmenu.so"
#define FIRST_WAIT_MS 5000u
#define WAIT_MS 20000u
#define STEP_MS 500u

/* Read the two journal slots; 1 when the newest record is STARTED. */
static int journal_started(void)
{
    static const char *const names[2] = { "/setting/inst/state.a",
                                          "/setting/inst/state.b" };
    u8 rec[J_BYTES];
    int k, found = 0;
    unsigned long best = 0, seq;
    u32 phase = 0, code;

    for (k = 0; k < 2; k++) {
        long fd = sys3(SYS_open, (long)names[k], 0, 0), n = 0;
        if (fd < 0)
            continue;
        while (n < J_BYTES) {
            long r = sys3(SYS_read, fd, (long)((u8 *)rec + n), J_BYTES - n);
            if (r <= 0)
                break;
            n += r;
        }
        sys2(SYS_close, fd, 0);
        if (n != J_BYTES)
            continue;
        if (j_check(rec, &seq, &phase, &code, 0, 0) != 1)
            continue;
        if (!found || seq > best) {
            best = seq;
            found = 1;
        }
    }
    if (!found)
        return 0;
    return phase == J_STARTED;
}

/* The guard runs as its own process so this constructor is never blocked. */
static void maybe_guard(void)
{
    static char *argv[] = {
        (char *)"/setting/guard.elf", (char *)"/setting/inst", (char *)"20",
        (char *)"/usr", (char *)"/usr/lib/libIMDB.so",
        (char *)"/usr/lib/libIMDB.so.retained",
        (char *)"/usr/lib/.libIMDB.standby",
        (char *)"/setting/inst/stock.so", (char *)0
    };
    static char *envp[] = { (char *)0 };
    long pid;

    if (!journal_started()) {
        out("NM_BOOT journal ok\n", 20);
        return;
    }
    out("NM_BOOT journal STARTED -> guard\n", 33);
    pid = sys1(SYS_fork, 0);
    if (pid == 0) {
        sys3(SYS_execve, (long)argv[0], (long)argv, (long)envp);
        sys1(SYS_exit, 127);
    }
    if (pid > 0)
        log_hex("NM_BOOT guard_pid=", (unsigned long)pid);
    else
        out("NM_BOOT fork fail\n", 18);
}

static void load_payload(void)
{
    char path[128];
    unsigned waited = 0;
    void *handle;
    int i;

    if (!read_conf(path, (int)sizeof(path))) {
        for (i = 0; DEFAULT_PATH[i] && i < (int)sizeof(path) - 1; i++)
            path[i] = DEFAULT_PATH[i];
        path[i] = 0;
    }
    log_str("NM_BOOT wait ");
    log_str(path);
    log_str("\n");

    sleep_ms(FIRST_WAIT_MS);
    /* the card is mounted by im.elf itself, i.e. after this constructor ran */
    while (!file_exists(path) && waited < WAIT_MS) {
        sleep_ms(STEP_MS);
        waited += STEP_MS;
    }
    if (!file_exists(path)) {
        out("NM_BOOT absent (inert)\n", 24);
        return;
    }
    handle = dlopen(path, 2 /* RTLD_NOW */);
    log_hex("NM_BOOT dlopen=", (unsigned long)handle);
    if (!handle) {
        out("NM_BOOT failed (inert)\n", 24);
        return;
    }
    out("NM_BOOT loaded\n", 15);
}

static void *loader_entry(void *arg)
{
    (void)arg;
    maybe_guard();
    load_payload();
    return NULL;
}

__attribute__((constructor)) static void nm_boot_init(void)
{
    unsigned long tid = 0;

    if (!comm_is_im())
        return;
    out("NM_BOOT alive\n", 14);
    if (file_exists("/setting/nm_off")) {
        out("NM_BOOT disabled\n", 17);
        return;
    }
    if (!dlopen || !pthread_create) {
        out("NM_BOOT nolib\n", 14);
        return;
    }
    if (pthread_create(&tid, NULL, loader_entry, NULL) != 0) {
        out("NM_BOOT thread fail\n", 20);
        return;
    }
    log_hex("NM_BOOT tid=", tid);
}
