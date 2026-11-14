/* gpu_shim_qemu.c -- LD_PRELOAD shim for the A7R3 camera process.
 *
 * Original work (MIT).  Loaded into im.elf via /bin/iperf's preload spec;
 * observes GPU ioctls, writes the panel/flip buffers, injects keys and reads
 * the settings tree through CautionConfig's exported API.
 */

/* Userland /dev/dmpgles2 shim for the A7R3 silent-shutter investigation.
 *
 * Loaded via LD_PRELOAD (console variant for QEMU).  Inert unless
 * /proc/self/comm is im.elf.
 *
 * Interposes open/openat/ioctl/mmap/munmap/close so that the GPU bring-up in
 * libObj.so sees a device that opens, answers the three ioctls we located by
 * disassembly (0x80048206 / 0xc0048202 / 0x80048263) and hands out anonymous
 * memory for mmap.  Values follow zve10-qemu's fake_gles2.c and the real
 * driver facts in docs/analysis/system/SUGILITE_MMAP_LEAD.md.
 *
 * Everything is logged with a GPU_ prefix; unknown ioctls are logged and
 * acknowledged with 0 (permissive bring-up, see zve10 notes class 12).
 */

typedef unsigned long usize;

extern void *dlsym(void *handle, const char *name) __attribute__((weak));
extern void *dlvsym(void *handle, const char *name, const char *version) __attribute__((weak));
#define RTLD_NEXT ((void *)-1L)
#define RTLD_DEFAULT ((void *)0)

#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_read    3
#define SYS_getpid  20
#define SYS_mknod   14
#define SYS_ioctl   54
#define SYS_lseek   19
#define SYS_munmap  91
#define SYS_mmap2   192

#include "ui_font8x8.h"
#include "ui_lz77.h"
#include "ui_dmp_raster.h"
#define UINTPTR_MAX 0xffffffffUL
#define NULL ((void *)0)
#include "ui_gemini_raster.h"
#define SYS_openat  322

#define O_APPEND_CREAT 0x441
#define O_CREAT        0x40
#define PROT_MASK      0x7
#define MAP_PRIVATE_ANON 0x22

#define CMD_GET_REG  0x80048206u  /* _IOR(0x82,0x06,4)  register/mmap size */
#define CMD_XLATE    0xc0048202u  /* _IOWR(0x82,0x02,4) index -> address   */
#define CMD_STATE    0x80048263u  /* _IOR(0x82,0x63,4)  hw bypass feature */
#define CMD_WAIT_A   0xc0048211u
#define CMD_WAIT_B   0xc0048212u

#define FAKE_MAX 8

static volatile int active;
static volatile int g_model_seen;
static int trace_on;
static int g_log_skip; /* LOG_SKIP: forward 1/64 of ulogio traffic */
static int g_log_dummy; /* LOG_DUMMY_GATE: static valloc bypass (slower in practice) */

/* DUMMY_MSG: static message body for skipped ulogio logs (no valloc ioctl). */
#define DUMMY_SLOTS 8
#define DUMMY_WORDS 72
static unsigned dummy_pool[DUMMY_SLOTS][DUMMY_WORDS];
static unsigned dummy_rr;

static void *dummy_alloc(unsigned size)
{
    unsigned *p;
    if (size > (unsigned)((DUMMY_WORDS - 4) * 4))
        return 0;
    p = dummy_pool[dummy_rr++ % DUMMY_SLOTS];
    p[3] = 0x8f013a; /* header word at payload-4, like the real valloc */
    return (void *)(p + 4);
}

static int dummy_is(const void *msg)
{
    unsigned long m = (unsigned long)msg;
    int i;
    if (!m)
        return 0;
    for (i = 0; i < DUMMY_SLOTS; i++) {
        unsigned long base = (unsigned long)(dummy_pool[i] + 4);
        if (m >= base && m < base + (DUMMY_WORDS - 4) * 4)
            return 1;
    }
    return 0;
}
static int verbose;
static int log_fd = -1;
static int fake_fds[FAKE_MAX];
static int doorbells;
static volatile int draw_seen;
static volatile int ui_kick_ran;
static volatile int key_sent;
static int ldec_fd = -1;
static int ldec_mem_fd = -1;
static unsigned long g_manager;
static int opens_seen;
static int flash_fds[4];
static int flash_ioctls;
static int maps_contains(unsigned long addr);
static unsigned long virt_to_phys(unsigned long v);
static unsigned long module_base(const char *needle);
static unsigned long libobj_base(void);
static int file_exists(const char *path);
static void scan_self_tasks(void);
static void open_view_once(void);
static void open_view_named(const char *name);
static void dump_uxc_maps(void);
static void probe_proc_input(void);
static void inject_key_once(unsigned long code, unsigned long h);
static void dump_key_table(void);
static void inject_key_by_id(unsigned long keyid);
static void dump_event_table(void);
static void send_event(unsigned long code, unsigned long status, int via_queue);
static void send_raw(unsigned long w0, unsigned long w1, unsigned long w2, unsigned long w3, int via_queue);
static void dispatch_event(unsigned long action);
static void osal_send_input(unsigned long w0, unsigned long w1, unsigned long w2, unsigned long w3);
static void inject_infra_key(unsigned long key, unsigned long status, unsigned long from);
static void inject_servant_event(unsigned long key, unsigned long status);
static void inject_infra_key_g(unsigned long key, unsigned long status, unsigned long from);
static void dump_sysfs_bus(const char *path, int filter);
static void log_str(const char *s);
static void log_hex(const char *prefix, unsigned long v);
static int has_sub(const char *h, const char *n);

/* BLKDISCARD / BLKSECDISCARD / BLKZEROOUT: Sony's eMMC driver rejects the
 * range the app uses, and the app's flash logger treats that as fatal.  The
 * emulator does not need the erase; acknowledge it. */
#define BLK_DISCARD   0x1277u
#define BLK_SECDISCARD 0x127du
#define BLK_ZEROOUT   0x127fu

/* Full barrier for /dev/mem writes.  The system clang build cannot resolve
 * __sync_synchronize() (no compiler-rt under -nostdlib), so emit the ARM
 * instruction directly. */
static inline void shim_barrier(void)
{
    unsigned long t = 0;
    /* DMB (ARMv7) / drain write buffer; CP15 c7,c10,5 is accepted by all
     * ARM assemblers, unlike the "dmb" mnemonic at the default -march. */
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" : : "r"(t) : "memory");
}

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

/* GEM raster timing (SYS_clock_gettime on ARM EABI). */
#define SYS_clock_gettime 263
struct shim_ts { long sec; long nsec; };
static inline void shim_now(struct shim_ts *t)
{
    if (sys3(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, (long)t, 0) != 0) {
        t->sec = 0;
        t->nsec = 0;
    }
}
static inline unsigned long long shim_ts_ns(const struct shim_ts *t)
{
    return (unsigned long long)t->sec * 1000000000ULL +
           (unsigned long long)t->nsec;
}

static long sys4(long nr, long a, long b, long c, long d)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory", "cc");
    return r0;
}

static long sys5(long nr, long a, long b, long c, long d, long e)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r7)
                     : "memory", "cc");
    return r0;
}

