/* Normal-mode KIKILOG dumper (A7R3).
 * Deploy via /setting/mode: dmode=3p + preload=/setting/mode/logdump.so
 * bootin then starts im.elf with LD_PRELOAD in NORMAL mode.
 * Dumps /tmp/kikilog.dat, /log/a7r3_dump.bin, the fixed alog ring and
 * /proc metadata to the SD card (or /log as fallback).  Raw syscalls only.
 */
#define SYS_exit    1
#define SYS_fork    2
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_getpid  20
#define SYS_mmap2   192
#define SYS_munmap  91
#define SYS_nanosleep 162

static long sys3(long n, long a, long b, long c)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile ("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long sys1(long n, long a)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    __asm__ volatile ("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static unsigned long slen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void kwrite(const char *s)
{
    long fd;
    unsigned long n = slen(s);
    fd = sys3(SYS_open, (long)"/dev/kmsg", 1, 0);
    if (fd >= 0) {
        sys3(SYS_write, fd, (long)s, (long)n);
        sys3(SYS_close, fd, 0, 0);
    }
}

static long open_ro(const char *p);
static long open_rw(const char *p);

static void write_small(const char *path, const char *a, const char *b)
{
    long fd = open_rw(path);
    if (fd < 0) return;
    if (a) sys3(SYS_write, fd, (long)a, (long)slen(a));
    if (b) sys3(SYS_write, fd, (long)b, (long)slen(b));
    sys3(SYS_close, fd, 0, 0);
}

static long open_ro(const char *p)
{
    return sys3(SYS_open, (long)p, 0, 0);
}

static long open_rw(const char *p)
{
    return sys3(SYS_open, (long)p, 0x241 /*O_WRONLY|O_CREAT|O_TRUNC*/, 0644);
}

static int comm_is_im(void)
{
    char b[32];
    int n, i;
    long fd = open_ro("/proc/self/comm");
    if (fd < 0) return 0;
    n = (int)sys3(SYS_read, fd, (long)b, 31);
    sys3(SYS_close, fd, 0, 0);
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) if (b[i] == '\n') b[i] = 0;
    return b[0] == 'i' && b[1] == 'm' && b[2] == '.' && b[3] == 'e';
}

/* Copy up to cap bytes from src into outfd, with a banner. */
static unsigned long copy_into(long outfd, const char *src, unsigned long cap)
{
    char buf[4096];
    unsigned long total = 0;
    long fd = open_ro(src);
    int n;

    if (src[0] == '/') {
        kwrite("LOGD ");
        kwrite(src);
        kwrite("\n");
    }
    if (fd < 0) return 0;
    for (;;) {
        if (total >= cap) break;
        n = (int)sys3(SYS_read, fd, (long)buf, (long)sizeof buf);
        if (n <= 0) break;
        if ((unsigned long)n > cap - total) n = (int)(cap - total);
        sys3(SYS_write, outfd, (long)buf, n);
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    return total;
}

/* Find a writable vfat mount on the SD card ("/dev/mmcblk..."). */
static int find_sd_mount(char *out, int cap)
{
    static char maps[65536];
    long fd = open_ro("/proc/mounts");
    int n, i;

    if (fd < 0) return 0;
    n = (int)sys3(SYS_read, fd, (long)maps, (long)sizeof(maps) - 1);
    sys3(SYS_close, fd, 0, 0);
    if (n <= 0) return 0;
    maps[n] = 0;
    for (i = 0; i + 10 < n; i++) {
        if (maps[i] == '/' && maps[i + 1] == 'd' && maps[i + 2] == 'e' &&
            maps[i + 3] == 'v' && maps[i + 4] == '/' &&
            maps[i + 5] == 'm' && maps[i + 6] == 'm' && maps[i + 7] == 'c') {
            int j = i, k = 0;
            while (j < n && maps[j] != ' ') j++;
            while (j < n && maps[j] == ' ') j++;
            while (j < n && maps[j] != ' ' && k < cap - 1) out[k++] = maps[j++];
            if (k > 0) { out[k] = 0; return 1; }
        }
    }
    return 0;
}

static void build_name(char *dst, const char *mnt, const char *file)
{
    int i = 0;
    while (mnt[i]) { dst[i] = mnt[i]; i++; }
    if (i > 0 && dst[i - 1] != '/') dst[i++] = '/';
    while (*file) dst[i++] = *file++;
    dst[i] = 0;
}

static char *put_hex(char *p, unsigned long v)
{
    static const char hx[] = "0123456789abcdef";
    char t[16];
    int i = 0, j = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v) { t[i++] = hx[v & 15]; v >>= 4; }
    while (i) p[j++] = t[--i];
    return p + j;
}

static void kmsg_n(const char *s, long n)
{
    long fd = sys3(SYS_open, (long)"/dev/kmsg", 1, 0);
    if (fd >= 0) {
        sys3(SYS_write, fd, (long)s, n);
        sys3(SYS_close, fd, 0, 0);
    }
}

