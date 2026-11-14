/* Rootfs /etc/ld.so.preload hook for the A7R3 silent-shutter investigation.
 *
 * Loaded into every dynamic process, but inert unless /proc/self/comm is
 * im.elf.  Logs ViewBaseProduct::PlayBeep(int) and LkmBeepIF::ModelExecute
 * calls to /setting/hook.log; both are forwarded to the original.
 *
 * ModelExecute is the real cross-library entry: viewUnified2.so imports it
 * from libObj.so, while PlayBeep has no static callers.  The first eight
 * dwords of its ParamList are dumped to locate the beep id.
 *
 * /rootfs is mounted read-only at runtime, so the preload entry cannot be
 * removed from here; delete it in service mode after collecting the log.
 */

typedef unsigned long usize;

extern void *dlsym(void *handle, const char *name) __attribute__((weak));
#define RTLD_NEXT ((void *)-1L)

#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_unlink 10
#define SYS_getpid 20

#define O_APPEND_CREAT 0x441

static volatile int active;
static volatile int resolving;
static int log_fd = -1;

static long sys3(long nr, long a, long b, long c)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7)
                     : "memory", "cc");
    return r0;
}

static usize slen(const char *s)
{
    usize n = 0;
    while (s[n])
        n++;
    return n;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int log_open(void)
{
    static const char *paths[] = {
        "/setting/hook.log",
        "/log/hook.log",
        "/tmp/hook.log",
        0
    };
    int i;
    if (log_fd >= 0)
        return log_fd;
    for (i = 0; paths[i]; i++) {
        int fd = (int)sys3(SYS_open, (long)paths[i], O_APPEND_CREAT, 0644);
        if (fd >= 0) {
            log_fd = fd;
            return fd;
        }
    }
    return -1;
}

static void out(const char *s, int n)
{
    int fd;
    if (!active)
        return;
    sys3(SYS_write, 1, (long)s, n);
    fd = log_open();
    if (fd >= 0)
        sys3(SYS_write, fd, (long)s, n);
}

static void log_str(const char *s)
{
    out(s, (int)slen(s));
}

static void log_int(const char *prefix, int v)
{
    char buf[48];
    char tmp[12];
    int n = 0;
    int i = 0;
    unsigned int u;

    while (*prefix && n < 32)
        buf[n++] = *prefix++;
    if (v < 0) {
        buf[n++] = '-';
        u = (unsigned int)(-v);
    } else {
        u = (unsigned int)v;
    }
    do {
        tmp[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u && i < (int)sizeof(tmp));
    while (i > 0 && n < 46)
        buf[n++] = tmp[--i];
    buf[n++] = '\n';
    out(buf, n);
}

static void log_hex(const char *prefix, unsigned int v)
{
    char buf[32];
    char tmp[8];
    int n = 0;
    int i = 0;

    while (*prefix && n < 12)
        buf[n++] = *prefix++;
    buf[n++] = '0';
    buf[n++] = 'x';
    if (!v) {
        buf[n++] = '0';
    } else {
        while (v) {
            tmp[i++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        }
        while (i > 0 && n < 30)
            buf[n++] = tmp[--i];
    }
    buf[n++] = '\n';
    out(buf, n);
}

static void log_words(const char *prefix, const volatile unsigned int *p,
                      int count)
{
    char buf[160];
    int n = 0;
    int i;
    int j;

    while (*prefix && n < 16)
        buf[n++] = *prefix++;
    for (i = 0; i < count && n < 150; i++) {
        unsigned int v = p[i];
        buf[n++] = ' ';
        for (j = 7; j >= 0; j--)
            buf[n++] = "0123456789abcdef"[(v >> (j * 4)) & 15];
    }
    buf[n++] = '\n';
    out(buf, n);
}

static int comm_is_target(void)
{
    char buf[64];
    int f;
    int n;

    f = (int)sys3(SYS_open, (long)"/proc/self/comm", 0, 0);
    if (f < 0)
        return 0;
    n = (int)sys3(SYS_read, f, (long)buf, sizeof(buf) - 1);
    sys3(SYS_close, f, 0, 0);
    if (n <= 0)
        return 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '))
        n--;
    buf[n] = 0;
    return same(buf, "im.elf");
}

__attribute__((constructor))
static void hook_root_init(void)
{
    long rc;

    if (!comm_is_target())
        return;

    rc = sys3(SYS_unlink, (long)"/etc/ld.so.preload", 0, 0);
    active = 1;
    log_str("HOOK_ROOT comm=im.elf\n");
    log_int("pid=", (int)sys3(SYS_getpid, 0, 0, 0));
    log_int("unlink_rc=", (int)rc);
}

typedef int (*playbeep_t)(void *, int);
static playbeep_t real_playbeep;

int _ZN15ViewBaseProduct8PlayBeepEi(void *self, int id)
{
    playbeep_t real = real_playbeep;

    if (!real && dlsym && !resolving) {
        resolving = 1;
        real = (playbeep_t)dlsym(RTLD_NEXT, "_ZN15ViewBaseProduct8PlayBeepEi");
        real_playbeep = real;
        resolving = 0;
    }
    if (active) {
        log_int("PlayBeep id=", id);
        if (!real)
            log_str("PlayBeep no-forward\n");
    }
    if (real)
        return real(self, id);
    return 0;
}

typedef int (*model_exec_t)(void *, const char *, int, void *);
static model_exec_t real_model_exec;

int _ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList(void *self, const char *cmd,
                                                int n, void *params)
{
    model_exec_t real = real_model_exec;

    if (!real && dlsym && !resolving) {
        resolving = 1;
        real = (model_exec_t)dlsym(RTLD_NEXT,
                                   "_ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList");
        real_model_exec = real;
        resolving = 0;
    }
    if (active) {
        log_str("ModelExecute cmd=");
        log_str(cmd ? cmd : "(null)");
        log_int(" n=", n);
        log_hex("params=", (unsigned int)params);
        if ((unsigned int)params >= 0x10000 && ((unsigned int)params & 3) == 0)
            log_words("words=", (const volatile unsigned int *)params, 8);
        if (!real)
            log_str("ModelExecute no-forward\n");
    }
    if (real)
        return real(self, cmd, n, params);
    return 0;
}