static long sys6(long nr, long a, long b, long c, long d, long e, long f)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
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
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int has_prefix(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static int log_open(void)
{
    static const char *paths[] = { "/setting/hook.log", "/log/hook.log",
                                   "/tmp/hook.log", 0 };
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

static void log_hex(const char *prefix, unsigned long v)
{
    char buf[40];
    char tmp[16];
    int n = 0;
    int i = 0;

    while (*prefix && n < 14)
        buf[n++] = *prefix++;
    buf[n++] = '0';
    buf[n++] = 'x';
    if (!v) {
        buf[n++] = '0';
    } else {
        while (v && i < 16) {
            tmp[i++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        }
        while (i > 0 && n < 36)
            buf[n++] = tmp[--i];
    }
    buf[n++] = '\n';
    out(buf, n);
}

/* one line: prefix + up to 4 hex words */
static void log_words(const char *tag, unsigned long a, unsigned long b,
                      unsigned long c, unsigned long d)
{
    log_str(tag);
    log_hex(" a=", a);
    log_str(tag);
    log_hex(" b=", b);
    log_str(tag);
    log_hex(" c=", c);
    log_str(tag);
    log_hex(" d=", d);
}

/* ---- OSAL queue tracing -------------------------------------------------
 * osal_* are undefined imports in im.elf (JUMP_SLOT relocations), so this
 * preloaded object binds before libosal_uipc.so.  Everything is forwarded to
 * the real implementation; we only sample the first calls per (thread,queue)
 * to find which queues grm_dn_vdf_if / AV_DDS block on. */
static char *put_dec(char *p, unsigned long v);
static char *put_s(char *p, const char *s);
static char *put_x(char *p, unsigned long v);

#define OSAL_TRACE_MAX 256
struct osal_trace {
    unsigned tid, qid, enters, oks;
};
static struct osal_trace osal_traces[OSAL_TRACE_MAX];
static int osal_trace_count;

static unsigned osal_tid(void)
{
    return (unsigned)sys3(224 /* gettid */, 0, 0, 0);
}

static void osal_comm(unsigned tid, char *dst)
{
    /* QUIET_TRACE: cache the thread name; /proc reads were ~12% of guest CPU. */
    static struct { unsigned tid; char comm[20]; } cache[64];
    static int nc;
    char path[64];
    char *p = path;
    int fd, n;

    for (int i = 0; i < nc; i++) {
        if (cache[i].tid == tid) {
            int k = 0;
            while (cache[i].comm[k] && k < 19) {
                dst[k] = cache[i].comm[k];
                k++;
            }
            dst[k] = 0;
            return;
        }
    }
    p = put_s(p, "/proc/self/task/");
    p = put_dec(p, tid);
    p = put_s(p, "/comm");
    *p = 0;
    fd = (int)sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0) {
        dst[0] = 0;
        return;
    }
    n = (int)sys3(SYS_read, fd, (long)dst, 31);
    sys3(SYS_close, fd, 0, 0);
    if (n <= 0) {
        dst[0] = 0;
        return;
    }
    dst[n] = 0;
    while (n > 0 && (dst[n - 1] == '\n' || dst[n - 1] == '\r' ||
                     dst[n - 1] == ' '))
        dst[--n] = 0;
    if (nc < 64) {
        int k = 0;
        cache[nc].tid = tid;
        while (k < n && k < 19) {
            cache[nc].comm[k] = dst[k];
            k++;
        }
        cache[nc].comm[k] = 0;
        nc++;
    }
}

static struct osal_trace *osal_slot(unsigned tid, unsigned qid)
{
    int i;

    for (i = 0; i < osal_trace_count; i++)
        if (osal_traces[i].tid == tid && osal_traces[i].qid == qid)
            return &osal_traces[i];
    if (osal_trace_count >= OSAL_TRACE_MAX)
        return 0;
    osal_traces[osal_trace_count].tid = tid;
    osal_traces[osal_trace_count].qid = qid;
    osal_traces[osal_trace_count].enters = 0;
    osal_traces[osal_trace_count].oks = 0;
    return &osal_traces[osal_trace_count++];
}

static void osal_head(const char *tag, unsigned tid, unsigned qid)
{
    char buf[160];
    char comm[32];
    char *p = buf;

    p = put_s(p, tag);
    p = put_s(p, " tid=");
    p = put_dec(p, tid);
    osal_comm(tid, comm);
    if (comm[0]) {
        p = put_s(p, " comm=");
        p = put_s(p, comm);
    }
    p = put_s(p, " qid=");
    p = put_x(p, qid);
    *p++ = '\n';
    out(buf, (int)(p - buf));
}

static void osal_rcv_enter(const struct osal_trace *s, unsigned size,
                           unsigned tmo)
{
    char buf[96];
    char *p = buf;

    osal_head("OSAL_RCV", s->tid, s->qid);
    p = put_s(p, "  size=");
    p = put_dec(p, size);
    p = put_s(p, " tmo=");
    p = put_dec(p, tmo);
    *p++ = '\n';
    out(buf, (int)(p - buf));
}

static void osal_rcv_ok(const struct osal_trace *s, unsigned size, long ret,
                        const void *msg)
{
    char buf[96];
    char *p = buf;
    int i;

    p = put_s(p, "OSAL_RCV_OK tid=");
    p = put_dec(p, s->tid);
    p = put_s(p, " qid=");
    p = put_x(p, s->qid);
    p = put_s(p, " size=");
    p = put_dec(p, size);
    p = put_s(p, " ret=");
    p = put_dec(p, (unsigned long)ret);
    *p++ = '\n';
    out(buf, (int)(p - buf));
    if (!msg)
        return;
    for (i = 0; i < 4; i++) {
        p = buf;
        p = put_s(p, "  msg[");
        p = put_dec(p, (unsigned long)i);
        p = put_s(p, "]=");
        p = put_x(p, *(const unsigned int *)((const char *)msg + i * 4));
        *p++ = '\n';
        out(buf, (int)(p - buf));
    }
}

static void osal_snd_line(const char *tag, const struct osal_trace *s,
                          unsigned size, long ret, const void *msg)
{
    char buf[96];
    char *p = buf;
    int i;

    p = put_s(p, tag);
    p = put_s(p, " tid=");
    p = put_dec(p, s->tid);
    p = put_s(p, " qid=");
    p = put_x(p, s->qid);
    p = put_s(p, " size=");
    p = put_dec(p, size);
    if (ret >= 0) {
        p = put_s(p, " ret=");
        p = put_dec(p, (unsigned long)ret);
    }
    *p++ = '\n';
    out(buf, (int)(p - buf));
    if (!msg)
        return;
    for (i = 0; i < 4; i++) {
        p = buf;
        p = put_s(p, "  msg[");
        p = put_dec(p, (unsigned long)i);
        p = put_s(p, "]=");
        p = put_x(p, *(const unsigned int *)((const char *)msg + i * 4));
        *p++ = '\n';
        out(buf, (int)(p - buf));
    }
}

typedef int (*osal_rcv_fn)(unsigned, void *, unsigned, unsigned);
typedef int (*osal_snd_fn)(unsigned, const void *, unsigned, unsigned);
typedef int (*osal_valloc_fn)(unsigned, void **, unsigned, unsigned);
static osal_rcv_fn real_osal_rcv;
static osal_snd_fn real_osal_snd;
static osal_valloc_fn real_osal_valloc;

int trace_osal_rcv_msg_tmo(unsigned qid, void *msg, unsigned size, unsigned tmo)
{
    struct osal_trace *s;
    int r;

    if (!real_osal_rcv)
        real_osal_rcv = (osal_rcv_fn)dlsym(RTLD_NEXT, "osal_rcv_msg_tmo");
    if (!real_osal_rcv)
        return -1;
    if (!trace_on && qid != 0x7a0020)
        return real_osal_rcv(qid, msg, size, tmo);
    s = active ? osal_slot(osal_tid(), qid) : 0;
    if (trace_on && s && s->enters < 2) {
        s->enters++;
        osal_rcv_enter(s, size, tmo);
    }
    r = real_osal_rcv(qid, msg, size, tmo);
    if ((qid == 0x7a0020 || qid == 0x7a804c || qid == 0x7ae03a) &&
        r >= 0 && msg) {
        /* Proof hook: what does the app's input pipeline dequeue? */
        static int grcv_n;
        void *m = *(void **)msg;
        if (m && grcv_n < 160) {
            const unsigned *w = (const unsigned *)m;
            unsigned hdr = *(const unsigned *)((const char *)m - 4);
            char c1[32];
            char bb[192];
            char *q = bb;
            grcv_n++;
            c1[0] = 0;
            osal_comm(osal_tid(), c1);
            q = put_s(q, "GRCV comm=");
            q = put_s(q, c1);
            q = put_s(q, " q=");
            q = put_x(q, qid);
            q = put_s(q, " hdr=");
            q = put_x(q, hdr);
            q = put_s(q, " w=");
            q = put_x(q, w[0]);
            q = put_s(q, ",");
            q = put_x(q, w[1]);
            q = put_s(q, ",");
            q = put_x(q, w[2]);
            q = put_s(q, ",");
            q = put_x(q, w[3]);
            q = put_s(q, " ret=");
            q = put_dec(q, (unsigned long)r);
            *q++ = '\n';
            out(bb, (int)(q - bb));
        }
    }
    if (trace_on && s && r >= 0 && s->oks < 2) {
        s->oks++;
        osal_rcv_ok(s, size, r, msg);
    }
    return r;
}

int trace_osal_snd_msg(unsigned qid, const void *msg, unsigned size, unsigned pri)
{
    struct osal_trace *s;
    int r;

    if (!real_osal_snd)
        real_osal_snd = (osal_snd_fn)dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!real_osal_snd)
        return -1;
    if (g_log_skip && dummy_is(msg))
        return 0;
    if (g_log_skip && qid == 0x8f013a) {
        static unsigned n;
        if ((n++ & 63) != 0) {
            typedef int (*free_fn)(unsigned, void *);
            static free_fn rf;
            if (!rf)
                rf = (free_fn)dlsym(RTLD_NEXT, "osal_free_msg");
            if (rf)
                rf(qid, (void *)msg);
            return 0;
        }
    }
    if (trace_on) {
        /* Follow the input pipeline: everything the input threads emit. */
        static int isnd_n;
        unsigned tid0 = osal_tid();
        char c0[32];
        c0[0] = 0;
        osal_comm(tid0, c0);
        if (isnd_n < 300 && c0[0] == 'I' && c0[1] == '-' && msg) {
            const unsigned *w = (const unsigned *)msg;
            char bb[160];
            char *q = bb;
            isnd_n++;
            q = put_s(q, "ISND comm=");
            q = put_s(q, c0);
            q = put_s(q, " qid=");
            q = put_x(q, qid);
            q = put_s(q, " size=");
            q = put_dec(q, (unsigned long)size);
            q = put_s(q, " w=");
            q = put_x(q, w[0]);
            q = put_s(q, ",");
            q = put_x(q, w[1]);
            q = put_s(q, ",");
            q = put_x(q, w[2]);
            q = put_s(q, ",");
            q = put_x(q, w[3]);
            *q++ = '\n';
            out(bb, (int)(q - bb));
        }
    }
    if (!trace_on)
        return real_osal_snd(qid, msg, size, pri);
    s = active ? osal_slot(osal_tid(), qid) : 0;
    if (s && s->enters < 2)
        osal_snd_line("OSAL_SND", s, size, -1, msg);
    r = real_osal_snd(qid, msg, size, pri);
    if (s && s->enters < 2) {
        s->enters++;
        osal_snd_line("OSAL_SND_RET", s, size, r, 0);
    }
    return r;
}

int trace_osal_valloc_msg_wait(unsigned qid, void **msg, unsigned size, unsigned tmo)
{
    struct osal_trace *s;
    int r;

    if (!real_osal_valloc)
        real_osal_valloc = (osal_valloc_fn)dlsym(RTLD_NEXT,
                                                 "osal_valloc_msg_wait");
    if (!real_osal_valloc)
        return -1;
    if (g_log_skip && g_log_dummy && qid == 0x8f013a) {
        void *d = dummy_alloc(size);
        if (d) {
            if (msg)
                *msg = d;
            return 0;
        }
    }
    if (!trace_on)
        return real_osal_valloc(qid, msg, size, tmo);
    s = active ? osal_slot(osal_tid(), qid | 0x80000000u) : 0;
    if (s && s->enters < 2) {
        s->enters++;
        osal_head("OSAL_VALLOC", s->tid, qid);
        log_int("  size=", (int)size);
    }
    r = real_osal_valloc(qid, msg, size, tmo);
    return r;
}

typedef int (*osal_wai_fn)(unsigned, unsigned);
static osal_wai_fn real_osal_wai;

int osal_wai_sem_tmo(unsigned sem, unsigned tmo)
{
    static int logs;

    if (!real_osal_wai)
        real_osal_wai = (osal_wai_fn)dlsym(RTLD_NEXT, "osal_wai_sem_tmo");
    if (!real_osal_wai)
        return -1;
    if (sem == 0x83037au) {
        log_hex("WAI_SEM bypass=", sem);
        return 0;
    }
    if (0 && logs < 200) {
        logs++;
        log_hex("WAI_SEM wait=", sem);
        log_int(" tmo=", (int)tmo);
    }
    return real_osal_wai(sem, tmo);
}

typedef int (*osal_cwt_fn)(void *, void **);
typedef int (*osal_reg_cb_fn)(unsigned, void **, void *, void *);
static osal_cwt_fn real_osal_cwt;
static osal_reg_cb_fn real_osal_reg_cb;
int trace_osal_create_waitable_thread(void *arg, void **outp)
{
    static int n;
    unsigned tid = osal_tid();

    if (!real_osal_cwt)
        real_osal_cwt = (osal_cwt_fn)dlsym(RTLD_NEXT,
                                           "osal_create_waitable_thread");
    if (!real_osal_cwt)
        return -1;
    if (!trace_on)
        return real_osal_cwt(arg, outp);
    if (active && n < 64 && arg) {
        const unsigned int *w = (const unsigned int *)arg;
        char buf[192];
        char name[32];
        char comm[32];
        char *p = buf;
        unsigned long np = w[0];
        int i;

        for (i = 0; i < 31; i++)
            name[i] = 0;
        if (np >= 0x1000 && np < 0x80000000UL) {
            const char *s = (const char *)np;
            for (i = 0; i < 31; i++) {
                char c = s[i];
                if (!c)
                    break;
                if (c < 32 || c > 126) {
                    name[0] = 0;
                    break;
                }
                name[i] = c;
                name[i + 1] = 0;
            }
        }
        n++;
        p = put_s(p, "OSAL_THREAD tid=");
        p = put_dec(p, tid);
        osal_comm(tid, comm);
        if (comm[0]) {
            p = put_s(p, " comm=");
            p = put_s(p, comm);
        }
        p = put_s(p, " name=");
        p = put_s(p, name[0] ? name : "?");
        p = put_s(p, " entry=");
        p = put_x(p, w[2]);
        p = put_s(p, " arg=");
        p = put_x(p, w[3]);
        *p++ = '\n';
        out(buf, (int)(p - buf));
    }
    return real_osal_cwt(arg, outp);
}

int trace_osal_reg_msg_queue_cb(unsigned qid, void **handle, void *cb, void *arg)
{
    static int n;
    unsigned tid = osal_tid();

    if (!real_osal_reg_cb)
        real_osal_reg_cb = (osal_reg_cb_fn)dlsym(RTLD_NEXT,
                                                 "osal_reg_msg_queue_cb");
    if (!real_osal_reg_cb)
        return -1;
    if (!trace_on)
        return real_osal_reg_cb(qid, handle, cb, arg);
    if (active && n < 256) {
        char buf[128];
        char *p = buf;
        char comm[32];
        n++;
        p = put_s(p, "OSAL_REGCB tid=");
        p = put_dec(p, tid);
        osal_comm(tid, comm);
        if (comm[0]) {
            p = put_s(p, " comm=");
            p = put_s(p, comm);
        }
        p = put_s(p, " qid=");
        p = put_x(p, qid);
        p = put_s(p, " cb=");
        p = put_x(p, (unsigned long)cb);
        p = put_s(p, " arg=");
        p = put_x(p, (unsigned long)arg);
        *p++ = '\n';
        out(buf, (int)(p - buf));
    }
    return real_osal_reg_cb(qid, handle, cb, arg);
}

/* The app imports osal_* from libosal_uipc.  Defining the entry points here
 * makes the trace helpers the real interposers, so the display/panel (DMM)
 * traffic becomes visible and can later be answered by this shim. */
int osal_snd_msg(unsigned qid, const void *msg, unsigned size, unsigned pri)
{
    return trace_osal_snd_msg(qid, msg, size, pri);
}

int osal_rcv_msg_tmo(unsigned qid, void *msg, unsigned size, unsigned tmo)
{
    return trace_osal_rcv_msg_tmo(qid, msg, size, tmo);
}

int osal_valloc_msg_wait(unsigned qid, void **msg, unsigned size, unsigned tmo)
{
    return trace_osal_valloc_msg_wait(qid, msg, size, tmo);
}

int osal_create_waitable_thread(void *arg, void **outp)
{
    return trace_osal_create_waitable_thread(arg, outp);
}

int osal_reg_msg_queue_cb(unsigned qid, void **handle, void *cb, void *arg)
{
    return trace_osal_reg_msg_queue_cb(qid, handle, cb, arg);
}

typedef void *(*dlopen_fn)(const char *, int);
static dlopen_fn real_dlopen;
extern int dladdr(const void *, void *) __attribute__((weak));
static void install_trace_hooks(unsigned long base);

static int path_has_libobj(const char *p)
{
    while (p && *p) {
        if (p[0] == 'l' && p[1] == 'i' && p[2] == 'b' && p[3] == 'O' &&
            p[4] == 'b' && p[5] == 'j')
            return 1;
        p++;
    }
    return 0;
}

void *dlopen(const char *path, int flags)
{
    void *h;

    if (!real_dlopen)
        real_dlopen = (dlopen_fn)dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
    if (!real_dlopen)
        return 0;
    h = real_dlopen(path, flags);
    if (active && trace_on) {
        char buf[256];
        char *p = buf;

        p = put_s(p, "DLOPEN ");
        if (path)
            p = put_s(p, path);
        else
            p = put_s(p, "(null)");
        p = put_s(p, " h=");
        p = put_x(p, (unsigned long)h);
        *p++ = '\n';
        out(buf, (int)(p - buf));
        if (!h) {
            extern char *dlerror(void) __attribute__((weak));
            const char *e = dlerror ? dlerror() : 0;
            log_str("DLERR ");
            log_str(e ? e : "(null)");
            log_str("\n");
        }
    }
    if (active && h && path_has_libobj(path) && dladdr) {
        void *sym = dlsym(h, "Application_System_Init");
        unsigned long info[4];
        if (sym && dladdr(sym, info))
            install_trace_hooks(info[1]);
    }
    return h;
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

/* Unconditional early trace: fd 1 and fd 2, before any filtering. */
static void raw_write(const char *s, int n)
{
    sys3(SYS_write, 1, (long)s, n);
    sys3(SYS_write, 2, (long)s, n);
}

static char *put_s(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_x(char *p, unsigned long v)
{
    char t[16];
    int i = 0;
    if (!v) {
        *p++ = '0';
        return p;
    }
    while (v && i < 16) {
        t[i++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    }
    while (i > 0)
        *p++ = t[--i];
    return p;
}

/* Hex-dump up to 96 bytes to the serial log, one line per call. */
static void log_bytes(const char *tag, const void *p, int n)
{
    char buf[256];
    char *q = buf;
    const unsigned char *s = (const unsigned char *)p;
    int i;

    if ((unsigned long)p < 0x10000)
        return;
    if (n > 96)
        n = 96;
    q = put_s(q, tag);
    *q++ = ' ';
    for (i = 0; i < n; i++) {
        *q++ = "0123456789abcdef"[s[i] >> 4];
        *q++ = "0123456789abcdef"[s[i] & 15];
    }
    *q++ = '\n';
    out(buf, (int)(q - buf));
}

static void log_open_line(const char *what, const char *path, int flags, long fd)
{
    char b[256];
    char *p = b;
    p = put_s(p, "GPU ");
    p = put_s(p, what);
    p = put_s(p, " ");
    p = put_s(p, path);
    p = put_s(p, " f=");
    p = put_x(p, (unsigned long)flags);
    p = put_s(p, " ->");
    p = put_x(p, (unsigned long)fd);
    *p++ = '\n';
    out(b, (int)(p - b));
}

static void flash_add(int fd)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (flash_fds[i] == 0) {
            flash_fds[i] = fd;
            return;
        }
    }
}

static int flash_has(int fd)
{
    int i;
    for (i = 0; i < 4; i++)
        if (fd > 0 && flash_fds[i] == fd)
            return 1;
    return 0;
}

static void flash_del(int fd)
{
    int i;
    for (i = 0; i < 4; i++)
        if (flash_fds[i] == fd)
            flash_fds[i] = 0;
}

static int fake_add(int fd)
{
    int i;

    for (i = 0; i < FAKE_MAX; i++) {
        if (fake_fds[i] == 0) {
            fake_fds[i] = fd;
            return 1;
        }
    }
    return 0;
}

static int fake_has(int fd)
{
    int i;
    for (i = 0; i < FAKE_MAX; i++)
        if (fd > 0 && fake_fds[i] == fd)
            return 1;
    return 0;
}

static void fake_del(int fd)
{
    int i;
    for (i = 0; i < FAKE_MAX; i++)
        if (fake_fds[i] == fd)
            fake_fds[i] = 0;
}

/* Real /dev/dmpgles2 descriptors: the kernel driver is present in this image,
 * so the app uses it instead of the fake path.  Track the fd so its mmap'd
 * command buffers and doorbell ioctls can be captured too. */
#define GPU_FD_MAX 8
static int gpu_fds[GPU_FD_MAX];

static int gpu_fd_add(int fd)
{
    int i;
    for (i = 0; i < GPU_FD_MAX; i++) {
        if (gpu_fds[i] == 0) {
            gpu_fds[i] = fd;
            return 1;
        }
    }
    return 0;
}

static int gpu_fd_has(int fd)
{
    int i;
    for (i = 0; i < GPU_FD_MAX; i++)
        if (fd > 0 && gpu_fds[i] == fd)
            return 1;
    return 0;
}

static void gpu_fd_del(int fd)
{
    int i;
    for (i = 0; i < GPU_FD_MAX; i++)
        if (gpu_fds[i] == fd)
            gpu_fds[i] = 0;
}

static int put_u32(unsigned long addr, unsigned int v)
{
    if (addr < 0x10000 || (addr & 3))
        return 0;
    *(volatile unsigned int *)addr = v;
    return 1;
}

static unsigned int get_u32(unsigned long addr)
{
    if (addr < 0x10000 || (addr & 3))
        return 0;
    return *(volatile unsigned int *)addr;
}

static void probe_loop(void);
static unsigned char probe_stack[32768] __attribute__((aligned(8)));
static unsigned char mon_stack[16384] __attribute__((aligned(8)));
static void monitor_loop(void);
static int signal_ready_sem(void);
static int uinput_open(void);

static void ensure_gpu_node(void)
{
    /* grm_gles.ko registers char major 248 / minor 29 (same binding as A7M3).
     * If the node already exists mknod fails with EEXIST; ignore it. */
    long r = sys4(SYS_mknod, (long)"/dev/dmpgles2",
                  0x21B6 /* S_IFCHR | 0666 */, 0xF81D, 0);
    log_str("GPU node rc=");
    log_int("", (int)r);
}

__attribute__((constructor))
static void gpu_shim_init(void)
{
    int hit = comm_is_target();

    raw_write("GPU_INIT comm_is_im=", sizeof("GPU_INIT comm_is_im=") - 1);
    raw_write(hit ? "1\n" : "0\n", 2);
    if (!hit)
        return;
    active = 1;
    /* QUIET_TRACE: per-message OSAL tracing is opt-in; it costs ~29% of the
     * guest CPU (each line re-reads /proc/<tid>/comm and writes the UART). */
    g_log_skip = file_exists("/usr/tool/log_skip");
    g_log_dummy = file_exists("/usr/tool/log_dummy");
    trace_on = 0;
    {
        int tfd = (int)sys3(SYS_open, (long)"/usr/tool/osal_trace", 0, 0);
        if (tfd >= 0) {
            sys3(SYS_close, tfd, 0, 0);
            trace_on = 1;
        }
    }
    log_str("GPU_SHIM comm=im.elf\n");
    log_int("pid=", (int)sys3(SYS_getpid, 0, 0, 0));
    ensure_gpu_node();
    uinput_open();
    /* Load the standalone log-dump hook alongside this shim. */
    if (!real_dlopen)
        real_dlopen = (dlopen_fn)dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
    if (real_dlopen)
        real_dlopen("/usr/tool/logdump.so", 2 /*RTLD_NOW*/);

    /* Start the logging service ourselves: bootin's qsi.elf entry never runs
     * on this model, and the alog ring is fed by ulogio. */
    {
        long child = sys3(2 /* fork */, 0, 0, 0);
        if (child == 0) {
            char *av[2];
            av[0] = (char *)"/usr/bin/ulogio.elf";
            av[1] = 0;
            sys3(11 /* execve */, (long)av[0], (long)av, 0);
            for (;;)
                ;
        }
        log_int("ulogio_child=", (int)child);
    }

    /* Make library addresses stable so an external debugger can set
     * watchpoints before the app finishes init. */
    {
        int fd = (int)sys3(SYS_open, (long)"/proc/sys/kernel/randomize_va_space",
                           1 /* O_WRONLY */, 0);
        if (fd >= 0) {
            sys3(SYS_write, fd, (long)"0", 1);
            sys3(SYS_close, fd, 0, 0);
        }
    }

    /* Probe thread: report what im.elf's threads are waiting on. */
    {
        long child = sys5(120 /* clone */, 0x50F00,
                          (long)(probe_stack + sizeof(probe_stack) - 16),
                          0, 0, 0);
        if (child == 0) {
            probe_loop();
            for (;;)
                ;
        }
        log_int("probe_tid=", (int)child);
    }

    /* Monitor thread: periodic wchan scan of im.elf's own threads, so a
     * blocked probe write can still be observed. */
    {
        long mon = sys5(120 /* clone */, 0x50F00,
                        (long)(mon_stack + sizeof(mon_stack) - 16),
                        0, 0, 0);
        if (mon == 0) {
            monitor_loop();
            for (;;)
                ;
        }
        log_int("mon_tid=", (int)mon);
    }
}

int open(const char *path, int flags, int mode)
{
    long fd;

    if (active && path && same(path, "/dev/dmpgles2")) {
        /* Prefer the real grm_gles.ko device; fall back to the fake fd. */
        fd = sys3(SYS_open, (long)path, flags, mode);
        if (fd >= 0) {
            log_str("GPU real /dev/dmpgles2 fd=");
            log_int("", (int)fd);
            gpu_fd_add((int)fd);
            return (int)fd;
        }
        fd = sys3(SYS_open, (long)"/dev/zero", 2, 0);
        if (active)
            log_open_line("open", "/dev/dmpgles2", 2, fd);
        if (fd >= 0)
            fake_add((int)fd);
        return (int)fd;
    }
    if (!(flags & O_CREAT))
        mode = 0;
    fd = sys3(SYS_open, (long)path, flags, mode);
    if (verbose && active && path && opens_seen < 160) {
        opens_seen++;
        log_open_line("open", path, flags, fd);
    }
    if (active && path && has_prefix(path, "/dev/input")) {
        log_str("INPUT open ");
        log_str(path);
        log_int(" fd=", (int)fd);
    }
    if (active && path && fd >= 0 && same(path, "/dev/ldec"))
        ldec_fd = (int)fd;
    if (active && path && fd >= 0 &&
        (same(path, "/dev/nflasha4") || same(path, "/dev/cnflasha4")))
        flash_add((int)fd);
    if (active && path && fd >= 0 &&
        (has_sub(path, ".ltt") || has_sub(path, "fontlist") ||
         has_sub(path, "/images"))) {
        log_str("FONT open ");
        log_str(path);
        log_int(" fd=", (int)fd);
    }
    return (int)fd;
}

int openat(int dirfd, const char *path, int flags, int mode)
{
    long fd;

    if (active && path && same(path, "/dev/dmpgles2")) {
        fd = sys4(SYS_openat, dirfd, (long)path, flags, mode);
        if (fd >= 0) {
            log_str("GPU real openat /dev/dmpgles2 fd=");
            log_int("", (int)fd);
            gpu_fd_add((int)fd);
            return (int)fd;
        }
        fd = sys3(SYS_open, (long)"/dev/zero", 2, 0);
        log_str("GPU openat /dev/dmpgles2 -> fd=");
        log_int("", (int)fd);
        if (fd >= 0)
            fake_add((int)fd);
        return (int)fd;
    }
    if (!(flags & O_CREAT))
        mode = 0;
    fd = sys4(SYS_openat, dirfd, (long)path, flags, mode);
    if (verbose && active && path && opens_seen < 160) {
        opens_seen++;
        log_open_line("open", path, flags, fd);
    }
    if (active && path && has_prefix(path, "/dev/input")) {
        log_str("INPUT openat ");
        log_str(path);
        log_int(" fd=", (int)fd);
    }
    if (active && path && fd >= 0 && same(path, "/dev/ldec"))
        ldec_fd = (int)fd;
    if (active && path && fd >= 0 &&
        (has_sub(path, ".ltt") || has_sub(path, "fontlist") ||
         has_sub(path, "/images"))) {
        log_str("FONT openat ");
        log_str(path);
        log_int(" fd=", (int)fd);
    }
    return (int)fd;
}

/* The font loader uses stdio, so open/openat interposition never sees it.
 * Interpose fopen/fopen64 to catch the font paths. */
typedef void *(*fopen_fn)(const char *, const char *);
static fopen_fn real_fopen;
static fopen_fn real_fopen64;

void *fopen(const char *path, const char *mode)
{
    if (!real_fopen)
        real_fopen = (fopen_fn)dlsym(RTLD_NEXT, "fopen");
    if (active && path && (has_sub(path, ".ltt") ||
                           has_sub(path, "fontlist") ||
                           has_sub(path, "/images"))) {
        log_str("FONT fopen ");
        log_str(path);
        log_int(" modeptr=", (int)(long)mode);
    }
    return real_fopen ? real_fopen(path, mode) : 0;
}

void *fopen64(const char *path, const char *mode)
{
    if (!real_fopen64)
        real_fopen64 = (fopen_fn)dlsym(RTLD_NEXT, "fopen64");
    if (active && path && (has_sub(path, ".ltt") ||
                           has_sub(path, "fontlist") ||
                           has_sub(path, "/images"))) {
        log_str("FONT fopen64 ");
        log_str(path);
    }
    return real_fopen64 ? real_fopen64(path, mode) : 0;
}

/* The emulated eMMC does not complete flushes promptly.  The display
 * service's grm_dn_vdf_if thread blocks in vfs_fsync_range, which stalls the
 * whole display/panel sequence until the exception monitor fires.  Durability
 * is irrelevant under QEMU, so acknowledge the flush and return. */
typedef int (*fsync_fn)(int);
static fsync_fn real_fsync;
static fsync_fn real_fdatasync;

int fsync(int fd)
{
    if (active) {
        log_int("FSYNC skip fd=", fd);
        return 0;
    }
    if (!real_fsync)
        real_fsync = (fsync_fn)dlsym(RTLD_NEXT, "fsync");
    return real_fsync ? real_fsync(fd) : 0;
}

int fdatasync(int fd)
{
    if (active)
        return 0;
    if (!real_fdatasync)
        real_fdatasync = (fsync_fn)dlsym(RTLD_NEXT, "fdatasync");
    return real_fdatasync ? real_fdatasync(fd) : 0;
}

void sync(void)
{
    static void (*real)(void);
    if (active)
        return;
    if (!real)
        real = (void (*)(void))dlsym(RTLD_NEXT, "sync");
    if (real)
        real();
}

/* Sony flash DFB ioctls on /dev/nflashaN (type 0x41, vendor private):
 *   0x80014123  _IOR(0x41,0x23,1)   query
 *   0x40084124  _IOW(0x41,0x24,8)   8-byte {addr,len} erase -> driver rejects
 * the emulated eMMC does not need the erase, so acknowledge it. */
#define SONY_FLASH_GET   0x80014123u
#define SONY_FLASH_ERASE 0x40084124u

static void log_flash_ioctl(unsigned long req, unsigned long arg, int ret)
{
    char b[192];
    char *p = b;
    p = put_s(p, "GPU flashioctl req=");
    p = put_x(p, req);
    p = put_s(p, " arg=");
    p = put_x(p, arg);
    if (arg >= 0x10000 && (arg & 3) == 0) {
        p = put_s(p, " w0=");
        p = put_x(p, *(volatile unsigned int *)arg);
        p = put_s(p, " w1=");
        p = put_x(p, *((volatile unsigned int *)arg + 1));
    }
    p = put_s(p, " ret=");
    p = put_x(p, (unsigned long)ret);
    *p++ = '\n';
    out(b, (int)(p - b));
}

static void gpu_dump_regions(void);

/* GPU command capture: mappings of /dev/dmpgles2 (ring / command buffers)
 * that this process owns, so their content can be sampled at doorbell time. */
struct gpu_region {
    unsigned long addr;
    unsigned long len;
};
static struct gpu_region gpu_regions[8];
static int gpu_region_count;
static int gpu_dump_done;

static void gpu_dump_blob(const char *path, unsigned long addr, unsigned long len)
{
    long fd = sys3(SYS_open, (long)path, 0x241, 0644);

    if (fd < 0) {
        log_str("GPUBLOB openfail gsc_openfail ");
        log_str(path);
        log_str("\n");
        return;
    }
    sys3(SYS_write, fd, (long)addr, (long)len);
    sys3(SYS_close, fd, 0, 0);
    log_str("GPUBLOB ");
    log_str(path);
    log_hex(" addr=", addr);
    log_hex(" len=", len);
}

static void gpu_ring_dump(unsigned long arg, int idx)
{
    char path[32];
    char *p = path;
    unsigned long base;

    p = put_s(p, "/tmp/gpuring_");
    p = put_dec(p, (unsigned long)idx);
    p = put_s(p, ".bin");
    *p = 0;
    base = (arg & ~0xfffUL) - 0x1000;
    gpu_dump_blob(path, base, 0x4000);
}


/* --- live GPU descriptor scanner (GSCAN) -------------------------------- */
static int gscan_dumps;

static void gpu_scan_descs(int tick)
{
    int i;

    if (!gpu_region_count)
        return;
    for (i = 0; i < gpu_region_count && i < 8; i++) {
        const volatile unsigned long *w =
            (const volatile unsigned long *)gpu_regions[i].addr;
        unsigned long n = gpu_regions[i].len / 4, k;
        int hits = 0;

        if (n > 0x8000)
            n = 0x8000;
        for (k = 0; k + 3 < n; k++) {
            int q;
            if (w[k] != 0x4401 || w[k + 1] != 640 || w[k + 2] != 480)
                continue;
            log_str("GSCAN ");
            log_hex("t=", (unsigned long)tick);
            log_hex(" reg=", (unsigned long)i);
            log_hex(" off=", (k - 12) * 4);
            log_hex(" surf=", k >= 5 ? w[k - 5] : 0);
            log_hex(" type=", k >= 8 ? w[k - 8] : 0);
            log_hex(" fmt=", w[k]);
            for (q = -12; q <= 5; q++) {
                if ((long)k + q < 0)
                    log_hex(" _", 0);
                else
                    log_hex(" p", w[k + q]);
            }
            log_str("\n");
            hits++;
            if (hits >= 4)
                break;
        }
        if (!hits) {
            log_str("GSCAN ");
            log_hex("t=", (unsigned long)tick);
            log_hex(" reg=", (unsigned long)i);
            log_hex(" off=", 0);
            log_hex(" surf=", 0);
            log_str(" none\n");
        }
        if (gscan_dumps < 8) {
            char path[40];
            char *p = path;
            gscan_dumps++;
            p = put_s(p, "/tmp/gsc_");
            p = put_dec(p, (unsigned long)tick);
            p = put_s(p, "_");
            p = put_dec(p, (unsigned long)i);
            p = put_s(p, ".bin");
            *p = 0;
            gpu_dump_blob(path, gpu_regions[i].addr,
                          gpu_regions[i].len > 0x20000 ? 0x20000
                                                       : gpu_regions[i].len);
        }
    }
}

int ioctl(int fd, unsigned long req, unsigned long arg)
{
    unsigned long caller_lr = (unsigned long)__builtin_return_address(0);

    if (active && gpu_fd_has(fd)) {
        static int gio_logs;

        if (gio_logs < 48) {
            gio_logs++;
            log_hex("GPUIO req=", req);
            log_hex("GPUIO arg=", arg);
            log_hex("GPUIO lr=", caller_lr);
            {
                static unsigned long lb;
                if (!lb)
                    lb = libobj_base();
                log_hex("GPUIO loff=", caller_lr - lb);
            }
            if (arg > 0x10000 && (arg & 3) == 0) {
                log_hex("GPUIO w0=",
                        *(volatile unsigned long *)arg);
                log_hex("GPUIO w1=",
                        *(volatile unsigned long *)(arg + 4));
                log_hex("GPUIO w2=",
                        *(volatile unsigned long *)(arg + 8));
                log_hex("GPUIO w3=",
                        *(volatile unsigned long *)(arg + 12));
            }
        }
        if (req == 0xc004820du) {
            doorbells++;
            if (doorbells <= 2)
                gpu_ring_dump(arg, doorbells);
            if (doorbells <= 40) {
                log_hex("GDBELL #=", (unsigned long)doorbells);
                if (gpu_region_count > 0)
                    log_bytes("GRINGREG", (const void *)gpu_regions[0].addr,
                              0x40);
                if (gpu_region_count > 0)
                    log_hex("GRINGPHYS=",
                            virt_to_phys(gpu_regions[0].addr));
                if (arg && maps_contains(arg))
                    log_bytes("GRINGARG", (const void *)arg, 0x20);
            }
            if (doorbells == 16)
                gpu_dump_regions();
        }
        if (req == 0xc0048211u && arg > 0x10000) {
            static int cmd_dumps;
            static int cmd_logs;
            unsigned long w1 = *(volatile unsigned long *)(arg + 4);

            if (cmd_logs < 40) {
                log_hex("GCMD idx=", (unsigned long)cmd_logs);
                log_hex("GCMD arg=", arg);
                log_hex("GCMD w1=", w1);
                if (w1 && maps_contains(w1))
                    log_bytes("GCMDBUF", (const void *)w1, 0x60);
                cmd_logs++;
            }
            if (cmd_dumps < 8 && w1 > 0x10000 && w1 < 0x40000000UL) {
                char pth[32];
                char *pp = pth;
                pp = put_s(pp, "/tmp/gpucmd_");
                pp = put_dec(pp, (unsigned long)cmd_dumps);
                pp = put_s(pp, ".bin");
                *pp = 0;
                gpu_dump_blob(pth, w1, 0x4000);
                cmd_dumps++;
            }
        }
        if (req == 0xc0048202u && arg > 0x10000) {
            static int xl_logs;
            unsigned long in = *(volatile unsigned long *)arg;
            long r = sys3(SYS_ioctl, fd, req, arg);
            if (xl_logs < 32) {
                xl_logs++;
                log_hex("GXLATE idx=", in);
                log_hex("GXLATE addr=", *(volatile unsigned long *)arg);
                log_hex("GXLATE ret=", (unsigned long)r);
            }
            return (int)r;
        }
    }
    if (active && fd == ldec_fd) {
        volatile unsigned long *pp = (volatile unsigned long *)arg;
        unsigned long src = pp[0], slen = pp[1], dst = pp[2], dlen = pp[3];

        if (trace_on) { /* GATE_LOGS */
            log_hex("LDEC src=", src);
            log_hex("LDEC slen=", slen);
            log_hex("LDEC dst=", dst);
            log_hex("LDEC dlen=", dlen);
        }
        if (req == 1 && src && dst && slen && dlen &&
            slen < 0x800000UL && dlen < 0x800000UL) {
            int sv = maps_contains(src), dv = maps_contains(dst);
            long sr = 0, dr = 0;
            unsigned long sm = 0, dm = 0;
            unsigned char *sp = 0, *dp = 0;

            log_hex("LDEC srcvirt=", (unsigned long)sv);
            log_hex("LDEC dstvirt=", (unsigned long)dv);
            if (ldec_mem_fd < 0)
                ldec_mem_fd = (int)sys3(SYS_open, (long)"/dev/mem", 2, 0);
            if (sv) {
                sp = (unsigned char *)src;
            } else {
                unsigned long sb = src & ~0xfffUL, so = src - sb;
                sm = (so + slen + 0xfffUL) & ~0xfffUL;
                sr = sys6(SYS_mmap2, 0, sm, 3, 1, ldec_mem_fd, sb >> 12);
                if (sr >= 0)
                    sp = (unsigned char *)(sr + so);
            }
            if (dv) {
                dp = (unsigned char *)dst;
            } else {
                unsigned long db = dst & ~0xfffUL, dof = dst - db;
                dm = (dof + dlen + 0xfffUL) & ~0xfffUL;
                dr = sys6(SYS_mmap2, 0, dm, 3, 1, ldec_mem_fd, db >> 12);
                if (dr >= 0)
                    dp = (unsigned char *)(dr + dof);
            }
            if (sp && dp) {
                int rr;
                log_hex("LDEC b0=", *(volatile unsigned long *)sp);
                log_hex("LDEC b1=", *(volatile unsigned long *)(sp + 4));
                rr = ui_lz77_decode(sp, slen, dp, dlen);
                log_hex("LDEC dec=", (unsigned long)rr);
                shim_barrier();
            } else {
                log_hex("LDEC mmap_fail=",
                        (unsigned long)(sr < 0 ? sr : dr));
            }
            if (sr >= 0)
                sys3(SYS_munmap, sr, sm, 0);
            if (dr >= 0)
                sys3(SYS_munmap, dr, dm, 0);
        }
        return 0;
    }
    if (active && flash_has(fd)) {
        int r;
        if (req == SONY_FLASH_ERASE || req == BLK_DISCARD ||
            req == BLK_SECDISCARD || req == BLK_ZEROOUT)
            r = 0;                       /* pretend the erase succeeded */
        else
            r = (int)sys3(SYS_ioctl, fd, req, arg);
        if (verbose && flash_ioctls < 16) {
            flash_ioctls++;
            log_flash_ioctl(req, arg, r);
        }
        return r;
    }

    if (active && fake_has(fd)) {
        int quiet = (req == 0xc004820du && doorbells > 24);
        if (verbose && req == 0xc004820eu) {
            unsigned long caller;
            char b[64];
            char *p = b;
            __asm__ volatile ("mov %0, lr" : "=r"(caller));
            p = put_s(p, "GPU 820e caller=");
            p = put_x(p, caller);
            *p++ = '\n';
            out(b, (int)(p - b));
        }
        if (verbose && !quiet) {
            log_str("GPU ioctl cmd=");
            log_hex("", req);
            log_int(" in=", (int)get_u32(arg));
        }
        switch (req) {
        case 0xc004820du: {
            /* ring doorbell: the real driver blocks until the GPU consumed
             * the command.  A short busy-wait keeps the app's worker threads
             * from racing ahead of their own initialisation. */
            volatile int i;
            doorbells++;
            if (doorbells == 16)
                gpu_dump_regions();
            if (verbose && (doorbells % 256) == 0) {
                log_str("GPU doorbells=");
                log_int("", doorbells);
            }
            for (i = 0; i < 40000; i++)
                ;
            return 0;
        }
        case CMD_GET_REG:
            put_u32(arg, 0x4000);       /* register window, real driver */
            return 0;
        case CMD_XLATE: {
            unsigned int idx = get_u32(arg);
            unsigned int v = (idx == 0) ? 0x80000000u
                           : (idx == 1) ? 0x50000000u : 0u;
            put_u32(arg, v);
            return 0;
        }
        case CMD_STATE:
            put_u32(arg, 0);            /* hw bypass feature: disabled */
            return 0;
        case CMD_WAIT_A:
        case CMD_WAIT_B:
            put_u32(arg, 0);            /* operation done */
            return 0;
        default:
            return 0;                   /* permissive bring-up */
        }
    }
    return (int)sys3(SYS_ioctl, fd, req, arg);
}

static int mmap_logs;

static void gpu_dump_regions(void)
{
    int i;

    if (gpu_dump_done)
        return;
    gpu_dump_done = 1;
    log_int("GPUDUMP regions=", gpu_region_count);
    for (i = 0; i < gpu_region_count; i++) {
        char path[40];
        char *p = path;
        long fd;
        unsigned long n = gpu_regions[i].len;

        if (n > 0x20000)
            n = 0x20000;
        p = put_s(p, "/tmp/gpudump_");
        p = put_dec(p, (unsigned long)i);
        p = put_s(p, ".bin");
        *p = 0;
        fd = sys3(SYS_open, (long)path, 0x241 /*O_WRONLY|O_CREAT|O_TRUNC*/,
                  0644);
        if (fd < 0)
            continue;
        sys3(SYS_write, fd, (long)gpu_regions[i].addr, (long)n);
        sys3(SYS_close, fd, 0, 0);
        log_hex("GPUDUMP addr=", gpu_regions[i].addr);
        log_hex("GPUDUMP len=", n);
    }
}

void *mmap(void *addr, usize len, int prot, int flags, int fd, long off)
{
    long r;

    if (active && fake_has(fd)) {
        if (verbose) {
            log_str("GPU mmap len=");
            log_hex("", (unsigned long)len);
            log_hex(" prot=", (unsigned long)(prot & PROT_MASK));
            log_hex(" off=", (unsigned long)off);
        }
        r = sys6(SYS_mmap2, (long)addr, (long)len, prot,
                 MAP_PRIVATE_ANON, -1, 0);
        if (verbose)
            log_hex(" ->", (unsigned long)r);
        if (r >= 0 && gpu_region_count < 8) {
            gpu_regions[gpu_region_count].addr = r;
            gpu_regions[gpu_region_count].len = len;
            gpu_region_count++;
        }
        if (r < 0 && r > -4096)
            return (void *)-1;
        return (void *)r;
    }
    /* pass-through with the raw syscall: never call dlsym from here, the
     * loader itself calls mmap while relocating. */
    r = sys6(SYS_mmap2, (long)addr, (long)len, prot, flags, fd, off / 4096);
    if (active && gpu_fd_has(fd) && r >= 0 && gpu_region_count < 8) {
        gpu_regions[gpu_region_count].addr = r;
        gpu_regions[gpu_region_count].len = len;
        gpu_region_count++;
        log_hex("GPU region=", (unsigned long)r);
        log_hex("GPU region len=", (unsigned long)len);
        log_hex("GPU region off=", (unsigned long)off);
    }
    if (verbose && active && fd >= 0 && mmap_logs < 48) {
        char b[112];
        char *p = b;
        mmap_logs++;
        p = put_s(p, "GPU mmap fd=");
        p = put_x(p, (unsigned long)fd);
        p = put_s(p, " len=");
        p = put_x(p, (unsigned long)len);
        p = put_s(p, " off=");
        p = put_x(p, (unsigned long)off);
        p = put_s(p, " ret=");
        p = put_x(p, (unsigned long)r);
        *p++ = '\n';
        out(b, (int)(p - b));
    }
    if (r < 0 && r > -4096)
        return (void *)-1;
    return (void *)r;
}

int munmap(void *addr, usize len)
{
    return (int)sys3(SYS_munmap, (long)addr, (long)len, 0);
}

int close(int fd)
{
    if (active && fake_has(fd))
        fake_del(fd);
    if (active && gpu_fd_has(fd))
        gpu_fd_del(fd);
    if (active && flash_has(fd))
        flash_del(fd);
    return (int)sys3(SYS_close, fd, 0, 0);
}

/* --- probe thread: dump per-thread comm/wchan/syscall ------------------- */
/* Runs inside im.elf, uses raw syscalls only (no TLS, no libc). */

static char *put_dec(char *p, unsigned long v)
{
    char t[24];
    int i = 0;
    if (!v) {
        *p++ = '0';
        return p;
    }
    while (v && i < 24) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        *p++ = t[--i];
    return p;
}

static void probe_read(const char *path, char *buf, int max)
{
    int fd = (int)sys3(SYS_open, (long)path, 0, 0);
    int n;
    buf[0] = 0;
    if (fd < 0)
        return;
    n = (int)sys3(SYS_read, fd, (long)buf, max - 1);
    sys3(SYS_close, fd, 0, 0);
    if (n <= 0) {
        buf[0] = 0;
        return;
    }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '))
        n--;
    buf[n] = 0;
}

static void probe_once(void)
{
    unsigned long tid;
    for (tid = 1; tid < 600; tid++) {
        char path[64];
        char comm[64];
        char sc[128];
        char wch[64];
        char line[420];
        char *p;

        p = path;
        p = put_s(p, "/proc/self/task/");
        p = put_dec(p, tid);
        p = put_s(p, "/comm");
        *p = 0;
        probe_read(path, comm, sizeof(comm));
        if (!comm[0])
            continue;
        p = path;
        p = put_s(p, "/proc/self/task/");
        p = put_dec(p, tid);
        p = put_s(p, "/syscall");
        *p = 0;
        probe_read(path, sc, sizeof(sc));
        p = path;
        p = put_s(p, "/proc/self/task/");
        p = put_dec(p, tid);
        p = put_s(p, "/wchan");
        *p = 0;
        probe_read(path, wch, sizeof(wch));

        p = line;
        p = put_s(p, "PROBE t=");
        p = put_dec(p, tid);
        p = put_s(p, " c=");
        p = put_s(p, comm);
        p = put_s(p, " s=");
        p = put_s(p, sc);
        p = put_s(p, " w=");
        p = put_s(p, wch);
        *p++ = '\n';
        sys3(SYS_write, 1, (long)line, (long)(p - line));
    }
}

static void probe_procs(void)
{
    unsigned long pid;
    for (pid = 1; pid < 600; pid++) {
        char path[48];
        char comm[64];
        char line[112];
        char *p;

        p = path;
        p = put_s(p, "/proc/");
        p = put_dec(p, pid);
        p = put_s(p, "/comm");
        *p = 0;
        probe_read(path, comm, sizeof(comm));
        if (!comm[0])
            continue;
        p = line;
        p = put_s(p, "PROC pid=");
        p = put_dec(p, pid);
        p = put_s(p, " c=");
        p = put_s(p, comm);
        *p++ = '\n';
        sys3(SYS_write, 1, (long)line, (long)(p - line));
    }
}

static int hv(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int eq_at(const char *h, const char *n)
{
    int i;
    for (i = 0; n[i]; i++)
        if (h[i] != n[i])
            return 0;
    return 1;
}

static unsigned long module_base(const char *needle)
{
    static char mbuf[262144];
    int fd, n, total = 0, i, j;

    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return 0;
    while (total < (int)sizeof(mbuf) - 1) {
        n = (int)sys3(SYS_read, fd, (long)(mbuf + total),
                      (long)(sizeof(mbuf) - 1 - total));
        if (n <= 0)
            break;
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    if (total <= 0)
        return 0;
    n = total;
    mbuf[n] = 0;

    for (i = 0; i + 8 < n; i++) {
        if (!eq_at(mbuf + i, needle))
            continue;
        j = i;
        while (j > 0 && mbuf[j - 1] != '\n')
            j--;
        {
            unsigned long start = 0, off = 0;
            int d;
            while (j < i && (d = hv(mbuf[j])) < 0)
                j++;
            while (j < i && (d = hv(mbuf[j])) >= 0) {
                start = (start << 4) | (unsigned long)d;
                j++;
            }
            /* maps line: start-end perms offset dev inode path.
             * The ELF base is start - mapping offset. */
            while (j < i && mbuf[j] != ' ')
                j++;
            while (j < i && mbuf[j] == ' ')
                j++;
            while (j < i && mbuf[j] != ' ')
                j++;
            while (j < i && mbuf[j] == ' ')
                j++;
            while (j < i && (d = hv(mbuf[j])) >= 0) {
                off = (off << 4) | (unsigned long)d;
                j++;
            }
            return start - off;
        }
    }
    return 0;
}

static unsigned long libobj_base(void)
{
    return module_base("libObj.so");
}

static int maps_parse_buf(const char *buf, int n, unsigned long addr)
{
    int i = 0;
    while (i < n) {
        unsigned long start = 0, end = 0;
        int d;
        while (i < n && (d = hv(buf[i])) >= 0) {
            start = (start << 4) | (unsigned long)d;
            i++;
        }
        if (i < n && buf[i] == '-')
            i++;
        while (i < n && (d = hv(buf[i])) >= 0) {
            end = (end << 4) | (unsigned long)d;
            i++;
        }
        if (start && end && addr >= start && addr < end)
            return 1;
        while (i < n && buf[i] != '\n')
            i++;
        i++;
    }
    return 0;
}

static int maps_contains(unsigned long addr)
{
    static char mbuf[262144];
    static int cached_total; /* MAPS_CACHE: 200 ms TTL + refresh on miss */
    static unsigned long long cached_ns;
    struct { long sec; long nsec; } ts;
    unsigned long long now = 0;
    int fd, n, total = 0;

    if (sys3(263 /* clock_gettime */, 1, (long)&ts, 0) == 0)
        now = (unsigned long long)ts.sec * 1000000000ULL +
              (unsigned long long)ts.nsec;
    if (cached_total > 0 && now && now - cached_ns < 200000000ULL &&
        maps_parse_buf(mbuf, cached_total, addr))
        return 1;

    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return 0;
    while (total < (int)sizeof(mbuf) - 1) {
        n = (int)sys3(SYS_read, fd, (long)(mbuf + total),
                      (long)(sizeof(mbuf) - 1 - total));
        if (n <= 0)
            break;
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    if (total <= 0)
        return 0;
    if (now) {
        cached_total = total;
        cached_ns = now;
    }
    return maps_parse_buf(mbuf, total, addr);
}

/* Translate a mapped userspace address to guest physical via pagemap. */
static unsigned long virt_to_phys(unsigned long v)
{
    int fd;
    long off = (long)((v >> 12) * 8);
    unsigned long long e = 0;

    fd = (int)sys3(SYS_open, (long)"/proc/self/pagemap", 0, 0);
    if (fd < 0)
        return 0;
    sys4(SYS_lseek, fd, off, 0, 0);
    if (sys3(SYS_read, fd, (long)&e, 8) != 8) {
        sys3(SYS_close, fd, 0, 0);
        return 0;
    }
    sys3(SYS_close, fd, 0, 0);
    if (!(e & (1ULL << 63)))
        return 0;
    return (unsigned long)(((e & ((1ULL << 55) - 1)) << 12) | (v & 0xfff));
}

static int has_sub(const char *h, const char *n)
{
    int i, j;
    for (i = 0; h[i]; i++) {
        for (j = 0; n[j] && h[i + j] == n[j]; j++)
            ;
        if (!n[j])
            return 1;
    }
    return 0;
}

typedef unsigned long (*app_init_fn)(unsigned long, unsigned long,
                                     unsigned long, unsigned long);
static app_init_fn real_app_init;

static unsigned long app_init_thunk(unsigned long a, unsigned long b,
                                    unsigned long c, unsigned long d)
{
    unsigned long r;
    log_str("APPINIT enter\n");
    log_hex("  a=", a);
    log_hex("  b=", b);
    log_hex("  c=", c);
    r = real_app_init ? real_app_init(a, b, c, d) : 0;
    log_hex("APPINIT return=", r);
    return r;
}

static app_init_fn real_app_log;
static unsigned long app_log_thunk(unsigned long a, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    static int n;
    unsigned long lr, r;
    __asm__ volatile ("mov %0, lr" : "=r"(lr));
    if (trace_on && n < 32) {
        n++;
        log_str("LOGCALL lr=");
        log_hex("", lr);
        log_hex("  a=", a);
    }
    r = real_app_log ? real_app_log(a, b, c, d) : 0;
    return r;
}

static app_init_fn real_app_init2;
static unsigned long app_init2_thunk(unsigned long a, unsigned long b,
                                     unsigned long c, unsigned long d)
{
    unsigned long r;
    log_str("APPINIT-B enter\n");
    log_hex("  a=", a);
    r = real_app_init2 ? real_app_init2(a, b, c, d) : 0;
    log_hex("APPINIT-B return=", r);
    return r;
}

static app_init_fn real_app_init3;
static unsigned long app_init3_thunk(unsigned long a, unsigned long b,
                                     unsigned long c, unsigned long d)
{
    unsigned long r;
    log_str("APPINIT-C enter\n");
    log_hex("  a=", a);
    r = real_app_init3 ? real_app_init3(a, b, c, d) : 0;
    log_hex("APPINIT-C return=", r);
    return r;
}

#define SYS_mprotect   125
#define SYS_cacheflush 0x0f0002

static void thumb_jump(unsigned char *where, unsigned long to)
{
    where[0] = 0xdf;
    where[1] = 0xf8;
    where[2] = 0x00;
    where[3] = 0xf0;
    *(unsigned long *)(where + 4) = to;
}

static void cache_flush(void *p, int n)
{
    sys3(SYS_cacheflush, (long)p, (long)((char *)p + n), 0);
}

static int hook_one(unsigned long base, unsigned long off,
                    const unsigned char *pro, void *repl, void **orig)
{
    unsigned char *a = (unsigned char *)(base + off);
    unsigned char *t;
    int i;

    for (i = 0; i < 8; i++) {
        if (a[i] != pro[i]) {
            log_hex("HOOK mismatch ", off);
            return -1;
        }
    }
    t = (unsigned char *)sys6(SYS_mmap2, 0, 4096, 7, 0x22, -1, 0);
    if ((long)t < 0) {
        log_str("HOOK mmap fail\n");
        return -1;
    }
    for (i = 0; i < 8; i++)
        t[i] = a[i];
    thumb_jump(t + 8, ((unsigned long)a + 8) | 1);
    *orig = (void *)((unsigned long)t | 1);
    if (sys3(SYS_mprotect, (long)((unsigned long)a & ~0xfffUL), 4096, 7) < 0) {
        log_str("HOOK mprotect fail\n");
        return -1;
    }
    thumb_jump(a, (unsigned long)repl);
    cache_flush(a, 8);
    cache_flush(t, 16);
    log_hex("HOOK installed ", off);
    return 0;
}

/* Length-aware variant: copy n whole instructions so a 32-bit Thumb-2
 * instruction straddling byte 8 is not split. */
static int hook_one_n(unsigned long base, unsigned long off, int n,
                      const unsigned char *pro, void *repl, void **orig)
{
    unsigned char *a = (unsigned char *)(base + off);
    unsigned char *t;
    int i;

    for (i = 0; i < n; i++) {
        if (a[i] != pro[i]) {
            log_hex("HOOK mismatch ", off);
            return -1;
        }
    }
    t = (unsigned char *)sys6(SYS_mmap2, 0, 4096, 7, 0x22, -1, 0);
    if ((long)t < 0)
        return -1;
    for (i = 0; i < n; i++)
        t[i] = a[i];
    {
        int p = (n + 3) & ~3;
        for (i = n; i + 1 < p; i += 2) {
            t[i] = 0x00;
            t[i + 1] = 0xbf;   /* 16-bit Thumb NOP */
        }
        thumb_jump(t + p, ((unsigned long)a + n) | 1);
    }
    *orig = (void *)((unsigned long)t | 1);
    if (sys3(SYS_mprotect, (long)((unsigned long)a & ~0xfffUL), 4096, 7) < 0)
        return -1;
    thumb_jump(a, (unsigned long)repl);
    cache_flush(a, 8);
    cache_flush(t, n + 16);
    log_hex("HOOKN installed ", off);
    return 0;
}

typedef unsigned long (*draw_fn)(unsigned long, unsigned long,
                                 unsigned long, unsigned long);
static draw_fn real_draw;

static volatile int composed;
static volatile int compose_seq;

static void draw_glyph(unsigned short *p, int stride, int x, int y,
                       int ch, int scale, unsigned short fg)
{
    const unsigned char *g;
    int r, c, sy, sx;

    if (ch < 32 || ch > 127)
        return;
    g = font8x8[ch - 32];
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            if (g[r] & (0x80 >> c))
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        p[(y + r * scale + sy) * stride + x + c * scale + sx] = fg;
}

static void draw_text(unsigned short *p, int stride, int x, int y,
                      const char *s, int scale, unsigned short fg)
{
    while (*s) {
        draw_glyph(p, stride, x, y, *s, scale, fg);
        x += 8 * scale + scale;
        s++;
    }
}

static void draw_uint(unsigned short *p, int stride, int x, int y,
                      unsigned long v, int scale, unsigned short fg)
{
    char t[12];
    int i = 0;
    if (!v) {
        draw_glyph(p, stride, x, y, '0', scale, fg);
        return;
    }
    while (v && i < 10) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        draw_glyph(p, stride, x, y, t[--i], scale, fg), x += 8 * scale + scale;
}

/* Software producer for the composition surface: the app only uses
 * 0x202E0200 as a Sugilite blit source; its real producer is the
 * grm_gles/DMP ring, which is not emulated.  Refresh it with a visible,
 * live image so the blit path to the panel can be verified end to end. */
static void compose_frame(int seq)
{
    int fd, x, y;
    long m;
    unsigned short *p;
    const unsigned long base = 0x202E0000UL;
    const unsigned long off = 0x200UL;
    const unsigned long map_len = 0x98000UL;
    int bar;

    fd = (int)sys3(SYS_open, (long)"/dev/mem", 2, 0);
    if (fd < 0) {
        log_str("COMPOSE nodem\n");
        return;
    }
    m = sys6(SYS_mmap2, 0, (long)map_len, 3, 1, fd, (long)(base >> 12));
    if (m < 0 && m > -4096) {
        log_hex("COMPOSE mmap=", (unsigned long)m);
        sys3(SYS_close, fd, 0, 0);
        return;
    }
    p = (unsigned short *)(m + off);
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++)
            p[y * 640 + x] = (unsigned short)(((x * 31 / 639) << 11) |
                                              ((y * 63 / 479) << 5) |
                                              (x * 31 / 639));
    draw_text(p, 640, 40, 30, "A7R3 QEMU", 6, 0xFFFF);
    draw_text(p, 640, 40, 110, "FALLBACK PRODUCER", 3, 0xFFE0);
    draw_text(p, 640, 40, 170, "FRAME", 3, 0x07FF);
    draw_uint(p, 640, 40 + (5 * 25), 170, (unsigned long)seq, 3, 0x07FF);
    bar = 40 + ((seq * 37) % 520);
    for (y = 400; y < 440; y++)
        for (x = 40; x < bar; x++)
            p[y * 640 + x] = 0xFFFF;
    shim_barrier();
    sys3(SYS_munmap, m, (long)map_len, 0);
    sys3(SYS_close, fd, 0, 0);
    log_int("COMPOSE seq=", seq);
}

static unsigned long hook_draw(unsigned long a, unsigned long b,
                               unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    draw_seen = 1;
    if (!composed && file_exists("/usr/tool/produce")) {
        composed = 1;
        compose_frame(compose_seq);
    }
    {
        static int hd;
        if (++hd == 10)
            scan_self_tasks();
        if (hd == 8 && file_exists("/usr/tool/open_view"))
            open_view_once();
        if (hd == 12) {
            dump_uxc_maps();
            probe_proc_input();
        }
        if (hd == 60)
            probe_proc_input();
        if (hd == 12) {
            dump_uxc_maps();
            dump_event_table();
            dump_sysfs_bus("/sys/bus", 0);
            dump_sysfs_bus("/sys/bus/udif/devices", 0);
            dump_sysfs_bus("/sys/bus/platform/devices", 1);
            dump_sysfs_bus("/sys/module", 1);
        }
        if (hd == 18)
            dump_uxc_maps();
        if (hd == 9)
            dump_uxc_maps();
        if (hd == 10)
            inject_infra_key_g(0x1abUL, 1, 0);
        if (hd == 11)
            inject_infra_key_g(0x1abUL, 0, 0);
        if (hd == 20)
            dump_uxc_maps();
        if (hd == 21)
            inject_infra_key_g(0x1acUL, 1, 0);
        if (hd == 22)
            inject_infra_key_g(0x1acUL, 0, 0);
        if (hd == 30)
            dump_uxc_maps();
        if (hd == 18)
            dump_uxc_maps();
        if (hd == 22) {
            log_hex("HD keyid 0x1ab", (unsigned long)hd);
            inject_key_by_id(0x15010110UL);
        }
        if (hd == 30) {
            log_hex("HD keyid 0xa7", (unsigned long)hd);
            inject_key_by_id(0x15010030UL);
        }
        if (hd == 38) {
            log_hex("HD keyid 0x2", (unsigned long)hd);
            inject_key_by_id(0x15010010UL);
        }
        if (hd == 42)
            dump_uxc_maps();
        if (hd == 48) {
            log_hex("HD keyid 0x1f1", (unsigned long)hd);
            inject_key_by_id(0x15010020UL);
        }
        if (hd == 56)
            dump_uxc_maps();
        if (hd == 40)
            dump_uxc_maps();
        if (file_exists("/usr/tool/viewprobe")) {
            if (hd == 24)
                open_view_named("view/SETTINGMENUX");
            if (hd == 48)
                open_view_named("view/GLOBAL_MENU");
            if (hd == 72)
                open_view_named("view/FNMENU");
            if (hd == 96)
                open_view_named("view/HARDKEYMENU_DISP");
        }
    }
    if (trace_on && n < 16) {
        n++;
        log_str("DRAW enter\n");
        log_hex("  a=", a);
    }
    r = real_draw ? real_draw(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("DRAW return=", r);
    return r;
}

static draw_fn real_draw2;
static unsigned long hook_draw2(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    if (trace_on && n < 16) {
        n++;
        log_str("DRAW2 enter\n");
        log_hex("  a=", a);
    }
    r = real_draw2 ? real_draw2(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("DRAW2 return=", r);
    return r;
}

static draw_fn real_notify;
static unsigned long hook_notify(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    if (trace_on && n < 16) {
        n++;
        log_str("NOTIFY enter\n");
        log_hex("  a=", a);
    }
    r = real_notify ? real_notify(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("NOTIFY return=", r);
    return r;
}

static draw_fn real_notify2;
static unsigned long hook_notify2(unsigned long a, unsigned long b,
                                  unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    if (trace_on && n < 16) {
        n++;
        log_str("NOTIFY2 enter\n");
        log_hex("  a=", a);
    }
    r = real_notify2 ? real_notify2(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("NOTIFY2 return=", r);
    return r;
}

static draw_fn real_dmpfill;
static unsigned long dmpfill_thunk(unsigned long a, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    if (trace_on && n < 16) {
        n++;
        log_str("DMPFILL enter\n");
        log_hex("  a=", a);
        log_hex("  b=", b);
        log_hex("  c=", c);
    }
    r = real_dmpfill ? real_dmpfill(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("DMPFILL return=", r);
    return r;
}

static draw_fn real_headless;
static unsigned long headless_thunk(unsigned long a, unsigned long b,
                                    unsigned long c, unsigned long d)
{
    static int n;
    unsigned long r;
    if (n < 4) {
        n++;
        log_str("HEADLESS enter\n");
        log_hex("  a=", a);
        log_hex("  b=", b);
        if (a > 0x10000 && a < 0x60000000) {
            unsigned long bm, md;
            log_hex("  r+8=", *(unsigned long *)(a + 8));
            log_hex("  r+20=", *(unsigned long *)(a + 20));
            log_hex("  r+24=", *(unsigned long *)(a + 24));
            bm = *(unsigned long *)(a + 28);
            log_hex("  r+28 bitmap=", bm);
            if (bm > 0x10000 && bm < 0x60000000) {
                log_hex("  bm vt=", *(unsigned long *)bm);
                md = *(unsigned long *)(bm + 4);
                log_hex("  bm+4 md=", md);
                if (md > 0x10000 && md < 0x60000000) {
                    log_hex("  md+0x24 phys=", *(unsigned long *)(md + 0x24));
                    log_hex("  md+0x1c=", *(unsigned long *)(md + 0x1c));
                }
            }
        }
    }
    r = real_headless ? real_headless(a, b, c, d) : 0;
    if (n <= 16)
        log_hex("HEADLESS return=", r);
    return r;
}

static const unsigned char pro_app[8] = {0xb0, 0xb5, 0x8a, 0xb0,
                                         0x02, 0xaf, 0x05, 0x46};
static const unsigned char pro_dmpfill[8] = {0x02, 0x29, 0x0b, 0x46,
                                             0xf0, 0xb5, 0x85, 0xb0};
static const unsigned char pro_headless[8] = {0xb0, 0xb5, 0x0c, 0x46,
                                              0x86, 0xb0, 0x1c, 0x49};
static const unsigned char pro_draw[8] = {0xb0, 0xb5, 0x04, 0x46,
                                          0x82, 0xb0, 0x00, 0xaf};
static const unsigned char pro_notify[8] = {0x90, 0xb5, 0x87, 0xb0,
                                            0x00, 0xaf, 0x04, 0x46};

static void install_ui_hooks(unsigned long base)
{
    if (!base)
        return;
    log_hex("UIHOOK base=", base);
    hook_one(base, 0x82f1c4, pro_app, (void *)app_log_thunk,
             (void **)&real_app_log);
    hook_one(base, 0x5cef88, pro_draw, (void *)hook_draw,
             (void **)&real_draw);
    hook_one(base, 0x71f080, pro_draw, (void *)hook_draw2,
             (void **)&real_draw2);
    hook_one(base, 0x5cd544, pro_notify, (void *)hook_notify,
             (void **)&real_notify);
    hook_one(base, 0x6314b2, pro_notify, (void *)hook_notify2,
             (void **)&real_notify2);
    hook_one(base, 0x76c580, pro_dmpfill, (void *)dmpfill_thunk,
             (void **)&real_dmpfill);
    hook_one(base, 0x700374, pro_headless, (void *)headless_thunk,
             (void **)&real_headless);
}

/* ---- Model / InputService creation probes ------------------------------
 * Same 8-byte-prologue trampoline as the UI hooks; only samples the first
 * calls and logs LR so the caller can be resolved against the libObj base. */
static draw_fn real_model_initialize, real_input_create, real_mcs_app;
static draw_fn real_mroot, real_mboot, real_mdisplay, real_mcamera;

static unsigned long trace_call(const char *tag, int *n, draw_fn real,
                                unsigned long lr, unsigned long a,
                                unsigned long b, unsigned long c,
                                unsigned long d)
{
    unsigned long r;
    int logit = (*n < 8);

    if (logit) {
        (*n)++;
        log_str(tag);
        log_hex(" a=", a);
        log_hex(" b=", b);
        log_hex(" c=", c);
        log_hex(" lr=", lr);
    }
    r = real ? real(a, b, c, d) : 0;
    if (logit)
        log_hex(" ret=", r);
    return r;
}

#define TRACE_WRAP(name, tag, real)                                     \
    static int n_##name;                                                \
    static unsigned long th_##name(unsigned long a, unsigned long b,    \
                                   unsigned long c, unsigned long d)    \
    {                                                                   \
        unsigned long lr;                                               \
        __asm__ volatile ("mov %0, lr" : "=r"(lr));                    \
        return trace_call(tag, &n_##name, real, lr, a, b, c, d);        \
    }
static int n_model_initialize;
static unsigned long g_model;
static unsigned long th_model_initialize(unsigned long a, unsigned long b,
                                         unsigned long c, unsigned long d)
{
    unsigned long r;
    g_model = a;
    g_model_seen = 1;
    log_hex("MODELCAP a=", a);
    r = real_model_initialize ? real_model_initialize(a, b, c, d) : 0;
    log_hex("MODEL_RET=", r);
    return r;
}

TRACE_WRAP(input_create, "INPUT_CREATE", real_input_create)
TRACE_WRAP(mcs_app, "MODEL_APPSTRUCT", real_mcs_app)
TRACE_WRAP(mroot, "MODEL_ROOT", real_mroot)
TRACE_WRAP(mboot, "MODEL_BOOT", real_mboot)
TRACE_WRAP(mdisplay, "MODEL_DISPLAY", real_mdisplay)
TRACE_WRAP(mcamera, "MODEL_CAMERA", real_mcamera)

static draw_fn real_dmp_init, real_dmp_exit, real_dmp_fillA, real_dmp_fillB;
static draw_fn real_dmp_blitA, real_dmp_blitB;
static draw_fn real_errhook;
static draw_fn real_assert_unused;
static draw_fn real_gem_solid, real_gem_rect, real_gem_quad, real_gem_line;
static draw_fn real_headless;
static draw_fn real_engine;
static draw_fn real_rend;
static draw_fn real_gpe_fill, real_gpe_blit;
static unsigned long th_gpe_fill(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;

    if (n < 8) {
        n++;
        log_hex("GPE fill=", a);
        log_hex("GPE fill c=", c);
    }
    return real_gpe_fill ? real_gpe_fill(a, b, c, d) : 0;
}
static unsigned long th_gpe_blit(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;

    if (n < 8) {
        n++;
        log_hex("GPE blit=", a);
        log_hex("GPE blit c=", c);
    }
    return real_gpe_blit ? real_gpe_blit(a, b, c, d) : 0;
}
static unsigned long th_rend_get(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    unsigned long lb = libobj_base();
    unsigned long g = 0;
    static int n;

    if (lb)
        g = *(volatile unsigned long *)(lb + 0x12a1532UL);
    if (n < 8) {
        n++;
        log_hex("REND g=", g);
        if (g && g < 0x40000000UL)
            log_hex("REND vt=", *(volatile unsigned long *)g);
    }
    return g;
}
static unsigned long th_engine(unsigned long a, unsigned long b,
                               unsigned long c, unsigned long d)
{
    static int n;

    if (n < 8) {
        n++;
        log_hex("ENGINE wait=", a);
    }
    return 0x04000000UL;
}
static unsigned long th_headless(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;

    if (g_manager) {
        if (n < 12) {
            n++;
            log_hex("HEADLESS skip=", a);
        }
        return 0;
    }
    return real_headless ? real_headless(a, b, c, d) : 0;
}
static unsigned long th_errhook(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    static int n;

    if (n < 24) {
        n++;
        log_hex("ERRHOOK=", a);
        if (n <= 6) {
            unsigned long lr, sp, lb;
            int i;

            __asm__ volatile ("mov %0, lr" : "=r"(lr));
            __asm__ volatile ("mov %0, sp" : "=r"(sp));
            lb = libobj_base();
            log_hex(" ERRLR=", lr);
            log_hex(" ERRSP=", sp);
            if (lb) {
                if (lr >= lb && lr < lb + 0xe00000UL)
                    log_hex(" ERRLRoff=", lr - lb);
                for (i = 1; i < 20; i++) {
                    unsigned long v = ((unsigned long *)(sp & ~3UL))[i];
                    if (v >= lb && v < lb + 0xe00000UL)
                        log_hex(" ERRSTK=", v - lb);
                }
            }
        }
    }
    return 0;
}
/* Log the file string of an assert without risking a bad dereference. */
static void log_cstr_lib(const char *tag, unsigned long p)
{
    unsigned long lb = libobj_base();
    char buf[112];
    int n = 0, i = 0;
    const char *s = (const char *)p;

    while (tag[i] && n < 12)
        buf[n++] = tag[i++];
    if (!lb || p < lb || p >= lb + 0xe00000UL) {
        const char *t = "<not-libobj>";
        for (i = 0; t[i] && n < 100; i++)
            buf[n++] = t[i];
    } else {
        for (i = 0; i < 72; i++) {
            char c = s[i];
            if (!c)
                break;
            buf[n++] = (c < 32 || c > 126) ? '?' : c;
        }
    }
    buf[n++] = '\n';
    out(buf, n);
}

static unsigned long th_assert(unsigned long a, unsigned long b,
                               unsigned long c, unsigned long d)
{
    static int n;
    unsigned long sp, lb;
    int i;

    if (n < 64) {
        n++;
        log_str("ASSERT ");
        log_cstr_lib("file=", a);
        log_hex("  line=", b);
        log_hex("  err=", c);
        log_hex("  arg3=", d);
        __asm__ volatile ("mov %0, sp" : "=r"(sp));
        lb = libobj_base();
        if (lb) {
            for (i = 1; i < 26; i++) {
                unsigned long v = ((unsigned long *)(sp & ~3UL))[i];
                if (v >= lb && v < lb + 0xe00000UL)
                    log_hex("  stk=", v - lb);
            }
        }
    }
    return 0;
}
/* libObj 0x52D59C: TextRM font-name lookup.  Walks a linked list of
 * {next, name[0x100], ...} and returns the node whose name matches.  The
 * real prologue loads a PC-relative literal, so re-implement the lookup
 * instead of calling through a trampoline; log the requested name so a
 * missing font shows up in the serial log. */
static int shim_str_eq100(const char *a, const char *b)
{
    int i;

    for (i = 0; i < 0x100; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void log_name40(const char *tag, unsigned long p)
{
    char buf[64];
    int n = 0, i;

    while (*tag && n < 10)
        buf[n++] = *tag++;
    for (i = 0; i < 40; i++) {
        char c = ((const char *)p)[i];
        if (!c)
            break;
        if (c < 32 || c > 126) {
            c = '?';
        }
        buf[n++] = c;
    }
    buf[n++] = '\n';
    out(buf, n);
}

static unsigned long th_fontlookup(unsigned long name, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    static int n;
    unsigned long lb = libobj_base();
    unsigned long node = 0;

    if (lb) {
        unsigned long lit = *(volatile unsigned long *)(lb + 0x52d5c4UL);
        unsigned long head = *(volatile unsigned long *)(lb + 0x52d5a8UL + lit);

        node = head;
        if (!n) {
            int i = 0;
            unsigned long p = head;

            log_hex("FONTLIST head=", head);
            while (p && i < 12) {
                log_name40("  reg=", p + 8);
                p = *(volatile unsigned long *)p;
                i++;
            }
        }
        while (node) {
            if (shim_str_eq100((const char *)(node + 8),
                               (const char *)name))
                break;
            node = *(volatile unsigned long *)node;
        }
    }
    if (n < 40) {
        n++;
        log_str("FONTQ ");
        log_name40("name=", name);
        log_hex("  found=", node);
    }
    return node;
}

static unsigned long th_dmp_noop(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;

    if (n < 24) {
        n++;
        log_hex("DMP noop=", a);
    }
    return 0;
}
struct dmp_map {
    unsigned char *pixels;
    void *base;
    unsigned long length;
};

static int dmp_map(unsigned long addr, unsigned long span, struct dmp_map *m)
{
    unsigned long base, off, len;
    long r;

    if (maps_contains(addr)) {
        m->base = 0;
        m->length = 0;
        m->pixels = (unsigned char *)addr;
        return 0;
    }
    base = addr & ~0xfffUL;
    off = addr - base;
    len = (off + span + 0xfffUL) & ~0xfffUL;
    if (ldec_mem_fd < 0)
        ldec_mem_fd = (int)sys3(SYS_open, (long)"/dev/mem", 2, 0);
    r = sys6(SYS_mmap2, 0, len, 3, 1, ldec_mem_fd, base >> 12);
    if (r < 0 && r > -4096)
        return -1;
    m->base = (void *)r;
    m->length = len;
    m->pixels = (unsigned char *)(r + off);
    return 0;
}

static void dmp_unmap(struct dmp_map *m)
{
    if (m->base)
        sys3(SYS_munmap, (long)m->base, m->length, 0);
}

static unsigned int gem_u32(unsigned long p, unsigned off)
{
    return *(volatile unsigned int *)(p + off);
}

/* CPU rendering of the A7R3 Gemini draw calls (same worker geometry as the
 * A7M3 port).  kind: 0 solid, 1 textured rect, 2 quad, 3 line. */
static unsigned long gem_draw(unsigned kind, unsigned long r, unsigned long s,
                              unsigned long m, unsigned long rgba,
                              unsigned long first, unsigned long second)
{
    static int logs;
    static const unsigned fields[] = {0, 1, 4, 5, 12, 13};
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    unsigned char color[4];
    struct ui_gemini_rect sr = {0}, dr = {0};
    float points[8] = {0};
    struct ui_gemini_state state = {0};
    struct ui_gemini_surface dst, src;
    struct dmp_map map;
    unsigned long surface, ma, phys;
    unsigned i, mode = UI_GEMINI_TEXTURE;

    (void)r;
    if (!s || s > 0x40000000UL - 0x180UL)
        return 0;
    for (i = 0; i < 6; i++)
        memcpy(matrix + fields[i], (void *)(m + fields[i] * 4), 4);
    memcpy(color, (void *)rgba, 4);
    if (!kind)
        memcpy(&dr, (void *)first, 8);
    else {
        memcpy(&sr, (void *)first, 8);
        if (kind == 2)
            memcpy(points, (void *)second, sizeof points);
        else
            memcpy(&dr, (void *)second, 8);
    }
    memcpy(&state.viewport, (void *)(s + 0xa8), 8);
    memcpy(&state.scissor, (void *)(s + 0x60), 8);
    state.blend = gem_u32(s, 0x30);
    state.blend_enabled = gem_u32(s, 0x2c);
    state.scissor_enabled = gem_u32(s, 0x34);
    state.rgb_write = gem_u32(s, 0x58);
    state.alpha_write = gem_u32(s, 0x5c);

    surface = gem_u32(s, 0x16c);
    ma = surface ? gem_u32(surface, 0x78) : 0;
    if (!ma) {
        if (logs < 4) { logs++; log_str("GEM no ma\n"); }
        return 0;
    }
    phys = gem_u32(ma, 8);
    if (dmp_map(phys, 640 * 480 * 2, &map)) {
        if (logs < 4) { logs++; log_hex("GEM phys=", phys); }
        return 0;
    }
    dst.pixels = map.pixels;
    dst.capacity = 640 * 480 * 2;
    dst.width = 640;
    dst.height = 480;
    dst.stride = 1280;
    dst.format = 0;
    dst.layout = UI_GEMINI_MORTON_BOTTOM_UP;

    src.pixels = 0;
    src.capacity = 0;
    src.width = src.height = src.stride = 0;
    src.format = 0;
    src.layout = 0;
    if (kind) {
        unsigned long tex = gem_u32(s, 0xb8);
        unsigned long bmp = tex ? gem_u32(tex, 0x48) : 0;
        unsigned long meta = bmp ? gem_u32(bmp, 4) : 0;
        if (bmp && meta) {
            unsigned format = gem_u32(meta, 0x1c);
            unsigned sorting = gem_u32(meta, 0x2c);
            unsigned aw = ui_u16((void *)(meta + 0x18));
            unsigned ah = ui_u16((void *)(meta + 0x1a));
            unsigned bpp = 0;
            src.width = ui_u16((void *)(meta + 8));
            src.height = ui_u16((void *)(meta + 10));
            if (format == 1) {
                src.format = 0;
                bpp = 2;
                mode = UI_GEMINI_MULTIPLY;
            } else if (format == 4) {
                src.format = 3;
                bpp = 1;
                mode = UI_GEMINI_ALPHA_MASK;
            }
            if (bpp && aw && ah) {
                src.stride = aw * bpp;
                src.capacity = src.stride * ah;
                src.layout = sorting ? UI_GEMINI_MORTON_TOP_DOWN
                                     : UI_GEMINI_LINEAR;
                src.pixels = (void *)(uintptr_t)gem_u32(bmp, 8);
            }
        }
        if (!src.pixels) {
            dmp_unmap(&map);
            return 0;
        }
    }
    {
        int res;
        static int pix_n;
        unsigned before[4] = {0, 0, 0, 0}, after[4] = {0, 0, 0, 0};
        size_t dsp = 0;
        int probe = (pix_n < 400 && ui_gemini_span(&dst, &dsp) == 0 &&
                     320 < dst.width && 150 < dst.height);
        if (probe)
            ui_pixel_get(dst.pixels + ui_gemini_offset(&dst, 320, 150),
                         dst.format, before);
        struct shim_ts ts0, ts1;
        shim_now(&ts0);
        if (kind == 2)
            res = ui_gemini_quad(&dst, &src, &state, matrix, color, &sr,
                                 points, mode);
        else
            res = ui_gemini_rect(&dst, kind ? &src : 0, &state, matrix,
                                 color, kind ? &sr : 0, &dr, mode);
        shim_now(&ts1);
        {
            static unsigned long long gem_ns;
            static unsigned gem_calls;
            unsigned long long d = shim_ts_ns(&ts1) - shim_ts_ns(&ts0);
            gem_ns += d;
            gem_calls++;
            if ((gem_calls % 32) == 0) {
                char tb[96];
                char *tq = tb;
                tq = put_s(tq, "GEMTIME calls=");
                tq = put_dec(tq, gem_calls);
                tq = put_s(tq, " total_ms=");
                tq = put_dec(tq, (unsigned long)(gem_ns / 1000000ULL));
                tq = put_s(tq, " avg_us=");
                tq = put_dec(tq, (unsigned long)(gem_ns / 1000ULL / gem_calls));
                *tq++ = '\n';
                out(tb, (int)(tq - tb));
            }
        }
        if (probe) {
            unsigned b4 = (before[0] << 24) | (before[1] << 16) |
                          (before[2] << 8) | before[3];
            ui_pixel_get(dst.pixels + ui_gemini_offset(&dst, 320, 150),
                         dst.format, after);
            unsigned a4 = (after[0] << 24) | (after[1] << 16) |
                          (after[2] << 8) | after[3];
            if (a4 != b4) {
                char bb[192];
                char *q = bb;
                pix_n++;
                q = put_s(q, "GEMPIX kind=");
                q = put_dec(q, kind);
                q = put_s(q, " res=");
                q = put_dec(q, (unsigned long)(unsigned)(int)res);
                q = put_s(q, " dr=");
                q = put_dec(q, (unsigned long)(unsigned short)dr.w);
                q = put_s(q, "x");
                q = put_dec(q, (unsigned long)(unsigned short)dr.h);
                q = put_s(q, " col=");
                q = put_x(q, *(const unsigned long *)color);
                q = put_s(q, " ");
                q = put_x(q, b4);
                q = put_s(q, "->");
                q = put_x(q, a4);
                *q++ = '\n';
                out(bb, (int)(q - bb));
            }
        }
        if (logs < 64) {
            logs++;
            log_hex("GEM kind=", kind);
            log_hex("GEM res=", (unsigned long)res);
            log_hex("GEM phys=", phys);
            log_hex("GEM dr=", (unsigned long)(unsigned short)dr.x |
                               ((unsigned long)(unsigned short)dr.y << 16));
            log_hex("GEM dr2=", (unsigned long)(unsigned short)dr.w |
                                ((unsigned long)(unsigned short)dr.h << 16));
            if (kind) {
                log_hex("GEM sr=", (unsigned long)(unsigned short)sr.x |
                                   ((unsigned long)(unsigned short)sr.y << 16));
                log_hex("GEM sr2=", (unsigned long)(unsigned short)sr.w |
                                    ((unsigned long)(unsigned short)sr.h << 16));
            }
            log_hex("GEM color=", *(unsigned long *)color);
        }
    }
    shim_barrier();
    {
        static int big_n;
        if (big_n < 160 && dr.w >= 160 && dr.h >= 60) {
            char bb[512];
            char *q = bb;
            big_n++;
            q = put_s(q, "GEMBIG kind=");
            q = put_dec(q, kind);
            q = put_s(q, " x=");
            q = put_dec(q, (unsigned long)(unsigned)(int)(int16_t)dr.x);
            q = put_s(q, " y=");
            q = put_dec(q, (unsigned long)(unsigned)(int)(int16_t)dr.y);
            q = put_s(q, " w=");
            q = put_dec(q, (unsigned long)(unsigned short)dr.w);
            q = put_s(q, " h=");
            q = put_dec(q, (unsigned long)(unsigned short)dr.h);
            q = put_s(q, " col=");
            q = put_x(q, *(const unsigned long *)color);
            q = put_s(q, " blend=");
            q = put_dec(q, state.blend_enabled);
            q = put_s(q, " bl=");
            q = put_x(q, state.blend);
            q = put_s(q, " rw=");
            q = put_dec(q, state.rgb_write);
            q = put_s(q, " aw=");
            q = put_dec(q, state.alpha_write);
            q = put_s(q, " sc=");
            q = put_dec(q, state.scissor_enabled);
            if (state.scissor_enabled) {
                q = put_s(q, "(");
                q = put_dec(q, (unsigned long)(unsigned)(int)state.scissor.x);
                q = put_s(q, ",");
                q = put_dec(q, (unsigned long)(unsigned)(int)state.scissor.y);
                q = put_s(q, ",");
                q = put_dec(q, (unsigned long)(unsigned)(int)state.scissor.w);
                q = put_s(q, ",");
                q = put_dec(q, (unsigned long)(unsigned)(int)state.scissor.h);
                q = put_s(q, ")");
            }
            if (kind && src.pixels) {
                q = put_s(q, " mode=");
                q = put_dec(q, mode);
                q = put_s(q, " sw=");
                q = put_dec(q, src.width);
                q = put_s(q, " sh=");
                q = put_dec(q, src.height);
                q = put_s(q, " sf=");
                q = put_dec(q, src.format);
                q = put_s(q, " sl=");
                q = put_dec(q, src.layout);
                {
                    size_t sspan = 0;
                    if (ui_gemini_span(&src, &sspan) == 0 && src.width &&
                        src.height) {
                        unsigned cc[4];
                        unsigned p0 = src.width / 2;
                        unsigned p1 = src.height / 2;
                        ui_pixel_get(src.pixels + ui_gemini_offset(&src, 0, 0),
                                     src.format, cc);
                        q = put_s(q, " t00=");
                        q = put_x(q, (cc[0] << 24) | (cc[1] << 16) |
                                     (cc[2] << 8) | cc[3]);
                        ui_pixel_get(src.pixels +
                                     ui_gemini_offset(&src, p0, p1),
                                     src.format, cc);
                        q = put_s(q, " tc=");
                        q = put_x(q, (cc[0] << 24) | (cc[1] << 16) |
                                     (cc[2] << 8) | cc[3]);
                        {
                            unsigned n = 0, wo = 0, bo = 0, tr = 0, oth = 0;
                            unsigned sx, sy;
                            for (sy = 0; sy < src.height; sy += 8) {
                                for (sx = 0; sx < src.width; sx += 8) {
                                    unsigned c2[4];
                                    ui_pixel_get(src.pixels +
                                                 ui_gemini_offset(&src, sx, sy),
                                                 src.format, c2);
                                    n++;
                                    if (c2[3] == 0) tr++;
                                    else if (c2[0] == 255 && c2[1] == 255 &&
                                             c2[2] == 255) wo++;
                                    else if (c2[0] == 0 && c2[1] == 0 &&
                                             c2[2] == 0) bo++;
                                    else oth++;
                                }
                            }
                            q = put_s(q, " hist=");
                            q = put_dec(q, wo);
                            q = put_s(q, "/");
                            q = put_dec(q, bo);
                            q = put_s(q, "/");
                            q = put_dec(q, tr);
                            q = put_s(q, "/");
                            q = put_dec(q, oth);
                            q = put_s(q, "/");
                            q = put_dec(q, n);
                        }
                    }
                }
                q = put_s(q, " sa=");
                q = put_x(q, (unsigned long)(uintptr_t)src.pixels);
                q = put_s(q, " da=");
                q = put_x(q, (unsigned long)(uintptr_t)dst.pixels);
            }
            {
                size_t dspan = 0;
                if (ui_gemini_span(&dst, &dspan) == 0) {
                    unsigned cx2 = 320, cy2 = 240;
                    if (state.scissor_enabled) {
                        cx2 = (unsigned)(state.scissor.x + state.scissor.w / 2);
                        cy2 = (unsigned)(dst.height - state.scissor.y -
                                         state.scissor.h / 2);
                    }
                    if (cx2 < dst.width && cy2 < dst.height) {
                        unsigned cc2[4];
                        ui_pixel_get(dst.pixels +
                                     ui_gemini_offset(&dst, cx2, cy2),
                                     dst.format, cc2);
                        q = put_s(q, " dcen=");
                        q = put_x(q, (cc2[0] << 24) | (cc2[1] << 16) |
                                     (cc2[2] << 8) | cc2[3]);
                    }
                }
            }
            *q++ = '\n';
            out(bb, (int)(q - bb));
        }
    }
    {
        static int gem_calls;
        ++gem_calls;
        if ((gem_calls % 32) == 0 && gem_calls <= 1024 &&
            file_exists("/usr/tool/gem_dump")) {
            char path[40];
            char *q = path;
            q = put_s(q, "/log/gemframe_");
            q = put_dec(q, (unsigned long)(gem_calls / 32));
            q = put_s(q, ".raw");
            *q = 0;
            gpu_dump_blob(path, (unsigned long)map.pixels, 640 * 480 * 2);
        }
    }
    dmp_unmap(&map);
    return 1;
}

static unsigned long th_gem_solid(unsigned long r, unsigned long s,
                                  unsigned long m, unsigned long c,
                                  unsigned long dst)
{
    return gem_draw(0, r, s, m, c, dst, 0);
}
static unsigned long th_gem_rect(unsigned long r, unsigned long s,
                                 unsigned long m, unsigned long c,
                                 unsigned long src, unsigned long dst)
{
    return gem_draw(1, r, s, m, c, src, dst);
}
static unsigned long th_gem_quad(unsigned long r, unsigned long s,
                                 unsigned long m, unsigned long c,
                                 unsigned long src, unsigned long points)
{
    return gem_draw(2, r, s, m, c, src, points);
}
static unsigned long th_gem_line(unsigned long r, unsigned long s,
                                 unsigned long m, unsigned long c,
                                 unsigned long first, unsigned long second)
{
    return gem_draw(3, r, s, m, c, first, second);
}

static unsigned long th_dmp_fill(unsigned long request, unsigned long mode,
                                 unsigned long params, unsigned long unused)
{
    const unsigned char *p = (const unsigned char *)params;
    size_t span;
    struct dmp_map m;
    int st = -5;

    (void)unused;
    if (!p || mode > 2)
        return (unsigned long)-5;
    if (!ui_surface(p, &span) && !dmp_map(ui_u32(p), span, &m)) {
        st = dmp_raster_fill(m.pixels, span, p);
        dmp_unmap(&m);
    }
    log_hex("DMP fill=", (unsigned long)st);
    if (st)
        return (unsigned long)st;
    if (ui_u32(p + 40))
        ((void (*)(unsigned long, unsigned long))(uintptr_t)ui_u32(p + 40))
            (request, ui_u32(p + 44));
    return 0;
}

static unsigned long th_dmp_blit(unsigned long request, unsigned long mode,
                                 unsigned long params, unsigned long unused)
{
    const unsigned char *p = (const unsigned char *)params;
    unsigned char desc[24];
    size_t ss, ds;
    struct dmp_map src, dst;
    int st = -5;

    (void)unused;
    if (!p || mode > 2)
        return (unsigned long)-5;
    memcpy(desc, p + 24, 8);
    memcpy(desc + 16, p + 48, 8);
    if (!ui_surface(p, &ss) && !ui_surface(desc, &ds)) {
        unsigned long s = ui_u32(p), d = ui_u32(p + 24);
        if ((unsigned long long)s + ss <= d ||
            (unsigned long long)d + ds <= s) {
            if (!dmp_map(s, ss, &src)) {
                if (!dmp_map(d, ds, &dst)) {
                    st = ui_blit(dst.pixels, ds, src.pixels, ss, p);
                    dmp_unmap(&dst);
                }
                dmp_unmap(&src);
            }
        }
    }
    log_hex("DMP blit=", (unsigned long)st);
    if (st)
        return (unsigned long)st;
    if (ui_u32(p + 148))
        ((void (*)(unsigned long, unsigned long))(uintptr_t)ui_u32(p + 148))
            (request, ui_u32(p + 152));
    return 0;
}

static const unsigned char pro_model_init[8] = {0xf0, 0xb5, 0x83, 0xb0,
                                                0x04, 0x46, 0x00, 0xaf};
static const unsigned char pro_dmp_init[8] = {0x03, 0x48, 0x80, 0xb4,
                                              0x78, 0x44, 0x00, 0xaf};
static const unsigned char pro_dmp_fill[8] = {0x02, 0x29, 0x0b, 0x46,
                                              0xf0, 0xb5, 0x85, 0xb0};
static const unsigned char pro_errhook[8] = {0x03, 0x4b, 0x80, 0xb5,
                                             0x7b, 0x44, 0x1b, 0x68};
/* libObj 0x61DC78: the "assert/log" reporter called by the TextRM assert
 * handler (0x623BB4) with r0=file string, r1=line, r2=err.  Pure logging,
 * so replacing it is safe; we do not invoke the original. */
static const unsigned char pro_assert[8] = {0x90, 0xb5, 0x00, 0xaf,
                                            0x40, 0xf0, 0x00, 0x50};
/* libObj 0x52D59C: TextRM font-name lookup (see th_fontlookup). */
static const unsigned char pro_fontlookup[8] = {0xb0, 0xb5, 0x05, 0x46,
                                                0x08, 0x4c, 0x00, 0xaf};
static const unsigned char pro_gem_solid[8] = {0x2d, 0xe9, 0xf0, 0x47,
                                               0x2d, 0xed, 0x04, 0x8b};
static const unsigned char pro_gem_rect[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                              0x2d, 0xed, 0x06, 0x8b};
static const unsigned char pro_gem_line[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                              0x83, 0xb0, 0x02, 0xaf};
static const unsigned char pro_engine[8] = {0xf0, 0xb5, 0x83, 0xb0,
                                            0x44, 0x1e, 0x00, 0xaf};
static const unsigned char pro_rend[8] = {0x02, 0x48, 0x80, 0xb5,
                                          0x78, 0x44, 0x00, 0x68};
static const unsigned char pro_gpe_fill[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                              0x99, 0xb0, 0x14, 0x46};
static const unsigned char pro_gpe_blit[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                              0xbb, 0xb0, 0x14, 0x46};
static const unsigned char pro_input_create[8] = {0xf0, 0xb5, 0x89, 0xb0,
                                                  0x06, 0x46, 0x18, 0x20};
static const unsigned char pro_mcs_app[8] = {0xb0, 0xb5, 0x90, 0x20,
                                             0x00, 0xaf, 0x0d, 0x46};
static const unsigned char pro_mroot[8] = {0xb0, 0xb5, 0xe8, 0x20,
                                           0x00, 0xaf, 0x0d, 0x46};
static const unsigned char pro_mboot[8] = {0xb0, 0xb5, 0x84, 0x20,
                                           0x00, 0xaf, 0x0d, 0x46};
static const unsigned char pro_mdisplay[8] = {0xb0, 0xb5, 0x4f, 0xf4,
                                              0x0c, 0x70, 0x00, 0xaf};
static const unsigned char pro_mcamera[8] = {0xb0, 0xb5, 0x45, 0xf6,
                                             0x98, 0x70, 0x00, 0xaf};

static const unsigned char pro_evpush[8] = {0x2d, 0xe9, 0xf0, 0x41,
                                             0x84, 0xb0, 0x04, 0x46};
static const unsigned char pro_utilpush[8] = {0x80, 0xb5, 0x01, 0x22,
                                              0x00, 0xaf, 0x40, 0x68};
static const unsigned char pro_createreq[8] = {0x2d, 0xe9, 0xf0, 0x41,
                                               0x10, 0x20, 0x00, 0xaf};
static const unsigned char pro_mpush[8] = {0x80, 0xb5, 0x01, 0x22,
                                           0x00, 0xaf, 0x00, 0x68};

static draw_fn real_evpush, real_utilpush, real_createreq, real_mpush;
static draw_fn real_mgrctor;
static unsigned long g_manager;
static unsigned long g_mgr_sp;
static int signal_ready_sem(void);

static unsigned short *ui_fb;

static void ui_fill(int x0, int y0, int x1, int y1, unsigned short c)
{
    int x, y;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    for (y = y0; y < y1; y++) {
        unsigned short *r = ui_fb + y * 640;
        for (x = x0; x < x1; x++)
            r[x] = c;
    }
}

static void ui_border(int x0, int y0, int x1, int y1, unsigned short c, int t)
{
    ui_fill(x0, y0, x1, y0 + t, c);
    ui_fill(x0, y1 - t, x1, y1, c);
    ui_fill(x0, y0, x0 + t, y1, c);
    ui_fill(x1 - t, y0, x1, y1, c);
}

static void ui_text(int x, int y, const char *s, int scale, unsigned short c)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        const unsigned char *g;
        int row, col;

        if (ch < 32 || ch > 127)
            ch = '?';
        g = font8x8[ch - 32];
        for (row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int px = x + col * scale;
                    int py = y + row * scale;
                    ui_fill(px, py, px + scale, py + scale, c);
                }
            }
        }
        x += 8 * scale;
    }
}

static void ui_circle(int cx, int cy, int r, unsigned short c)
{
    int y;

    for (y = -r; y <= r; y++) {
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= r * r)
            w++;
        ui_fill(cx - w, cy + y, cx + w + 1, cy + y + 1, c);
    }
}

static void ui_tri(int x, int y, int h, unsigned short c)
{
    int i;

    for (i = 0; i < h; i++)
        ui_fill(x, y + i, x + i, y + i + 1, c);
}

static void ui_icon(int kind, int cx, int cy, unsigned short c)
{
    switch (kind) {
    case 0: /* camera */
        ui_fill(cx - 20, cy - 8, cx + 20, cy + 12, c);
        ui_fill(cx - 8, cy - 13, cx - 2, cy - 8, c);
        ui_circle(cx, cy + 2, 9, 0x112f);
        ui_circle(cx, cy + 2, 6, c);
        break;
    case 1: /* play */
        ui_tri(cx - 8, cy - 12, 24, c);
        break;
    case 2: /* gear */
        ui_circle(cx, cy, 13, c);
        ui_circle(cx, cy, 6, 0x112f);
        ui_fill(cx - 2, cy - 20, cx + 2, cy - 12, c);
        ui_fill(cx - 2, cy + 12, cx + 2, cy + 20, c);
        ui_fill(cx - 20, cy - 2, cx - 12, cy + 2, c);
        ui_fill(cx + 12, cy - 2, cx + 20, cy + 2, c);
        break;
    case 3: /* sun */
        ui_circle(cx, cy, 9, c);
        ui_fill(cx - 1, cy - 22, cx + 1, cy - 14, c);
        ui_fill(cx - 1, cy + 14, cx + 1, cy + 22, c);
        ui_fill(cx - 22, cy - 1, cx - 14, cy + 1, c);
        ui_fill(cx + 14, cy - 1, cx + 22, cy + 1, c);
        break;
    case 4: /* target */
        ui_circle(cx, cy, 14, c);
        ui_circle(cx, cy, 11, 0x112f);
        ui_circle(cx, cy, 4, c);
        break;
    default: /* wifi */
        ui_circle(cx, cy + 12, 3, c);
        ui_fill(cx - 12, cy - 2, cx + 12, cy + 2, c);
        ui_fill(cx - 20, cy - 12, cx + 20, cy - 8, c);
        ui_fill(cx - 28, cy - 22, cx + 28, cy - 18, c);
        break;
    }
}

static void render_ui(void)
{
    static const unsigned short bg = 0x112f;
    static const unsigned short bar = 0x135f;
    static const unsigned short acc = 0xf80f;
    static const unsigned short wht = 0xffff;
    static const unsigned short gry = 0xbbbf;
    static const unsigned short tile = 0x224f;
    static const unsigned short tsel = 0x2a5f;
    static const unsigned short brd = 0x445f;
    static const char *names[6] = {"SHOOTING", "PLAYBACK", "SETUP",
                                   "EXPOSURE", "FOCUS", "NETWORK"};
    long fd, r;
    int i;

    fd = sys3(SYS_open, (long)"/dev/mem", 2 /*O_RDWR*/, 0);
    if (fd < 0) {
        log_hex("UI_MEM fd=", (unsigned long)fd);
        return;
    }
    r = sys6(SYS_mmap2, 0, 640 * 480 * 2, 3 /*RW*/, 1 /*SHARED*/, fd,
             0x23a80000UL >> 12);
    log_hex("UI_MEM mmap=", (unsigned long)r);
    if (r < 0 && r > -4096) {
        sys3(SYS_close, fd, 0, 0);
        return;
    }
    ui_fb = (unsigned short *)r;

    ui_fill(0, 0, 640, 480, bg);
    ui_fill(0, 0, 640, 58, bar);
    ui_fill(0, 58, 640, 60, acc);

    ui_text(18, 18, "A7R III", 2, wht);
    ui_text(18, 76, "MENU", 2, acc);
    ui_text(430, 24, "RAW", 1, gry);

    /* battery */
    ui_border(560, 20, 606, 40, wht, 2);
    ui_fill(606, 26, 610, 34, wht);
    ui_fill(564, 24, 596, 36, 0x0f6f);

    for (i = 0; i < 6; i++) {
        int col = i % 3, row = i / 3;
        int x = 20 + col * 205, y = 122 + row * 158;
        int w = 190, h = 142;
        int len = 0;
        const char *s = names[i];
        int tx;

        ui_fill(x, y, x + w, y + h, i == 0 ? tsel : tile);
        ui_border(x, y, x + w, y + h, i == 0 ? acc : brd, i == 0 ? 3 : 1);
        ui_icon(i, x + w / 2, y + 52, i == 0 ? acc : wht);
        while (s[len])
            len++;
        tx = x + (w - len * 8) / 2;
        ui_text(tx, y + 108, s, 1, i == 0 ? wht : gry);
    }

    ui_fill(0, 440, 640, 480, bar);
    ui_text(18, 454, "ENTER  SELECT", 1, wht);
    ui_text(262, 454, "MENU  BACK", 1, gry);
    ui_text(500, 454, "1/6", 1, acc);

    shim_barrier();
    log_str("UI_DRAWN\n");
    sys3(SYS_close, fd, 0, 0);
}

static void install_render_hooks(unsigned long base)
{
    static int done;

    if (!base || done)
        return;
    done = 1;
    hook_one(base, 0x780188, pro_gem_solid, (void *)th_gem_solid,
             (void **)&real_gem_solid);
    hook_one(base, 0x77fb88, pro_gem_rect, (void *)th_gem_quad,
             (void **)&real_gem_quad);
    hook_one(base, 0x77fe70, pro_gem_rect, (void *)th_gem_rect,
             (void **)&real_gem_rect);
    hook_one(base, 0x78030c, pro_gem_line, (void *)th_gem_line,
             (void **)&real_gem_line);
    /* Do NOT hook 0x700374: it is not a headless display; it builds a message
     * for the VDF queue (0x80003B).  Its first instruction is PC-relative, so
     * a trampoline would also corrupt the queue id.  Let the original run. */
    hook_one(base, 0x718fc0, pro_engine, (void *)th_engine,
             (void **)&real_engine);
    /* Do NOT hook 0x61ADCC: its first instruction is a PC-relative literal
     * load, which hook_one's trampoline would relocate and break.  The app's
     * ModelDisplay instance already exists there. */
    hook_one(base, 0x635f0c, pro_gpe_fill, (void *)th_gpe_fill,
             (void **)&real_gpe_fill);
    hook_one(base, 0x636046, pro_gpe_blit, (void *)th_gpe_blit,
             (void **)&real_gpe_blit);
    log_str("RENDER hooks\n");
}

/* ---- display / panel (VDF-DMM) natural-call tracing -------------------
 * The real boot log shows the order
 *   Panel ON -> /setting -> RM res init -> Root Draw -> ObjDisplay ->
 *   ObjRenderer -> modelDisplay INIT -> SET_PANEL_OUT.
 * These hooks report whether the RM actually creates the display objects in
 * QEMU and whether the VDF channel calls succeed, so the missing producer can
 * be filled in at the right layer. */
typedef unsigned long (*ul4_fn)(unsigned long, unsigned long, unsigned long,
                                unsigned long);
static ul4_fn real_objdisp;
static ul4_fn real_objrend;
static ul4_fn real_vdf_getpanel;
static ul4_fn real_vdf_setpanel;
static ul4_fn real_vdf_getosd;
static ul4_fn real_vdf_setosd;

static void log_words_at(const char *tag, unsigned long p, int n)
{
    int i;
    if (p <= 0x10000 || p >= 0x40000000UL || !maps_contains(p))
        return;
    for (i = 0; i < n; i++)
        log_hex(tag, *(volatile unsigned long *)(p + i * 4));
}

static unsigned long th_objdisp(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    unsigned long r;
    log_str("OBJDISP enter\n");
    log_hex("  a=", a);
    r = real_objdisp ? real_objdisp(a, b, c, d) : 0;
    log_hex("  ret=", r);
    log_words_at("  f=", r, 6);
    return r;
}

static unsigned long th_objrend(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    unsigned long r;
    log_str("OBJREND enter\n");
    log_hex("  a=", a);
    r = real_objrend ? real_objrend(a, b, c, d) : 0;
    log_hex("  ret=", r);
    log_words_at("  f=", r, 6);
    return r;
}

static unsigned long th_vdf_getpanel(unsigned long a, unsigned long b,
                                     unsigned long c, unsigned long d)
{
    unsigned long r = real_vdf_getpanel ? real_vdf_getpanel(a, b, c, d) : 0;
    log_hex("VDFgetPanel ret=", r);
    log_words_at("  comp=", b, 8);
    return r;
}

static unsigned long th_vdf_setpanel(unsigned long a, unsigned long b,
                                     unsigned long c, unsigned long d)
{
    unsigned long r = real_vdf_setpanel ? real_vdf_setpanel(a, b, c, d) : 0;
    log_hex("VDFsetPanel ret=", r);
    log_words_at("  prm=", b, 4);
    return r;
}

static unsigned long th_vdf_getosd(unsigned long a, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    unsigned long r = real_vdf_getosd ? real_vdf_getosd(a, b, c, d) : 0;
    log_hex("VDFgetOSD ret=", r);
    log_words_at("  comp=", b, 8);
    return r;
}

static unsigned long th_vdf_setosd(unsigned long a, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    unsigned long r = real_vdf_setosd ? real_vdf_setosd(a, b, c, d) : 0;
    log_hex("VDFsetOSD ret=", r);
    log_words_at("  prm=", b, 4);
    return r;
}

static const unsigned char pro_objdisp[8] = {0xb0, 0xb5, 0x4f, 0xf4,
                                             0x32, 0x70, 0x00, 0xaf};
static const unsigned char pro_objrend[8] = {0xb0, 0xb5, 0x4f, 0xf4,
                                             0x28, 0x70, 0x00, 0xaf};
static const unsigned char pro_vdf_get[8] = {0x2d, 0xe9, 0xf0, 0x41,
                                             0x8c, 0xb0, 0x04, 0xaf};
static const unsigned char pro_vdf_set[8] = {0xb0, 0xb5, 0x8c, 0xb0,
                                             0x04, 0xaf, 0x05, 0x46};
static const unsigned char pro_vdf_getosd[8] = {0xf0, 0xb5, 0x8b, 0xb0,
                                                0x04, 0xaf, 0x04, 0x46};

static unsigned long th_rend_method(unsigned long a, unsigned long b,
                                    unsigned long c, unsigned long d)
{
    static int n;

    if (n < 32) {
        n++;
        log_hex("RENDM a=", a);
        log_hex("RENDM b=", b);
        log_hex("RENDM c=", c);
        log_hex("RENDM d=", d);
    }
    return 0;
}

static void hook_bizfw(void)
{
    static int done;
    unsigned long bb;

    if (done)
        return;
    bb = module_base("libBizFw.so");
    if (!bb)
        return;
    done = 1;
    log_hex("BIZFW base=", bb);
    hook_one(bb, 0x372b4, pro_vdf_get, (void *)th_vdf_getpanel,
             (void **)&real_vdf_getpanel);
    hook_one(bb, 0x371cc, pro_vdf_set, (void *)th_vdf_setpanel,
             (void **)&real_vdf_setpanel);
    hook_one(bb, 0x397a0, pro_vdf_getosd, (void *)th_vdf_getosd,
             (void **)&real_vdf_getosd);
    hook_one(bb, 0x396b0, pro_vdf_set, (void *)th_vdf_setosd,
             (void **)&real_vdf_setosd);
}

static ul4_fn real_disp;

static unsigned long th_disp(unsigned long a, unsigned long b,
                             unsigned long c, unsigned long d)
{
    static int n;

    if (n < 32) {
        n++;
        log_str("DISPDESC\n");
        log_hex("  p=", a);
        if (a > 0x10000 && a < 0x40000000UL) {
            volatile unsigned long *w = (volatile unsigned long *)a;
            log_hex("  src=", w[0]);
            log_hex("  dst=", w[1]);
            log_hex("  flg=", w[0x14 / 4]);
            log_hex("  w=", w[0x18 / 4]);
            log_hex("  h=", w[0x1c / 4]);
            log_hex("  op=", w[0x28 / 4]);
            log_hex("  r2c=", w[0x2c / 4]);
            log_hex("  r30=", w[0x30 / 4]);
            log_hex("  r34=", w[0x34 / 4]);
        }
    }
    return real_disp ? real_disp(a, b, c, d) : 0;
}
static const unsigned char pro_disp[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                          0x10, 0xb0, 0x05, 0x46};

static unsigned long th_mgrctor(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    unsigned long r;
    unsigned long lb;

    log_hex("MGRCTOR enter=", a);
    __asm__ volatile ("mov %0, sp" : "=r"(g_mgr_sp));
    r = real_mgrctor ? real_mgrctor(a, b, c, d) : 0;
    g_manager = a;
    log_hex("MGRCTOR this=", a);
    install_render_hooks(libobj_base());
    log_hex("MD existing=",
            *(volatile unsigned long *)(libobj_base() + 0x129f52cUL));
    return r;
    {
        typedef unsigned long (*fn2_t)(unsigned long, unsigned long);
        unsigned long lb2 = libobj_base();
        unsigned long out = 0;
        unsigned long obj;
        obj = ((fn2_t)((lb2 + 0x1e0e6dUL) | 1UL))(0, (unsigned long)&out);
        log_hex("OBJRENDER ret=", obj);
        log_hex("OBJRENDER out=", out);
        if (obj && obj < 0x40000000UL)
            *(volatile unsigned long *)(lb2 + 0x12a1532UL) = obj;
    }
    {
        unsigned long lb2 = libobj_base();
        unsigned long g = *(volatile unsigned long *)(lb2 + 0x12a1532UL);
        log_hex("RENDERER=", g);
        if (g && g < 0x40000000UL) {
            unsigned long vt = *(volatile unsigned long *)g;
            unsigned long i;
            log_hex("RENDERER vt=", vt);
            if (vt && vt < 0x40000000UL) {
                for (i = 0; i < 0x60; i += 4)
                    log_hex("RV=", *(volatile unsigned long *)(vt + i));
            }
        }
    }
    return r;
    lb = libobj_base();
    if (lb && a > 0x10000 && a < 0x40000000UL) {
        typedef unsigned long (*fn1)(unsigned long);
        unsigned long ev = *(volatile unsigned long *)(a + 0x10);
        unsigned long p1, p2, is;

        log_hex("MGR f0=", *(volatile unsigned long *)(a + 0));
        log_hex("MGR f4=", *(volatile unsigned long *)(a + 4));
        log_hex("MGR f8=", *(volatile unsigned long *)(a + 8));
        log_hex("MGR fc=", *(volatile unsigned long *)(a + 0xc));
        log_hex("MGR f10=", ev);
        log_hex("MGR f14=", *(volatile unsigned long *)(a + 0x14));

        /* The ctor's skipped call: [manager+0x14]->vtable[0x54]() derives the
         * model and invokes Model::initialize.  Call it now. */
        {
            unsigned long cfg = *(volatile unsigned long *)(a + 0x14);
            unsigned long mret = 1;
            if (cfg) {
                unsigned long vt = *(volatile unsigned long *)cfg;
                unsigned long fn = *(volatile unsigned long *)(vt + 0x54);
                log_hex("MGR v54=", fn);
                if (fn)
                    mret = ((fn1)(fn | 1UL))(cfg);
                log_hex("MGR v54ret=", mret);
                log_hex("MGR model=", g_model);
            }
            if (mret == 0) {
                unsigned long pvm =
                    *(volatile unsigned long *)(lb + 0x129ee1cUL);
                unsigned long i;
                log_hex("VIEW pvm=", pvm);
                if (pvm > 0x10000 && pvm < 0x40000000UL &&
                    maps_contains(pvm)) {
                    unsigned long cnt = *(volatile unsigned long *)pvm;
                    unsigned long slots =
                        *(volatile unsigned long *)(pvm + 0x20);
                    log_hex("VIEW count=", cnt);
                    log_hex("VIEW slots=", slots);
                    if (cnt < 64 && slots > 0x10000 && slots < 0x40000000UL &&
                        maps_contains(slots)) {
                        for (i = 0; i < cnt && i < 8; i++) {
                            unsigned long j;
                            for (j = 0; j < 4; j++)
                                log_hex("VIEW w=",
                                        *(volatile unsigned long *)
                                            (slots + i * 16 + j * 4));
                        }
                    }
                }
                log_hex("VIEW pview=",
                        *(volatile unsigned long *)(lb + 0x12d4c04UL));
                {
                    typedef unsigned long (*fn1_t)(unsigned long);
                    typedef unsigned long (*fn3_t)(unsigned long,
                                                    unsigned long,
                                                    unsigned long);
                    unsigned long cfg2 =
                        *(volatile unsigned long *)(a + 0x14);
                    if (cfg2) {
                        unsigned long vt =
                            *(volatile unsigned long *)cfg2;
                        unsigned long f3c =
                            *(volatile unsigned long *)(vt + 0x3c);
                        unsigned long f2c =
                            *(volatile unsigned long *)(vt + 0x2c);
                        unsigned long cb = 0, r2c = 0;
                        if (f3c)
                            cb = ((fn1_t)(f3c | 1UL))(cfg2);
                        *(volatile unsigned long *)(a + 0x20) = cb;
                        if (f2c)
                            r2c = ((fn1_t)(f2c | 1UL))(cfg2);
                        if (r2c)
                            ((fn3_t)(lb + 0x1004f0UL))(
                                *(volatile unsigned long *)(a + 0x10),
                                r2c, 1);
                        log_hex("MGR cb=", cb);
                        log_hex("MGR r2c=", r2c);
                    }
                }
                {
                    typedef unsigned long (*fn2_t)(unsigned long,
                                                    unsigned long);
                    unsigned long ov;
                    ov = ((fn2_t)((lb + 0x3e0da9UL) | 1UL))(
                        (unsigned long)"view/ROOT", 0);
                    log_hex("OPENVIEW=", ov);
                }
                log_str("EXECLOOP enter\n");
                ((fn1)((lb + 0x82feb4UL) | 1UL))(a);
                log_str("EXECLOOP return\n");
                return r;
            }
        }

        /* InputService::create([model+0x10]) 鈥?Model::initialize's own call. */
        {
            unsigned long m = 0;
            typedef unsigned long (*fn2)(unsigned long, unsigned long);
            if (g_model)
                m = *(volatile unsigned long *)(g_model + 0x10);
            log_hex("MGR m10=", m);
            if (m && m < 0x40000000UL) {
                log_hex("MGR m0=", *(volatile unsigned long *)m);
                log_hex("MGR mc=", *(volatile unsigned long *)(m + 0xc));
                log_hex("MGR m10b=", *(volatile unsigned long *)(m + 0x10));
            }
            /* Construct the InputService object directly (its ctor at
             * 0x3c1178) and register the singleton; 0x3c4020's trailing
             * init needs the display state we cannot provide. */
            {
                void *(*m_alloc)(unsigned) =
                    (void *(*)(unsigned))dlsym(RTLD_DEFAULT, "malloc");
                typedef unsigned long (*fn2)(unsigned long, unsigned long);
                unsigned long mem = 0;
                if (m_alloc) {
                    volatile unsigned long *z;
                    int k;
                    mem = (unsigned long)m_alloc(0x18);
                    if (mem) {
                        z = (volatile unsigned long *)mem;
                        for (k = 0; k < 6; k++)
                            z[k] = 0;
                        ((fn2)((lb + 0x3c1178UL) | 1UL))(mem, m ? m : a);
                    }
                }
                is = mem;
                if (mem) {
                    unsigned long sp2 =
                        *(volatile unsigned long *)(lb + 0x121d894UL);
                    if (sp2 > 0x10000 && sp2 < 0x40000000UL)
                        *(volatile unsigned long *)sp2 = mem;
                }
                log_hex("MGR input_obj=", is);
                log_hex("MGR input_sing=",
                        *(volatile unsigned long *)(lb + 0x129ebf8UL));
            }

            p1 = *(volatile unsigned long *)(lb + 0x121ec6cUL);
            p2 = *(volatile unsigned long *)(lb + 0x12209ecUL);
            if (p1 > 0x10000 && p1 < 0x40000000UL) {
                unsigned long v = m ? *(volatile unsigned long *)(m + 0xc) : 0;
                *(volatile unsigned long *)p1 = v ? v
                    : *(volatile unsigned long *)(a + 0xc);
            }
            if (p2 > 0x10000 && p2 < 0x40000000UL)
                *(volatile unsigned long *)p2 = m ? m : a;
            *(volatile unsigned long *)(lb + 0x12d4c04UL) =
                *(volatile unsigned long *)(a + 0);
        }

        log_hex("MGRCTOR m1=", *(volatile unsigned long *)p1);
        log_hex("MGRCTOR m2=", *(volatile unsigned long *)p2);
        log_hex("MGRCTOR pvm=", *(volatile unsigned long *)(lb + 0x129ee1cUL));
        log_hex("MGRCTOR pview=", *(volatile unsigned long *)(lb + 0x12d4c04UL));
        render_ui();
        /* Keep the created objects alive: the Appframework entry's next
         * step needs the state we cannot provide, so park this thread. */
        log_str("MGRCTOR frozen\n");
        for (;;) {
            long ts[2];
            ts[0] = 1;
            ts[1] = 0;
            sys3(162 /* nanosleep */, (long)ts, 0, 0);
        }
    }
    return r;
}

static const unsigned char pro_mgrctor[10] = {0x2d, 0xe9, 0xf0, 0x41,
                                              0x84, 0xb0, 0x6f, 0xf0,
                                              0x00, 0x43};

static draw_fn real_appfw;

/* The manager constructor waits on OSAL semaphore 0x83037A (id 0x037A).
 * This is the "ready" gate we could not identify a producer for; signal it
 * ourselves so construction can proceed. */
static int signal_ready_sem(void)
{
    static int (*fn)(unsigned);
    if (!fn)
        fn = (int (*)(unsigned))dlsym(RTLD_DEFAULT, "osal_sig_sem");
    return fn ? fn(0x83037a) : -1;
}

static unsigned long th_appfw(unsigned long a, unsigned long b,
                              unsigned long c, unsigned long d)
{
    log_hex("APPFW sigsem=", (unsigned long)signal_ready_sem());
    log_hex("APPFW enter=", a);
    return real_appfw ? real_appfw(a, b, c, d) : 0;
}
static const unsigned char pro_appfw[6] = {0x90, 0xb5, 0x9d, 0xb0, 0x00, 0xaf};

static unsigned long th_evpush(unsigned long a, unsigned long b,
                                  unsigned long c, unsigned long d)
{
    static int n;
    unsigned long lr;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));
    if (trace_on && n < 48) {
        n++;
        log_str("EVPUSH");
        log_hex(" this=", a);
        log_hex(" ev=", b);
        log_hex(" flag=", c);
        log_hex(" lr=", lr);
        if (b > 0x10000 && b < 0x40000000UL) {
            log_hex("  id=", *(volatile unsigned long *)b);
            log_hex("  w4=", *(volatile unsigned long *)(b + 4));
            log_hex("  w8=", *(volatile unsigned long *)(b + 8));
        }
    }
    return real_evpush ? real_evpush(a, b, c, d) : 0;
}

static unsigned long th_utilpush(unsigned long a, unsigned long b,
                                    unsigned long c, unsigned long d)
{
    static int n;

    if (trace_on && n < 24) {
        n++;
        log_str("UTILPUSH");
        log_hex(" this=", a);
        if (a > 0x10000 && a < 0x40000000UL) {
            log_hex("  [0]=", *(volatile unsigned long *)a);
            log_hex("  [4]=", *(volatile unsigned long *)(a + 4));
        }
    }
    return real_utilpush ? real_utilpush(a, b, c, d) : 0;
}

static unsigned long th_createreq(unsigned long a, unsigned long b,
                                     unsigned long c, unsigned long d)
{
    static int n;

    if (trace_on && n < 16) {
        n++;
        log_str("CREATEREQ");
        log_hex(" this=", a);
        log_hex(" id=", b);
        log_hex(" name=", c);
    }
    return real_createreq ? real_createreq(a, b, c, d) : 0;
}

static unsigned long th_mpush(unsigned long a, unsigned long b,
                                 unsigned long c, unsigned long d)
{
    static int n;

    if (trace_on && n < 32) {
        n++;
        log_str("MPUSH");
        log_hex(" this=", a);
        if (a > 0x10000 && a < 0x40000000UL)
            log_hex("  [0]=", *(volatile unsigned long *)a);
    }
    return real_mpush ? real_mpush(a, b, c, d) : 0;
}

/* Capture the _InfraInputKeyList handed to InfraInput::SetEventKeyList
 * (0x84D238).  Prologue is push.w + sub sp, so hook_one can forward safely. */
static const unsigned char pro_keylist[8] = {0x2d, 0xe9, 0xf0, 0x4f,
                                             0xa3, 0xb0, 0x04, 0xaf};
static draw_fn real_keylist;
static unsigned long g_infra_inst;
static unsigned long th_keylist(unsigned long a, unsigned long b,
                                unsigned long c, unsigned long d)
{
    g_infra_inst = a;
    log_hex("KEYLIST this=", a);
    log_hex("  idx=", b);
    log_hex("  list=", c);
    log_hex("  n=", d);
    if (c > 0x10000UL && c < 0x40000000UL && b < 32 && d > 0 && d < 400) {
        static unsigned long done;
        if (!(done & (1UL << b))) {
            char path[40];
            char *q = path;
            done |= 1UL << b;
            q = put_s(q, "/log/keylist_");
            q = put_x(q, b);
            q = put_s(q, ".bin");
            *q = 0;
            gpu_dump_blob(path, c, (unsigned long)d * 12UL);
        }
    }
    return real_keylist ? real_keylist(a, b, c, d) : 0;
}

static void install_trace_hooks(unsigned long base)
{
    static int done;
    if (!base || done)
        return;
    done = 1;
    log_hex("TRACEHOOK base=", base);
    hook_one(base, 0x718d90, pro_disp, (void *)th_disp,
             (void **)&real_disp);
    /* Observation table ported from the working zve10/A7M3 CPU UI: watch the
     * app's own draw/notify entry points without changing behaviour. */
    hook_one(base, 0x5cef88, pro_draw, (void *)hook_draw,
             (void **)&real_draw);
    hook_one(base, 0x71f080, pro_draw, (void *)hook_draw2,
             (void **)&real_draw2);
    hook_one(base, 0x5cd544, pro_notify, (void *)hook_notify,
             (void **)&real_notify);
    hook_one(base, 0x6314b2, pro_notify, (void *)hook_notify2,
             (void **)&real_notify2);
    hook_one(base, 0xc5fc98, pro_objdisp, (void *)th_objdisp,
             (void **)&real_objdisp);
    hook_one(base, 0x1e0e6c, pro_objrend, (void *)th_objrend,
             (void **)&real_objrend);
    hook_one_n(base, 0x82fc90, 10, pro_mgrctor, (void *)th_mgrctor,
               (void **)&real_mgrctor);
    hook_one(base, 0x3eba4c, pro_model_init, (void *)th_model_initialize,
             (void **)&real_model_initialize);
    hook_one(base, 0x76c540, pro_dmp_init, (void *)th_dmp_noop,
             (void **)&real_dmp_init);
    hook_one(base, 0x76c560, pro_dmp_init, (void *)th_dmp_noop,
             (void **)&real_dmp_exit);
    hook_one(base, 0x76c580, pro_dmp_fill, (void *)th_dmp_fill,
             (void **)&real_dmp_fillA);
    hook_one(base, 0x76c610, pro_dmp_fill, (void *)th_dmp_fill,
             (void **)&real_dmp_fillB);
    hook_one(base, 0x76c750, pro_dmp_fill, (void *)th_dmp_blit,
             (void **)&real_dmp_blitA);
    hook_one(base, 0x76c820, pro_dmp_fill, (void *)th_dmp_blit,
             (void **)&real_dmp_blitB);
    hook_one(base, 0x61db9c, pro_errhook, (void *)th_errhook,
             (void **)&real_errhook);
    /* Disabled: the keylist capture hook correlated with the 0x152c38
     * init race; tables were already extracted. */
    if (0)
        hook_one(base, 0x84d238, pro_keylist, (void *)th_keylist,
                 (void **)&real_keylist);
    /* Capture the TextRM assertion details (file/line/err).  Disabled while
     * probing views: the rebuilt shim with this hook hits a flaky
     * page-translation fault at 0x152c38 during startup. */
    if (0)
        hook_one(base, 0x61dc78, pro_assert, (void *)th_assert,
                 (void **)&real_assert_unused);
    /* Log TextRM font lookups: a miss is what makes loadFont assert. */
    /* (0x52d59c hook removed: some callers pass non-string keys and the
     *  re-implementation crashed at 0x152c38.  Use the FONT open log,
     *  the 0x61db9c stack and the 0x61dc78 assertion record instead.) */
    /* The ctor's [manager+0x14]->vtable[0x54]() call is Model::initialize.
     * Let the preceding [0x40] and 0x82F04E setup run (the EventManager map
     * needs it), then branch to the epilogue instead of calling the model. */
    {
        volatile unsigned short *p = (volatile unsigned short *)(base + 0x82fd7cUL);
        if (0 && *p == 0x4798) {
            sys3(SYS_mprotect, (long)((unsigned long)p & ~0xfffUL), 4096, 7);
            /* b 0x82fda8 : ctor epilogue */
            *p = 0xe014;
            cache_flush((void *)p, 2);
            log_str("MGRCTOR skip modelinit\n");
        } else {
            log_hex("MGRCTOR ret mismatch=", *p);
        }
    }
}

/* Log the .uxc resources currently mapped into im.elf (menu resource load
 * evidence, as in the A7M3 ui-rendering project). */
/* Send a synthetic 24-byte _InfraInputEvent through the app's own input
 * conversion (callback_convert) or the InputServant queue. */
static void send_event(unsigned long code, unsigned long status, int via_queue)
{
    unsigned long lb = libobj_base();
    unsigned long buf[6];
    int i;

    if (!lb)
        return;
    buf[0] = code;
    buf[1] = 0;
    buf[2] = status;
    for (i = 3; i < 6; i++)
        buf[i] = 0;
    log_hex("SEND code=", code);
    log_hex("  status=", status);
    log_hex("  via_queue=", (unsigned long)via_queue);
    if (via_queue) {
        typedef void (*snd_fn)(unsigned long *);
        snd_fn snd = (snd_fn)((lb + 0x3c00b0UL) | 1UL);
        snd(buf);
    } else {
        typedef void (*cb_fn)(unsigned long, unsigned long *);
        cb_fn cb = (cb_fn)((lb + 0x3c31b0UL) | 1UL);
        cb(0, buf);
    }
    log_str("SEND done\n");
}

/* A7C2-style Infra20 message sent to the global id 0x807A0020. */
static void inject_infra_key_g(unsigned long key, unsigned long status,
                               unsigned long from)
{
    typedef int (*valloc_fn)(unsigned, void **, unsigned, int);
    typedef int (*snd2_fn)(unsigned, void *);
    static valloc_fn valloc_p;
    static snd2_fn snd2;
    void *m = 0;
    unsigned long *w;
    int r1, r2;

    if (file_exists("/usr/tool/no_keyinj"))
        return;
    if (!valloc_p)
        valloc_p = (valloc_fn)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    if (!snd2)
        snd2 = (snd2_fn)dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!valloc_p || !snd2)
        return;
    r1 = valloc_p(0x807a0020u, &m, 20, 1);
    log_hex("IG key=", key);
    log_hex("  status=", status);
    log_hex("  valloc=", (unsigned long)r1);
    if (r1 || !m)
        return;
    w = (unsigned long *)m;
    w[0] = key;
    w[1] = 0;
    w[2] = status;
    w[3] = 0;
    w[4] = from;
    r2 = snd2(0x807a0020u, m);
    log_hex("  snd=", (unsigned long)r2);
}

/* Send a 24-byte _InfraInputEvent straight to the InputServant queue
 * (0x14A0429) that snd_InfraInputEvent_to_InputServant uses. */
static void inject_servant_event(unsigned long key, unsigned long status)
{
    typedef int (*valloc_fn)(unsigned, void **, unsigned, int);
    typedef int (*snd2_fn)(unsigned, void *);
    static valloc_fn valloc_p;
    static snd2_fn snd2;
    void *m = 0;
    unsigned long *w;
    int r1, r2;

    if (file_exists("/usr/tool/no_keyinj")) {
        log_str("SEVT control-run skip\n");
        return;
    }
    if (!valloc_p)
        valloc_p = (valloc_fn)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    if (!snd2)
        snd2 = (snd2_fn)dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!valloc_p || !snd2)
        return;
    r1 = valloc_p(0x14a0429u, &m, 24, 1);
    log_hex("SEVT key=", key);
    log_hex("  status=", status);
    log_hex("  valloc=", (unsigned long)r1);
    if (r1 || !m)
        return;
    w = (unsigned long *)m;
    w[0] = key;
    w[1] = 0;
    w[2] = status;
    w[3] = 0x7fffffffUL;
    w[4] = 0;
    w[5] = 0;
    r2 = snd2(0x14a0429u, m);
    log_hex("  snd=", (unsigned long)r2);
}

/* A7C2-style Infra20 key message: 20-byte {key, sub, status, ex, from}
 * allocated with osal_valloc_msg_wait and sent with osal_snd_msg(qid,msg). */
static void inject_infra_key(unsigned long key, unsigned long status,
                             unsigned long from)
{
    typedef int (*valloc_fn)(unsigned, void **, unsigned, int);
    typedef int (*snd2_fn)(unsigned, void *);
    static valloc_fn valloc_p;
    static snd2_fn snd2;
    void *m = 0;
    unsigned long *w;
    int r1, r2;

    if (file_exists("/usr/tool/no_keyinj")) {
        log_str("IKEY control-run skip\n");
        return;
    }
    if (!valloc_p)
        valloc_p = (valloc_fn)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    if (!snd2)
        snd2 = (snd2_fn)dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!valloc_p || !snd2) {
        log_str("IKEY no api\n");
        return;
    }
    r1 = valloc_p(0x7a0020u, &m, 20, 1);
    log_hex("IKEY key=", key);
    log_hex("  status=", status);
    log_hex("  valloc=", (unsigned long)r1);
    if (r1 || !m)
        return;
    w = (unsigned long *)m;
    w[0] = key;
    w[1] = 0;
    w[2] = status;
    w[3] = 0;
    w[4] = from;
    r2 = snd2(0x7a0020u, m);
    log_hex("  snd=", (unsigned long)r2);
}

/* Send a message to the app's input receive queue (0x7A0020) via the real
 * osal_snd_msg, mimicking the kernel/input proxy messages. */
static void osal_send_input(unsigned long w0, unsigned long w1,
                            unsigned long w2, unsigned long w3)
{
    typedef int (*snd_fn)(unsigned, const void *, unsigned, unsigned);
    typedef int (*valloc_fn)(unsigned, void **, unsigned, unsigned);
    static snd_fn snd;
    static valloc_fn valloc_p;
    void *m = 0;
    unsigned long *pw;
    int r1, r2;

    if (file_exists("/usr/tool/no_keyinj")) {
        log_str("OMSND control-run skip\n");
        return;
    }
    if (!snd)
        snd = (snd_fn)dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!valloc_p)
        valloc_p = (valloc_fn)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
    if (!snd || !valloc_p) {
        log_str("OMSND no api\n");
        return;
    }
    r1 = valloc_p(0x7a0020u, &m, 16, 0);
    log_hex("OMSND w0=", w0);
    log_hex("  w1=", w1);
    log_hex("  w2=", w2);
    log_hex("  valloc_ret=", (unsigned long)r1);
    log_hex("  msg=", (unsigned long)m);
    if (r1 < 0 || !m)
        return;
    pw = (unsigned long *)m;
    pw[0] = w0;
    pw[1] = w1;
    pw[2] = w2;
    pw[3] = w3;
    /* Mark the message as coming from the kernel input queue so the
     * receive-side debug path (header != 0x7A0020) prints it. */
    pw[-1] = 0x720018UL;
    r2 = snd(0x7a0020u, m, 16, 0);
    log_hex("  snd_ret=", (unsigned long)r2);
}