/* Emit a filtered excerpt of the KIKILOG to /dev/kmsg so the serial shows it. */
static void kmsg_excerpt(const char *src, unsigned long cap)
{
    char in[1024];
    char out[1024];
    unsigned long total = 0;
    long fd = open_ro(src);
    int n, i;
    char h[48];
    char *p;

    if (fd < 0) return;
    kmsg_n("KIKI_BEGIN\n", 11);
    while (total < cap) {
        n = (int)sys3(SYS_read, fd, (long)in, (long)sizeof in);
        if (n <= 0) break;
        for (i = 0; i < n; i++) {
            char c = in[i];
            out[i] = (c == '\n' || (c >= 32 && c < 127)) ? c : '.';
        }
        kmsg_n(out, n);
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    p = put_hex(h, total);
    *p++ = '\n';
    kmsg_n("KIKI_BYTES=", 11);
    kmsg_n(h, p - h);
    kmsg_n("KIKI_END\n", 9);
}

static void klog_hex(const char *prefix, unsigned long v)
{
    char h[32];
    char *p = put_hex(h, v);
    *p++ = '\n';
    kwrite(prefix);
    kmsg_n(h, p - h);
}

static void logdump_work(void)
{
    char path[256];
    char mnt[128];
    long outfd;
    unsigned long total = 0;
    long memfd;
    char h[32];
    char *p;

    kwrite("LOGD start\n");

    if (find_sd_mount(mnt, sizeof mnt)) {
        build_name(path, mnt, "a7r3_normal_log.bin");
        kwrite("LOGD sd=");
        kwrite(mnt);
        kwrite("\n");
    } else {
        build_name(path, "/tmp", "a7r3_normal_log.bin");
    }
    outfd = open_rw(path);
    if (outfd < 0) {
        build_name(path, "/log", "a7r3_normal_log.bin");
        outfd = open_rw(path);
    }
    if (outfd < 0) { kwrite("LOGD open fail\n"); return; }
    kwrite("LOGD path=");
    kwrite(path);
    kwrite("\n");
    write_small("/tmp/logdump_path", path, "\n");

    {
        static const char banner[] =
            "A7R3 normal-mode logdump\n-----------------------\n";
        sys3(SYS_write, outfd, (long)banner, (long)sizeof banner - 1);
    }
    total += copy_into(outfd, "/proc/version", 4096);
    total += copy_into(outfd, "/proc/mounts", 16384);
    {
        unsigned long k = copy_into(outfd, "/tmp/kikilog.dat", 4u * 1024 * 1024);
        total += k;
        klog_hex("LOGD kikilog_bytes=", k);
    }
    {
        unsigned long k = copy_into(outfd, "/log/a7r3_dump.bin", 4u * 1024 * 1024);
        total += k;
        klog_hex("LOGD logfile_bytes=", k);
    }

    /* Fixed alog ring seen on this platform (read via /dev/mem seek). */
    memfd = sys3(SYS_open, (long)"/dev/mem", 0 /*O_RDONLY*/, 0);
    if (memfd >= 0) {
        if (sys3(19 /*lseek*/, memfd, (long)0x1F080000L, 0) >= 0) {
            char rbuf[4096];
            unsigned long left = 2u * 1024 * 1024;
            int n;
            while (left) {
                long want = left > sizeof rbuf ? (long)sizeof rbuf : (long)left;
                n = (int)sys3(SYS_read, memfd, (long)rbuf, want);
                if (n <= 0) break;
                sys3(SYS_write, outfd, (long)rbuf, n);
                total += n;
                left -= n;
            }
        }
        sys3(SYS_close, memfd, 0, 0);
    }
    sys3(SYS_close, outfd, 0, 0);
    p = put_hex(h, total);
    *p++ = '\n';
    kmsg_n("LOGD_total=", 11);
    kmsg_n(h, p - h);
    write_small("/tmp/logdump_done", h, 0);
    kwrite("LOGD done\n");

    kmsg_excerpt("/tmp/kikilog.dat", 8192);
}

/* Fork so the dump happens after the app has logged something, without
 * delaying im.elf startup. */
__attribute__((constructor))
static void logdump_init(void)
{
    long pid;
    char cb[32];
    long fd;
    int n;

    cb[0] = 0;
    fd = open_ro("/proc/self/comm");
    if (fd >= 0) {
        n = (int)sys3(SYS_read, fd, (long)cb, 31);
        if (n > 0) {
            cb[n] = 0;
            while (n > 0 && (cb[n - 1] == '\n' || cb[n - 1] == '\r'))
                cb[--n] = 0;
        }
        sys3(SYS_close, fd, 0, 0);
    }
    write_small("/tmp/logdump_seen", cb, "\n");
    if (!comm_is_im()) return;
    write_small("/tmp/logdump_loaded", "ok\n", 0);
    pid = sys3(SYS_fork, 0, 0, 0);
    if (pid > 0) return;
    if (pid == 0) {
        long ts[2];
        sys3(66 /*setsid*/, 0, 0, 0);
        ts[0] = 15;
        ts[1] = 0;
        sys3(SYS_nanosleep, (long)ts, 0, 0);
        logdump_work();
        for (;;)
            sys3(SYS_nanosleep, (long)ts, 0, 0);
    }
}
