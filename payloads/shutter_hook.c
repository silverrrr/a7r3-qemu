/* Log-only LD_PRELOAD hook for A7R3 electronic-shutter sound investigation.
 *
 * Interposes:
 *   ViewBaseProduct::PlayBeep(int)                 _ZN15ViewBaseProduct8PlayBeepEi
 *   LkmBeepIF::ModelExecute(char const*,int,...)  _ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList
 *   open / openat / fopen                           (logs shutter/avsystemsound paths)
 *
 * Writes /tmp/a7r3_payload/hook.log and also mirrors the load marker to stdout.
 * No behavior changes: every intercepted call is forwarded to the original.
 */

#include <stdarg.h>

typedef unsigned long usize;

extern void *dlsym(void *handle, const char *name);
#define RTLD_NEXT ((void *)-1L)

#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_unlink 10

static volatile int hook_ready;
static volatile int resolving;

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

static int contains(const char *s, const char *needle)
{
    if (!s || !needle)
        return 0;
    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

static void raw_write(int fd, const char *s, usize n)
{
    if (n)
        sys3(SYS_write, fd, (long)s, (long)n);
}

static void log_str(const char *s)
{
    int fd;
    if (!hook_ready)
        return;
    fd = (int)sys3(SYS_open, (long)"/setting/hook.log", 0x441, 0644);
    if (fd >= 0) {
        raw_write(fd, s, slen(s));
        sys3(SYS_close, fd, 0, 0);
    }
}

static void log_int(const char *prefix, int v)
{
    char buf[32];
    char tmp[16];
    int i = 0;
    int n = 0;
    unsigned int u;
    if (v < 0) {
        log_str(prefix);
        log_str("-");
        u = (unsigned int)(-v);
    } else {
        log_str(prefix);
        u = (unsigned int)v;
    }
    do {
        tmp[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u && i < (int)sizeof(tmp));
    while (i > 0 && n < (int)sizeof(buf) - 2)
        buf[n++] = tmp[--i];
    buf[n++] = '\n';
    buf[n] = 0;
    log_str(buf);
}

/* ---- PlayBeep ---- */
typedef int (*playbeep_t)(void *, int);
static playbeep_t real_playbeep;

int _ZN15ViewBaseProduct8PlayBeepEi(void *self, int id)
{
    if (!real_playbeep && !resolving) {
        resolving = 1;
        real_playbeep = (playbeep_t)dlsym(RTLD_NEXT, "_ZN15ViewBaseProduct8PlayBeepEi");
        resolving = 0;
    }
    log_int("PlayBeep id=", id);
    if (real_playbeep)
        return real_playbeep(self, id);
    return 0;
}

/* ---- LkmBeepIF::ModelExecute ---- */
typedef int (*model_exec_t)(void *, const char *, int, void *);
static model_exec_t real_model_exec;

int _ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList(void *self, const char *cmd, int n, void *params)
{
    if (!real_model_exec && !resolving) {
        resolving = 1;
        real_model_exec = (model_exec_t)dlsym(RTLD_NEXT,
                                              "_ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList");
        resolving = 0;
    }
    log_str("ModelExecute cmd=");
    log_str(cmd ? cmd : "(null)");
    log_int(" n=", n);
    if (real_model_exec)
        return real_model_exec(self, cmd, n, params);
    return 0;
}

static int interesting(const char *path)
{
    return contains(path, "Shutter") || contains(path, "shutter") ||
           contains(path, "avsystemsound") || contains(path, "BEEP") ||
           contains(path, "beep");
}

/* ---- open / open64 / openat / fopen ---- */
typedef int (*open_t)(const char *, int, ...);
static open_t real_open;

int open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & 0x40) { /* O_CREAT */
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (hook_ready && interesting(path)) {
        log_str("open ");
        log_str(path);
        log_str("\n");
    }
    if (!real_open && !resolving) {
        resolving = 1;
        real_open = (open_t)dlsym(RTLD_NEXT, "open");
        resolving = 0;
    }
    if (real_open)
        return real_open(path, flags, mode);
    return -1;
}

typedef int (*openat_t)(int, const char *, int, ...);
static openat_t real_openat;

int openat(int dirfd, const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & 0x40) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (hook_ready && interesting(path)) {
        log_str("openat ");
        log_str(path);
        log_str("\n");
    }
    if (!real_openat && !resolving) {
        resolving = 1;
        real_openat = (openat_t)dlsym(RTLD_NEXT, "openat");
        resolving = 0;
    }
    if (real_openat)
        return real_openat(dirfd, path, flags, mode);
    return -1;
}

typedef void *(*fopen_t)(const char *, const char *);
static fopen_t real_fopen;

void *fopen(const char *path, const char *mode)
{
    if (hook_ready && interesting(path)) {
        log_str("fopen ");
        log_str(path);
        log_str("\n");
    }
    if (!real_fopen && !resolving) {
        resolving = 1;
        real_fopen = (fopen_t)dlsym(RTLD_NEXT, "fopen");
        resolving = 0;
    }
    if (real_fopen)
        return real_fopen(path, mode);
    return 0;
}

/* ---- constructor ---- */
__attribute__((constructor))
static void hook_init(void)
{
    static const char msg[] = "HOOK_LOADED\n";
    /* Fail-safe: remove the persistent preload before doing anything else, so
     * a later crash cannot cause a boot loop. */
    sys3(SYS_unlink, (long)"/setting/mode/preload", 0, 0);
    raw_write(1, msg, sizeof(msg) - 1);
    hook_ready = 1;
    log_str("HOOK_LOADED\n");
}