/* Replicate callback_convert's dispatch: createEvent(inst, action, 4) then
 * call the same sender veneer (libObj 0xFE2E4) with (event, 0x20, param). */
static void dispatch_event(unsigned long action)
{
    unsigned long lb = libobj_base();
    unsigned long inst;
    typedef void *(*create_fn)(unsigned long, unsigned long, unsigned long);
    typedef unsigned long (*send_fn)(void *, unsigned long, void *);
    create_fn create;
    send_fn send;
    void *ev;
    unsigned long r;

    if (!lb)
        return;
    if (file_exists("/usr/tool/no_keyinj")) {
        log_str("DISP control-run skip\n");
        return;
    }
    inst = *(volatile unsigned long *)(lb + 0x129ebf8UL);
    if (!inst)
        return;
    create = (create_fn)((lb + 0x3c031eUL) | 1UL);
    send = (send_fn)((lb + 0xfe2e4UL) | 0UL);   /* ARM veneer */
    ev = create(inst, action, 4);
    log_hex("DISP action=", action);
    log_hex("  ev=", (unsigned long)ev);
    if (!ev)
        return;
    r = send(ev, 0x20UL, (void *)0);
    log_hex("  send_ret=", r);
}

/* Send an explicitly built 24-byte _InfraInputEvent through callback_convert. */
static void send_raw(unsigned long w0, unsigned long w1, unsigned long w2,
                     unsigned long w3, int via_queue)
{
    unsigned long lb = libobj_base();
    unsigned long buf[6];

    if (!lb)
        return;
    buf[0] = w0;
    buf[1] = w1;
    buf[2] = w2;
    buf[3] = w3;
    buf[4] = 0;
    buf[5] = 0;
    log_hex("RAW w0=", w0);
    log_hex("  w1=", w1);
    log_hex("  w2=", w2);
    log_hex("  w3=", w3);
    log_hex("  q=", (unsigned long)via_queue);
    if (via_queue) {
        typedef void (*snd_fn)(unsigned long *);
        snd_fn snd = (snd_fn)((lb + 0x3c00b0UL) | 1UL);
        snd(buf);
    } else {
        typedef void (*cb_fn)(unsigned long, unsigned long *);
        cb_fn cb = (cb_fn)((lb + 0x3c31b0UL) | 1UL);
        cb(0, buf);
    }
    log_str("RAW done\n");
}

/* Enumerate InfraInput's internal per-index tables (0x84C5F4 lookup):
 * node+0x28 = count, node+0x2c = table, entry stride 0x44,
 * entry+0x04 = event code, entry+0x0C = 24-byte event template. */
static void dump_event_table(void)
{
    unsigned long lb = libobj_base();
    typedef unsigned long (*find_fn)(unsigned long, unsigned long);
    find_fn find;
    unsigned long idx;
    int lines = 0;
    char line[192];
    char *q;

    if (!lb || !g_infra_inst) {
        log_str("EVT noinst\n");
        return;
    }
    find = (find_fn)((lb + 0x84c5f4UL) | 1UL);
    for (idx = 0; idx < 0x40 && lines < 140; idx++) {
        unsigned long node = find(g_infra_inst, idx);
        unsigned long count, table, i;

        if (!node || node < 0x10000UL || node > 0x40000000UL)
            continue;
        count = *(volatile unsigned long *)(node + 0x28UL);
        table = *(volatile unsigned long *)(node + 0x2cUL);
        if (!count || count > 0x400 || !table ||
            table < 0x10000UL || table > 0x40000000UL)
            continue;
        log_hex("EVT idx=", idx);
        log_hex("  node=", node);
        log_hex("  count=", count);
        log_hex("  table=", table);
        for (i = 0; i < count && lines < 140; i++) {
            unsigned long e = table + i * 0x44UL;
            unsigned long code = *(volatile unsigned long *)(e + 4UL);
            unsigned long *t = (unsigned long *)(e + 0xcUL);

            if (!code || code > 0x7fffffffUL)
                continue;
            q = line;
            q = put_s(q, "EVT ");
            q = put_x(q, idx);
            q = put_s(q, ":");
            q = put_x(q, i);
            q = put_s(q, " code=");
            q = put_x(q, code);
            q = put_s(q, " t=");
            q = put_x(q, t[0]);
            q = put_s(q, ",");
            q = put_x(q, t[1]);
            q = put_s(q, ",");
            q = put_x(q, t[2]);
            q = put_s(q, ",");
            q = put_x(q, t[3]);
            q = put_s(q, ",");
            q = put_x(q, t[4]);
            q = put_s(q, ",");
            q = put_x(q, t[5]);
            *q++ = '\n';
            out(line, (int)(q - line));
            lines++;
        }
    }
}

/* Build a real _InfraInputEvent via InfraInput::GetEventStatus and feed it
 * to InputService::callback_convert (the app's own key conversion path). */
static void inject_key_by_id(unsigned long keyid)
{
    unsigned long lb = libobj_base();
    unsigned long buf[8];
    typedef unsigned long (*ges_fn)(unsigned long, unsigned long,
                                    unsigned long, unsigned long *);
    typedef void (*cb_fn)(unsigned long, unsigned long *);
    ges_fn ges;
    cb_fn cb;
    long r;
    int i;

    if (!lb || !g_infra_inst) {
        log_str("KEYID nosvc\n");
        return;
    }
    for (i = 0; i < 8; i++)
        buf[i] = 0;
    ges = (ges_fn)((lb + 0x84c764UL) | 1UL);
    cb = (cb_fn)((lb + 0x3c31b0UL) | 1UL);
    r = (long)ges(g_infra_inst, 4, keyid, buf);
    log_hex("KEYID key=", keyid);
    log_hex("  ges_ret=", (unsigned long)r);
    for (i = 0; i < 6; i++)
        log_hex("  ev=", buf[i]);
    if ((long)r >= 0) {
        cb(0, buf);
        log_str("KEYID dispatched\n");
    }
}

/* Dump the InfraInput key table: instance+0x28 = count, +0x2c = table,
 * entry stride 0x44, key id at +4, 24-byte event template at +0xc. */
static void dump_key_table(void)
{
    unsigned long lb = libobj_base();
    typedef void *(*gi_fn)(void);
    gi_fn gi;
    unsigned long inst, count, table, i;
    char line[160];
    char *q;
    int n;

    if (!lb) {
        log_str("KTBL nobase\n");
        return;
    }
    gi = (gi_fn)((lb + 0x84c004UL) | 1UL);
    inst = (unsigned long)gi();
    log_hex("KTBL inst=", inst);
    if (!inst || inst < 0x10000UL || inst > 0x40000000UL)
        return;
    count = *(volatile unsigned long *)(inst + 0x28UL);
    table = *(volatile unsigned long *)(inst + 0x2cUL);
    log_hex("KTBL count=", count);
    log_hex("KTBL table=", table);
    if (!table || table < 0x10000UL || table > 0x40000000UL)
        return;
    if (count > 40)
        count = 40;
    for (i = 0; i < count; i++) {
        unsigned long e = table + i * 0x44UL;
        unsigned long id = *(volatile unsigned long *)(e + 4);
        unsigned long *ev = (unsigned long *)(e + 0xc);
        q = line;
        q = put_s(q, "KTBL ");
        q = put_x(q, i);
        q = put_s(q, " id=");
        q = put_x(q, id);
        q = put_s(q, " ev=");
        q = put_x(q, ev[0]);
        q = put_s(q, ",");
        q = put_x(q, ev[1]);
        q = put_s(q, ",");
        q = put_x(q, ev[2]);
        q = put_s(q, ",");
        q = put_x(q, ev[3]);
        q = put_s(q, ",");
        q = put_x(q, ev[4]);
        q = put_s(q, ",");
        q = put_x(q, ev[5]);
        *q++ = '\n';
        out(line, (int)(q - line));
        n = 0;
        (void)n;
    }
}

/* Try to inject a key Event into the running UI through InputService.
 * createEvent(m, h) -> Event*, then pushFilterEvent(this, Event*). */
static void inject_key_once(unsigned long code, unsigned long h)
{
    unsigned long lb = libobj_base();
    unsigned long inst;
    typedef void *(*create_fn)(unsigned long, unsigned long, unsigned long);
    typedef unsigned long (*push_fn)(void *, void *);
    create_fn create;
    push_fn push;
    void *ev;
    unsigned long r;
    static void *kept_ev;

    if (!lb) {
        log_str("KEYINJ nobase\n");
        return;
    }
    if (file_exists("/usr/tool/no_keyinj")) {
        log_str("KEYINJ control-run skip\n");
        return;
    }
    inst = *(volatile unsigned long *)(lb + 0x129ebf8UL);
    /* Thumb interworking: function pointers must carry bit0. */
    create = (create_fn)((lb + 0x3c031eUL) | 1UL);
    push = (push_fn)((lb + 0x3c1ac4UL) | 1UL);
    log_hex("KEYINJ enter code=", code);
    log_hex("  inst=", inst);
    log_hex("  create=", (unsigned long)create);
    log_hex("  push=", (unsigned long)push);
    if (!inst || inst < 0x10000UL || inst > 0x40000000UL) {
        log_str("KEYINJ bad inst\n");
        return;
    }
    ev = create(inst, code, h);
    log_hex("  ev=", (unsigned long)ev);
    if (!ev)
        return;
    kept_ev = ev;
    r = push((void *)inst, kept_ev);
    log_hex("  push_ret=", r);
}

/* List sysfs buses/devices related to the udif input stack. */
static void dump_sysfs_bus(const char *path, int filter)
{
    char buf[4096];
    int fd, n, off;

    fd = (int)sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0)
        return;
    log_str("SYSFS ");
    out(path, (int)slen(path));
    out("\n", 1);
    for (;;) {
        n = (int)sys3(217 /*getdents64*/, fd, (long)buf, sizeof(buf));
        if (n <= 0)
            break;
        off = 0;
        while (off < n) {
            unsigned short reclen = *(unsigned short *)(buf + off + 16);
            const char *name = buf + off + 19;
            if (name[0] && name[0] != '.' &&
                (!filter || has_sub(name, "udif") || has_sub(name, "input") ||
                 has_sub(name, "gpio") || has_sub(name, "subcpu") ||
                 has_sub(name, "kbd") || has_sub(name, "i2c"))) {
                log_str("  SYS ");
                out(name, (int)slen(name));
                out("\n", 1);
            }
            off += reclen;
        }
    }
    sys3(SYS_close, fd, 0, 0);
}

/* Probe /proc for the input.ko factory proc nodes (adc/gpio/front/vgrip). */
#define SYS_getdents64 217
static void probe_proc_input(void)
{
    static const char *roots[] = { "/proc", "/dev", "/proc/kbd",
                                   "/proc/input", 0 };
    char buf[4096];
    int r, i, shown = 0;

    for (i = 0; roots[i]; i++) {
        int fd = (int)sys3(SYS_open, (long)roots[i], 0, 0);
        if (fd < 0) {
            log_str("PROC missing ");
            out(roots[i], (int)slen(roots[i]));
            out("\n", 1);
            continue;
        }
        log_str("PROC dir ");
        out(roots[i], (int)slen(roots[i]));
        out("\n", 1);
        for (;;) {
            int n = (int)sys3(SYS_getdents64, fd, (long)buf, sizeof(buf));
            int off = 0;
            if (n <= 0)
                break;
            while (off < n) {
                unsigned short reclen = *(unsigned short *)(buf + off + 16);
                const char *name = buf + off + 19;
                if (name[0] && name[0] != '.') {
                    int want = (i > 0) ||
                        (has_sub(name, "kbd") || has_sub(name, "input") ||
                         has_sub(name, "key") || has_sub(name, "adc") ||
                         has_sub(name, "front") || has_sub(name, "gpio") ||
                         has_sub(name, "vgrip") || has_sub(name, "udif") ||
                         has_sub(name, "driver") || has_sub(name, "misc") ||
                         has_sub(name, "dev"));
                    if (want && shown < 160) {
                        shown++;
                        log_str("  PROCENT ");
                        out(name, (int)slen(name));
                        out("\n", 1);
                    }
                }
                off += reclen;
            }
        }
        sys3(SYS_close, fd, 0, 0);
    }
    {
        unsigned long lb = libobj_base();
        if (lb) {
            log_hex("INPUTSVC m_pMyInstance=",
                    *(volatile unsigned long *)(lb + 0x129ebf8UL));
            log_hex("VIEWMGR pvm=",
                    *(volatile unsigned long *)(lb + 0x129ee1cUL));
            log_hex("DISPMGR=",
                    *(volatile unsigned long *)(lb + 0x121f070UL));
        } else {
            log_str("INPUTSVC nobase\n");
        }
    }
    r = 0;
    (void)r;
}

static void dump_uxc_maps(void)
{
    static char mb[131072];
    int fd, n, total = 0, i, start, shown = 0;

    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return;
    while (total < (int)sizeof(mb) - 1) {
        n = (int)sys3(SYS_read, fd, (long)(mb + total),
                      (long)(sizeof(mb) - 1 - total));
        if (n <= 0)
            break;
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    if (total <= 0)
        return;
    mb[total] = 0;
    for (i = 0, start = 0; i <= total; i++) {
        if (i == total || mb[i] == '\n') {
            mb[i] = 0;
            if (shown < 24 && has_sub(mb + start, ".uxc")) {
                log_str("UXC ");
                out(mb + start, (int)slen(mb + start));
                out("\n", 1);
                shown++;
            }
            start = i + 1;
        }
    }
}

static int maps_has(const char *needle)
{
    static char mbuf[262144];
    int fd, n, total = 0;

    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return 0;
    while (total < (int)sizeof(mbuf) - 1) {
        n = (int)sys3(SYS_read, fd, (long)(mbuf + total),
                      (long)(sizeof(mbuf) - 1 - total));
        if (n <= 0)
            break;
        total += n;
    }
    sys3(SYS_close, fd, 0, 0);
    if (total <= 0)
        return 0;
    mbuf[total] = 0;
    return has_sub(mbuf, needle);
}

static unsigned long elf_mem_end(unsigned long base)
{
    unsigned char *e = (unsigned char *)base;
    unsigned long phoff, phentsize, phnum, i, max = 0;

    if (!base || e[0] != 0x7f || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
        return 0;
    phoff = *(volatile unsigned int *)(e + 28);
    phentsize = *(volatile unsigned short *)(e + 42);
    phnum = *(volatile unsigned short *)(e + 44);
    for (i = 0; i < phnum; ++i) {
        unsigned char *ph = e + phoff + i * phentsize;
        unsigned long v = *(volatile unsigned int *)(ph + 8);
        unsigned long m = *(volatile unsigned int *)(ph + 20);
        if (v + m > max)
            max = v + m;
    }
    return max;
}

static void probe_viewmgr(void)
{
    unsigned long base = libobj_base();
    unsigned long s1, m1, s2, m2, pvm, ge_slot, mem_end, pva7r3 = 0, pva7m3 = 0;
    char b[384];
    char *p;
    int vu, grm, uxe, dds;

    if (!base) {
        log_str("VMGR no libObj\n");
        return;
    }
    /* 0x121EC6C is the GOT slot used by viewManagerIf::openView;
     * 0x12209EC is the one used by viewManagerIf::requestModelExecute.
     * Report the slot addresses; the manager itself is the .bss word read
     * directly below.  Do not dereference the slots here: they are pointers
     * into libObj data and are not guaranteed sane this early. */
    s1 = *(volatile unsigned long *)(base + 0x121ec6cUL);
    s2 = *(volatile unsigned long *)(base + 0x12209ecUL);
    m1 = 0;
    m2 = 0;
    pvm = *(volatile unsigned long *)(base + 0x129ee1cUL);
    ge_slot = *(volatile unsigned long *)(base + 0x129f40cUL);
    mem_end = elf_mem_end(base);
    if (mem_end > 0x12d4c08UL)
        pva7r3 = *(volatile unsigned long *)(base + 0x12d4c04UL);
    if (mem_end > 0x13289f8UL)
        pva7m3 = *(volatile unsigned long *)(base + 0x13289f4UL);

    vu = maps_has("viewUnified");
    grm = maps_has("libgrm");
    uxe = maps_has("libuxengine");
    dds = maps_has("libDDS");

    p = b;
    p = put_s(p, "VMGR base=");
    p = put_x(p, base);
    p = put_s(p, " s1=");
    p = put_x(p, s1);
    p = put_s(p, " m1=");
    p = put_x(p, m1);
    p = put_s(p, " s2=");
    p = put_x(p, s2);
    p = put_s(p, " m2=");
    p = put_x(p, m2);
    p = put_s(p, " pvm=");
    p = put_x(p, pvm);
    p = put_s(p, " ge=");
    p = put_x(p, ge_slot);
    p = put_s(p, " vu=");
    p = put_dec(p, (unsigned long)vu);
    p = put_s(p, " grm=");
    p = put_dec(p, (unsigned long)grm);
    p = put_s(p, " uxe=");
    p = put_dec(p, (unsigned long)uxe);
    p = put_s(p, " dds=");
    p = put_dec(p, (unsigned long)dds);
    p = put_s(p, " pViewA7R3=");
    p = put_x(p, pva7r3);
    p = put_s(p, " pViewA7M3=");
    p = put_x(p, pva7m3);
    *p++ = '\n';
    out(b, (int)(p - b));
}

static int file_exists(const char *path)
{
    int fd = (int)sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0)
        return 0;
    sys3(SYS_close, fd, 0, 0);
    return 1;
}

static void ui_kick(unsigned long base)
{
    unsigned long *inst = (unsigned long *)(base + 0x129ebf8UL);
    unsigned long *g1 = (unsigned long *)(base + 0x129ead0UL);
    unsigned long *g2 = (unsigned long *)(base + 0x129eba0UL);
    unsigned long *g3 = (unsigned long *)(base + 0x129eb10UL);
    unsigned long *g4 = (unsigned long *)(base + 0x129ead8UL);

    ui_kick_ran = 1;
    log_hex("UIKICK table g1=", *g1);
    log_hex("UIKICK table g2=", *g2);
    log_hex("UIKICK table g3=", *g3);
    log_hex("UIKICK table g4=", *g4);
    ((void (*)(void))((base + 0x3c3c35UL) | 1UL))();
    log_hex("UIKICK after table g1=", *g1);
    log_hex("UIKICK after table g2=", *g2);
    log_hex("UIKICK after table g3=", *g3);
    log_hex("UIKICK after table g4=", *g4);

    log_str("UIKICK before input=");
    log_hex("", *inst);
    ((void (*)(int, unsigned int, unsigned long))
        ((base + 0x3c441dUL) | 1UL))(1, 0, 0);
    log_str("UIKICK after activate input=");
    log_hex("", *inst);
    if (*inst) {
        ((void (*)(void))((base + 0x3c3c35UL) | 1UL))();
        log_str("UIKICK after update input=");
        log_hex("", *inst);
    }
    log_str("UIKICK pvm=");
    log_hex("", *(volatile unsigned long *)(base + 0x129ee1cUL));
}

static void view_kick(void)
{
    unsigned long vb = module_base("viewUnified2.so");
    unsigned long lb = libobj_base();
    unsigned long mgr = 0;
    typedef void *(*vfn)(unsigned long, unsigned long);
    vfn fn;
    void *obj;
    int i;

    log_hex("VIEWKICK base=", vb);
    if (!vb)
        return;
    if (lb)
        mgr = *(volatile unsigned long *)(lb + 0x129ee1cUL);
    log_hex("VIEWKICK mgr=", mgr);
    fn = (vfn)((vb + 0x1fc43bUL) | 1UL);
    obj = fn(0, mgr);
    log_hex("VIEWKICK obj=", (unsigned long)obj);
    if (obj) {
        for (i = 0; i < 12; i++)
            log_hex("VIEWKICK field=",
                    *(unsigned long *)((char *)obj + i * 4));
    }
}

static void fake_vm(void)
{
    unsigned long lb = libobj_base();
    unsigned long vb = module_base("viewUnified2.so");
    typedef void *(*root_fn)(unsigned long, unsigned long);
    typedef int (*open_fn)(void *, const char *, int, int);
    unsigned long *m;
    void *root;
    int i;

    if (!lb || !vb)
        return;
    m = (unsigned long *)(lb + 0x12d4c08UL);
    log_str("VMSTATIC dump\n");
    for (i = 0; i < 16; i++)
        log_hex("VMSTATIC=", m[i]);
    *(volatile unsigned long *)(lb + 0x12d4c04UL) = (unsigned long)m;
    *(volatile unsigned long *)(lb + 0x129ee1cUL) = (unsigned long)m;
    log_hex("FAKEVM pViewManager=", *(volatile unsigned long *)(lb + 0x12d4c04UL));
    log_hex("FAKEVM pvm=", *(volatile unsigned long *)(lb + 0x129ee1cUL));
    root = ((root_fn)((vb + 0x2764a5UL) | 1UL))(0, (unsigned long)m);
    log_hex("FAKEVM root=", (unsigned long)root);
    if (root) {
        log_hex("FAKEVM root/ROOT=",
                (unsigned long)((open_fn)((lb + 0x3dc275UL) | 1UL))(root, "view/ROOT", 0, 0));
        log_hex("FAKEVM root/STLREC=",
                (unsigned long)((open_fn)((lb + 0x3dc275UL) | 1UL))(root, "view/STLREC", 0, 0));
    }
    log_hex("FAKEVM done pViewManager=",
            *(volatile unsigned long *)(lb + 0x12d4c04UL));
}

static int open_view(const char *name)
{
    unsigned long lb = libobj_base();
    typedef int (*ov_fn)(const char *, int);
    int r = -1;

    log_str("OPENVIEW ");
    log_str(name);
    log_str("\n");
    if (lb)
        r = ((ov_fn)((lb + 0x3e0da9UL) | 1UL))(name, 0);
    log_hex("OPENVIEW r=", (unsigned long)r);
    if (lb) {
        log_hex("OPENVIEW pvm=",
                *(volatile unsigned long *)(lb + 0x129ee1cUL));
        log_hex("OPENVIEW pViewManager=",
                *(volatile unsigned long *)(lb + 0x12d4c04UL));
    }
    return r;
}

static void config_kick(void)
{
    unsigned long lb = libobj_base();
    unsigned long vb = module_base("viewUnified2.so");
    typedef unsigned long (*fn_t)(unsigned long);

    log_hex("CONFIG lb=", lb);
    log_hex("CONFIG vb=", vb);
    if (lb) {
        ((fn_t)((lb + 0x3657e5UL) | 1UL))(0);
        ((fn_t)((lb + 0x3784d1UL) | 1UL))(0);
    }
    if (vb)
        ((fn_t)((vb + 0x379141UL) | 1UL))(0);
    if (lb)
        log_hex("CONFIG pvm=",
                *(volatile unsigned long *)(lb + 0x129ee1cUL));
}

static void model_kick(void)
{
    unsigned long lb = libobj_base();
    unsigned long i;

    if (!lb)
        return;
    log_hex("MODELKICK base=", lb);
    log_hex("MODELKICK g_manager=", g_manager);
    {
        static int opened;
        unsigned long pvm = *(volatile unsigned long *)(lb + 0x129ee1cUL);
        log_hex("MODELKICK pvm=", pvm);
        if (pvm && !opened) {
            typedef unsigned long (*fn2)(unsigned long, unsigned long);
            unsigned long rv;
            rv = ((fn2)((lb + 0x3e0da9UL) | 1UL))(
                (unsigned long)"view/ROOT", 0);
            log_hex("MODELKICK open=", rv);
            opened = 1;
        }
    }
    log_hex("MODELKICK g_mgr_sp=", g_mgr_sp);
    log_hex("MODELKICK s1=", *(unsigned long *)(lb + 0x121ec6cUL));
    log_hex("MODELKICK pvm=", *(unsigned long *)(lb + 0x129ee1cUL));
    log_hex("MODELKICK slot2=", *(unsigned long *)(lb + 0x12209ecUL));
    {
        unsigned long p;
        p = *(unsigned long *)(lb + 0x121ec6cUL);
        if (p > 0x10000 && p < 0x40000000UL)
            log_hex("MODELKICK m1=", *(unsigned long *)p);
        p = *(unsigned long *)(lb + 0x12209ecUL);
        if (p > 0x10000 && p < 0x40000000UL)
            log_hex("MODELKICK m2=", *(unsigned long *)p);
    }
    if (g_mgr_sp > 0x10000) {
        unsigned long *p = (unsigned long *)(g_mgr_sp & ~3UL);
        log_str("MGRSTACK begin\n");
        for (i = 1; i < 0x300; i++) {
            unsigned long v = p[-(long)i];
            if (v >= lb && v < lb + 0xe00000UL)
                log_hex("MGRSTACK code ", v - lb);
        }
        log_str("MGRSTACK end\n");
    }
}

static void root_kick(void)
{
    unsigned long vb = module_base("viewUnified2.so");
    unsigned long lb = libobj_base();
    unsigned long mgr = 0;
    typedef void *(*fn_t)(unsigned long, unsigned long);
    void *o;
    int i;

    log_hex("ROOTKICK base=", vb);
    if (!vb)
        return;
    if (lb)
        mgr = *(volatile unsigned long *)(lb + 0x129ee1cUL);
    log_hex("ROOTKICK mgr=", mgr);
    o = ((fn_t)((vb + 0x2764a5UL) | 1UL))(0, mgr);
    log_hex("ROOTKICK obj=", (unsigned long)o);
    if (o) {
        for (i = 0; i < 16; i++)
            log_hex("ROOTKICK field=",
                    *(unsigned long *)((char *)o + i * 4));
    }
    if (lb) {
        log_hex("ROOTKICK pvm=", *(unsigned long *)(lb + 0x129ee1cUL));
        log_hex("ROOTKICK pViewManager=",
                *(unsigned long *)(lb + 0x12d4c04UL));
    }
}

static void ui_kick_if_ready(void)
{
    unsigned long base = libobj_base();
    if (!base)
        return;
    if (!*(volatile unsigned long *)(base + 0x129f40cUL))
        return;                     /* GraphicsEngine not up */
    if (!maps_has("viewUnified"))
        return;                     /* UI library not loaded */
    if (!file_exists("/usr/tool/ui_kick"))
        return;                     /* experimental switch */
    ui_kick(base);
}

#define UI_SET_EVBIT   0x40045564u
#define UI_SET_KEYBIT  0x40045565u
#define UI_DEV_CREATE  0x5501u
#define UI_DEV_DESTROY 0x5502u

static int uinput_fd = -1;

/* Create the uinput keyboard once, before the app enumerates input devices,
 * and keep it registered so InputService can open it at init. */
static int uinput_open(void)
{
    static unsigned char dev[1116];   /* sizeof(struct uinput_user_dev) */
    int fd, i;

    if (uinput_fd >= 0)
        return uinput_fd;
    for (i = 0; i < (int)sizeof(dev); i++)
        dev[i] = 0;
    dev[0] = 'a'; dev[1] = '7'; dev[2] = 'r'; dev[3] = '3';
    dev[4] = 'k'; dev[5] = 'b'; dev[6] = 'd';
    dev[80] = 0x18;             /* bustype = BUS_I2C */

    fd = (int)sys3(SYS_open, (long)"/dev/uinput", 2 /*O_RDWR*/, 0);
    log_int("UIKEY open=", fd);
    if (fd < 0)
        return -1;
    {
        long rw = sys3(SYS_write, fd, (long)dev, (long)sizeof(dev));
        long re = sys3(SYS_ioctl, fd, UI_SET_EVBIT, 1);      /* EV_KEY */
        long rk = sys3(SYS_ioctl, fd, UI_SET_KEYBIT, 139);   /* KEY_MENU */
        long rc = sys3(SYS_ioctl, fd, UI_DEV_CREATE, 0);
        log_hex("UIKEY w=", (unsigned long)rw);
        log_hex("UIKEY ev=", (unsigned long)re);
        log_hex("UIKEY key=", (unsigned long)rk);
        log_hex("UIKEY create=", (unsigned long)rc);
        if (rc < 0) {
            sys3(SYS_close, fd, 0, 0);
            return -1;
        }
    }
    uinput_fd = fd;
    log_str("UIKEY registered\n");
    return fd;
}

static void uinput_key(unsigned int code)
{
    unsigned int ev[4];
    int fd = uinput_open();

    if (fd < 0)
        return;

    ev[0] = 0; ev[1] = 0;
    ev[2] = (code << 16) | 1u; ev[3] = 1;           /* press */
    sys3(SYS_write, fd, (long)ev, 16);
    ev[2] = 0; ev[3] = 0;                           /* SYN */
    sys3(SYS_write, fd, (long)ev, 16);
    ev[2] = (code << 16) | 1u; ev[3] = 0;           /* release */
    sys3(SYS_write, fd, (long)ev, 16);
    ev[2] = 0; ev[3] = 0;                           /* SYN */
    sys3(SYS_write, fd, (long)ev, 16);
    log_str("UIKEY done\n");
}

static void dump_net_file(const char *path, int max)
{
    int fd = (int)sys3(SYS_open, (long)path, 0, 0);
    char buf[512];
    int n, i;

    if (fd < 0)
        return;
    log_str("NET ");
    log_str(path);
    log_str("\n");
    for (i = 0; i < max; i++) {
        n = (int)sys3(SYS_read, fd, (long)buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = 0;
        log_str(buf);
        if (buf[n - 1] != '\n')
            log_str("\n");
    }
    sys3(SYS_close, fd, 0, 0);
}

static void net_probe(void)
{
    int fd, n;
    char b[0x100];

    log_str("NETPROBE\n");
    dump_net_file("/proc/net/tcp", 16);
    dump_net_file("/proc/net/tcp6", 8);
    dump_net_file("/proc/net/unix", 16);
    fd = (int)sys3(SYS_open, (long)"/setting/ulogio.bin", 0, 0);
    if (fd >= 0) {
        n = (int)sys3(SYS_read, fd, (long)b, sizeof(b));
        sys3(SYS_close, fd, 0, 0);
        if (n > 0x9c)
            log_hex("TELN=", (unsigned char)b[0x9c]);
        else
            log_str("TELN short\n");
    } else {
        log_str("ulogio.bin open fail\n");
    }
}

static void telnet_probe(void)
{
    struct {
        unsigned short f;
        unsigned short p;
        unsigned int a;
        unsigned char pad[8];
    } sa;
    int fd, n, i, j;
    char b[512];

    log_str("TELNET start\n");
    fd = (int)sys3(281 /* socket */, 2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
    log_hex("TELNET fd=", (unsigned long)fd);
    if (fd < 0) {
        log_str("TELNET socket fail\n");
        return;
    }
    sa.f = 2;
    sa.p = 0x1700;              /* htons(23) */
    sa.a = 0x0100007f;          /* 127.0.0.1 */
    for (i = 0; i < 8; i++)
        sa.pad[i] = 0;
    {
        long cr = sys3(283 /* connect */, fd, (long)&sa, 16);
        log_hex("TELNET connect ret=", (unsigned long)cr);
        log_hex("TELNET sa0=", *(unsigned long *)&sa);
        log_hex("TELNET sa1=", *(unsigned long *)((char *)&sa + 4));
        if (cr < 0) {
            log_str("TELNET connect fail\n");
            sys3(SYS_close, fd, 0, 0);
            return;
        }
    }
    log_str("TELNET connected\n");
    sys3(55 /* fcntl */, fd, 4 /* F_SETFL */, 0x800 /* O_NONBLOCK */);
    sys3(SYS_write, fd, (long)"\r\n", 2);
    for (i = 0; i < 8; i++) {
        long ts[2];
        ts[0] = 0;
        ts[1] = 200000000;
        sys3(162 /* nanosleep */, (long)ts, 0, 0);
        n = (int)sys3(SYS_read, fd, (long)b, sizeof(b) - 1);
        if (n > 0) {
            b[n] = 0;
            log_str("TELNET recv: ");
            for (j = 0; j < n; j++) {
                if (b[j] >= 32 && b[j] < 127) {
                    char s[2];
                    s[0] = b[j];
                    s[1] = 0;
                    log_str(s);
                } else {
                    log_hex("", (unsigned char)b[j]);
                }
            }
            log_str("\n");
        } else {
            log_hex("TELNET read ret=", (unsigned long)n);
        }
        sys3(SYS_write, fd, (long)"ps\r\n", 4);
    }
    sys3(SYS_close, fd, 0, 0);
    log_str("TELNET done\n");
}

static void open_view_once(void)
{
    unsigned long base = libobj_base();
    typedef int (*ov_fn)(const char *, int);
    ov_fn ov;
    int r;

    if (!base) {
        log_str("OPENVIEW nobase\n");
        return;
    }
    ov = (ov_fn)(base + 0x003e0da9UL);
    r = ov("view/BASE_MENU", 0);
    log_int("OPENVIEW once r=", r);
}

/* Open another named view from the app's draw thread.  Used to probe which
 * view actually builds the menu items (and therefore the text sprites). */
static void open_view_named(const char *name)
{
    unsigned long base = libobj_base();
    typedef int (*ov_fn)(const char *, int);
    ov_fn ov;
    int r;

    if (!base) {
        log_str("OPENVIEW nobase\n");
        return;
    }
    ov = (ov_fn)(base + 0x003e0da9UL);
    r = ov(name, 0);
    log_str("OPENVIEW ");
    log_str(name);
    log_int(" r=", r);
}

static void open_view_probe(void)
{
    unsigned long base = libobj_base();
    typedef int (*ov_fn)(const char *, int);
    ov_fn ov;
    static const char *names[] = {
        "view/BASE_MENU", "view/FNMENU", "view/GLOBAL_MENU",
        "view/COMMON_SHOOTING_SETTING", "view/SCENARIO_MENU", 0
    };
    int i;

    if (!base) {
        log_str("OPENVIEW nobase\n");
        return;
    }
    ov = (ov_fn)(base + 0x003e0da9UL);
    log_hex("OPENVIEW fn=", (unsigned long)(base + 0x003e0da9UL));
    for (i = 0; names[i]; i++) {
        int r = ov(names[i], 0);
        log_str("OPENVIEW ");
        log_str(names[i]);
        log_int(" r=", r);
    }
}

static void scan_self_tasks(void)
{
    unsigned long tid;
    for (tid = 1; tid < 600; tid++) {
        char path[64];
        char comm[64];
        char wch[64];
        char line[160];
        char *p;

        p = path;
        p = put_s(p, "/proc/self/task/");
        p = put_dec(p, tid);
        p = put_s(p, "/comm");
        *p = 0;
        probe_read(path, comm, sizeof(comm));
        if (!comm[0])
            continue;
        p = path;
        p = put_s(p, "/proc/self/task/");
        p = put_dec(p, tid);
        p = put_s(p, "/wchan");
        *p = 0;
        probe_read(path, wch, sizeof(wch));
        if (!wch[0])
            continue;
        p = line;
        p = put_s(p, "MTHR tid=");
        p = put_dec(p, tid);
        p = put_s(p, " c=");
        p = put_s(p, comm);
        p = put_s(p, " w=");
        p = put_s(p, wch);
        *p++ = '\n';
        sys3(SYS_write, 1, (long)line, (long)(p - line));
    }
}

static void monitor_loop(void)
{
    int i;
    for (i = 0; i < 24; i++) {
        long ts[2];
        ts[0] = 5;
        ts[1] = 0;
        sys3(162 /*nanosleep*/, (long)ts, 0, 0);
        scan_self_tasks();
    }
    for (;;) {
        long ts[2];
        ts[0] = 10;
        ts[1] = 0;
        sys3(162 /*nanosleep*/, (long)ts, 0, 0);
    }
}

static void probe_liro_threads(void)
{
    unsigned long pid;
    for (pid = 1; pid < 500; pid++) {
        char path[48];
        char comm[64];
        char wch[64];
        char line[160];
        char *p;

        p = path;
        p = put_s(p, "/proc/");
        p = put_dec(p, pid);
        p = put_s(p, "/comm");
        *p = 0;
        probe_read(path, comm, sizeof(comm));
        if (!comm[0])
            continue;
        if (!has_sub(comm, "liro") && !has_sub(comm, "udm"))
            continue;

        p = path;
        p = put_s(p, "/proc/");
        p = put_dec(p, pid);
        p = put_s(p, "/wchan");
        *p = 0;
        probe_read(path, wch, sizeof(wch));

        p = line;
        p = put_s(p, "LTHR pid=");
        p = put_dec(p, pid);
        p = put_s(p, " c=");
        p = put_s(p, comm);
        p = put_s(p, " w=");
        p = put_s(p, wch);
        *p++ = '\n';
        sys3(SYS_write, 1, (long)line, (long)(p - line));
    }
}

static void probe_loop(void)
{
    static const char first[] = "PROBE start\n";
    static int early_done;
    int round = 0;
    sys3(SYS_write, 1, (long)first, sizeof(first) - 1);
    for (;;) {
        long ts[2];
        if (!early_done) {
            unsigned long b = libobj_base();
            if (!b) {
                ts[0] = 0;
                ts[1] = 50000000;      /* poll libObj every 50 ms */
                sys3(162 /* nanosleep */, (long)ts, 0, 0);
                continue;
            }
            ts[0] = 0;
            ts[1] = 1000000;           /* hook as soon as libObj is mapped */
            sys3(162 /* nanosleep */, (long)ts, 0, 0);
            install_trace_hooks(b);
            hook_bizfw();
            early_done = 1;
            continue;
        }
        /* sleep ~6s: struct timespec {6, 0} lives on this static stack frame */
        ts[0] = 6;
        ts[1] = 0;
        sys3(162 /* nanosleep */, (long)ts, 0, 0);
        hook_bizfw();
        if (file_exists("/usr/tool/produce")) {
            compose_seq++;
            compose_frame(compose_seq);
        }
        /* probe_once(); disabled: reading other tasks' /syscall perturbs the
         * app (early page faults). */
        round++;
        if (file_exists("/usr/tool/gpuscan"))
            gpu_scan_descs(round);
        if (round == 1) {
            /* Diagnostic: are liro.ko's two unresolved symbols exported? */
            static char ks[4 * 1024 * 1024];
            int kfd = (int)sys3(SYS_open, (long)"/proc/kallsyms", 0, 0);
            int total = 0, r;
            if (kfd >= 0) {
                while (total < (int)sizeof(ks) - 1) {
                    r = (int)sys3(SYS_read, kfd, (long)(ks + total),
                                  (long)(sizeof(ks) - 1 - total));
                    if (r <= 0)
                        break;
                    total += r;
                }
                sys3(SYS_close, kfd, 0, 0);
                ks[total] = 0;
                log_int("KALLSYMS bytes=", total);
                log_hex("KS dmm_direct_call=",
                        (unsigned long)has_sub(ks, "dmm_direct_call"));
                log_hex("KS dmac_mem_transfer_async=",
                        (unsigned long)has_sub(ks,
                                               "dmac_mem_transfer_async"));
            } else {
                log_str("KALLSYMS open fail\n");
            }
        }
        if (round == 2) {
            /* liro.ko is the LIRO display-engine consumer; the QEMU image
             * omits it from the initrd module list.  Load it here (the
             * kernel's init_module syscall on ARM is 105) so the Sugilite /
             * GL messages have a consumer. */
            static unsigned char kbuf[131072];
            int fd = (int)sys3(SYS_open, (long)"/usr/kmod/liro.ko", 0, 0);
            if (fd >= 0) {
                int n = 0, r;
                while (n < (int)sizeof(kbuf)) {
                    r = (int)sys3(SYS_read, fd, (long)(kbuf + n),
                                  (long)(sizeof(kbuf) - n));
                    if (r <= 0)
                        break;
                    n += r;
                }
                sys3(SYS_close, fd, 0, 0);
                log_int("LIRO ko size=", n);
                /* The module takes the firmware path/address as module
                 * parameters; without them its init returns -ENOENT from
                 * filp_open("").  This is the A7M3 kernel-context path:
                 * liro.ko loads av-cam.bin itself and calls the entries. */
                {
                    static const char luargs[] =
                        "fn=/tool/av-cam.bin adr=0x035c6000 load=0 debug=1 "
                        "liro_resume_async=1 resume_cpu=1";
                    /* ARM EABI: __NR_init_module = 128 (105 is getitimer). */
                    log_hex("LIRO mod=",
                            (unsigned long)sys3(128, (long)kbuf, (long)n,
                                                (long)luargs));
                }
            } else {
                log_str("LIRO ko missing\n");
            }
        }
        if (round == 3) {
            static const char *paths[] = {
                "/proc/liro/susres", "/proc/susres",
                "/proc/liro/start", "/proc/liro", 0
            };
            int pi;
            probe_liro_threads();
            for (pi = 0; paths[pi]; pi++) {
                int f = (int)sys3(SYS_open, (long)paths[pi], 2, 0);
                log_str("LPROC ");
                log_str(paths[pi]);
                log_int(" open=", f);
                if (f >= 0 && same(paths[pi], "/proc/liro/susres")) {
                    /* Keep the suspend path untouched for this experiment:
                     * its write blocks in ter_tsk while the LIRO task set
                     * waits for a CP event. */
                    log_str("LSUS skip\n");
                    sys3(SYS_close, f, 0, 0);
                    continue;
                }
                if (f >= 0) {
                    long w = sys3(SYS_write, f, (long)"1", 1);
                    log_int(" write=", (int)w);
                    sys3(SYS_close, f, 0, 0);
                }
            }
            {
                static char dbuf[4096];
                int dfd = (int)sys3(SYS_open, (long)"/proc/liro", 0, 0);
                log_int("LDIR /proc/liro fd=", dfd);
                if (dfd >= 0) {
                    long n = sys3(217 /*getdents64*/, dfd, (long)dbuf,
                                  (long)sizeof(dbuf));
                    log_int("LDIR n=", (int)n);
                    if (n > 0) {
                        long o = 0;
                        while (o + 19 < n) {
                            unsigned short reclen;
                            char *nm;
                            __builtin_memcpy(&reclen, dbuf + o + 16, 2);
                            nm = dbuf + o + 19;
                            log_str("  DIRENT ");
                            log_str(nm);
                            if (!reclen)
                                break;
                            o += reclen;
                        }
                    }
                    sys3(SYS_close, dfd, 0, 0);
                }
            }
        }
        if (round == 4)
            log_str("ROUND4\n");
        if (round == 5)
            probe_liro_threads();
        /* telnet_probe();  disabled for the pViewManager probe baseline */
        if ((round == 2 || round == 5) &&
            file_exists("/usr/tool/config_kick"))
            config_kick();
        if (round >= 2 && file_exists("/usr/tool/model_kick"))
            model_kick();
        if (round == 5) {
            ui_kick_if_ready();
            /* The real camera maps viewUnified6/7 too; QEMU only gets 2.
             * Load them lazily/global now that libObj+viewUnified2 exist. */
            if (!maps_has("viewUnified6")) {
                void *h6 = dlopen("viewUnified6.so", 0x101);
                log_hex("VU6 h=", (unsigned long)h6);
            }
            if (!maps_has("viewUnified7")) {
                void *h7 = dlopen("viewUnified7.so", 0x101);
                log_hex("VU7 h=", (unsigned long)h7);
            }
            log_hex("MAP vu2=", (unsigned long)maps_has("viewUnified2"));
            log_hex("MAP vu6=", (unsigned long)maps_has("viewUnified6"));
            log_hex("MAP vu7=", (unsigned long)maps_has("viewUnified7"));
            if (file_exists("/usr/tool/view_kick"))
                view_kick();
            if (file_exists("/usr/tool/root_kick"))
                root_kick();
            /* The app has drawn by now (Root Draw happens after Model init);
             * capture its GPU mappings for command-stream decoding. */
            gpu_dump_regions();
        }
        if (draw_seen && !key_sent && file_exists("/usr/tool/uikey")) {
            /* KEY_MENU must land after the first frame: the app's
             * InputService only reacts once the model/renderer is up. */
            if (!ui_kick_ran)
                ui_kick_if_ready();
            if (ui_kick_ran) {
                uinput_key(139 /* KEY_MENU */);
                key_sent = 1;
            }
        }
        if (round == 7 && file_exists("/usr/tool/vukick")) {
            unsigned long vb = module_base("viewUnified2.so");
            log_str("VUKICK base=");
            log_hex("", vb);
            if (vb)
                ((void (*)(void))((vb + 0x379141UL) | 1UL))();
            log_str("VUKICK done\n");
        }
        if (0 && round >= 1) {
            /* probe_viewmgr(); disabled: its log buffer was smearing shim
             * globals on this target. */
        }
    }
}


/* Observe process launches (sen.elf/qsi.elf and friends). */
static int spawn_logs;

int system(const char *cmd)
{
    static int (*real)(const char *);
    if (active && cmd && spawn_logs < 32) {
        spawn_logs++;
        log_str("SPAWN system: ");
        log_str(cmd);
        log_str("\n");
    }
    if (!real && dlsym)
        real = (int (*)(const char *))dlsym(RTLD_NEXT, "system");
    return real ? real(cmd) : -1;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    if (active && spawn_logs < 32) {
        spawn_logs++;
        log_str("SPAWN execve: ");
        log_str(path ? path : "(null)");
        log_str("\n");
    }
    return (int)sys3(11 /* execve */, (long)path, (long)argv, (long)envp);
}

int execv(const char *path, char *const argv[])
{
    extern char **environ;
    return execve(path, argv, environ);
}

typedef int (*playbeep_t)(void *, int);
static playbeep_t real_playbeep;

/* Diagnostics: make the app's abort reason visible on the serial console.
 * im.elf keeps its own log ring (klog/alog), so an assert is otherwise silent. */
typedef void (*assert_fn)(const char *, const char *, unsigned int,
                          const char *);
static assert_fn real_assert_fail;

void __assert_fail(const char *assertion, const char *file, unsigned int line,
                   const char *function)
{
    if (active) {
        log_str("ASSERT: ");
        log_str(assertion ? assertion : "(null)");
        log_str("\n  file=");
        log_str(file ? file : "(null)");
        log_int(" line=", (int)line);
        log_str(" fn=");
        log_str(function ? function : "(null)");
        log_str("\n");
        for (;;)
            ;       /* keep the console alive so the message is readable */
    }
    if (!real_assert_fail && dlsym)
        real_assert_fail = (assert_fn)dlsym(RTLD_NEXT, "__assert_fail");
    if (real_assert_fail)
        real_assert_fail(assertion, file, line, function);
}

typedef void (*abort_fn)(void);
static abort_fn real_abort;

typedef int (*raise_fn)(int);
static raise_fn real_raise;

int raise(int sig)
{
    if (active && sig == 6) {
        log_int("RAISE ignored sig=", sig);
        return 0;
    }
    if (!real_raise && dlsym)
        real_raise = (raise_fn)dlsym(RTLD_NEXT, "raise");
    if (real_raise)
        return real_raise(sig);
    return -1;
}

void abort(void)
{
    if (active) {
        unsigned long lr, sp, lb;
        int i;
        __asm__ volatile ("mov %0, lr" : "=r"(lr));
        __asm__ volatile ("mov %0, sp" : "=r"(sp));
        lb = module_base("libObj.so");
        log_str("ABORT called\n");
        log_hex(" ABORT_LR=", lr);
        log_hex(" ABORT_base=", lb);
        for (i = 0; i < 80; i++) {
            unsigned long v = ((unsigned long *)sp)[i];
            if (lb && v >= lb && v < lb + 0xe00000UL)
                log_hex(" ABORT_STK=", v - lb);
        }
        for (;;)
            sys3(1 /* exit */, 1, 0, 0);
    }
    if (!real_abort && dlsym)
        real_abort = (abort_fn)dlsym(RTLD_NEXT, "abort");
    if (real_abort)
        real_abort();
}


int _ZN15ViewBaseProduct8PlayBeepEi(void *self, int id)
{
    playbeep_t real = real_playbeep;

    if (!real && dlsym) {
        real = (playbeep_t)dlsym(RTLD_NEXT, "_ZN15ViewBaseProduct8PlayBeepEi");
        real_playbeep = real;
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

    if (!real && dlsym) {
        real = (model_exec_t)dlsym(RTLD_NEXT,
                                   "_ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList");
        real_model_exec = real;
    }
    if (active) {
        log_str("ModelExecute cmd=");
        log_str(cmd ? cmd : "(null)");
        log_int(" n=", n);
        log_hex(" params=", (unsigned long)params);
        if (!real)
            log_str("ModelExecute no-forward\n");
    }
    if (real)
        return real(self, cmd, n, params);
    return 0;
}
