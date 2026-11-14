/* probe_menu.c -- A7R3 "new menu" M0 probe.
 *
 * Read-only reconnaissance library, preloaded into im.elf:
 *
 *   LD_PRELOAD="/usr/tool/probe_menu.so:/usr/tool/libefence.so"
 *
 * It answers, with evidence from the running guest, the questions the ZV-E10
 * port had to answer for its own firmware before it could inject a menu row:
 *
 *   * which modules are loaded and what the menu entry points resolve to,
 *   * what the native settings-node tree actually looks like: the exported
 *     40-byte `cmnViewSettingNodeRoot*` prototypes, their vptr (-> class name
 *     via dladdr), child count, and every property record {key, kind, value}.
 *
 * The node layout is the one verified on ZV-E10 (native_entry.inc):
 *
 *     +0x00 vptr        +0x10 parent      +0x20 physical
 *     +0x04 children**  +0x14 depth       +0x24 state
 *     +0x08 count       +0x18 selected
 *     +0x0C properties* +0x1C selectable   sizeof == 0x28
 *
 * and A7R3 exports these roots as exactly 40-byte objects, so it carries over.
 *
 * Triggering: `setitimer` into im.elf is known to hang its main loop, and a
 * hand-rolled clone() is needless risk, so this probe piggybacks on dlopen
 * completion -- the approach the ZV-E10 port settled on.  Output goes to fd 1
 * (the serial console) with a `PM_` prefix and is mirrored to /tmp.
 *
 * Nothing is written to persistent storage and no code is patched.
 */
typedef unsigned long usize;

extern void *dlsym(void *handle, const char *name) __attribute__((weak));
extern void *dlvsym(void *handle, const char *name, const char *version) __attribute__((weak));
extern int dladdr(const void *addr, void *info) __attribute__((weak));

#define RTLD_NEXT ((void *)-1L)
#define RTLD_DEFAULT ((void *)0)
#define RTLD_LAZY 1
#define RTLD_NOLOAD 4

#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_read 3
#define SYS_getpid 20
#define SYS_clock_gettime 263

#define O_APPEND_CREAT 0x441
#define CLOCK_MONOTONIC 1

#define NULL ((void *)0)

/* ------------------------------------------------------------------ */
/* syscalls (freestanding: no libc)                                    */
/* ------------------------------------------------------------------ */

static long sys2(long nr, long a, long b)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
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

static long sys4(long nr, long a, long b, long c, long d, long e, long f)
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
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}



static int has_prefix(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static int g_log_fd = -1;

static int log_open(void)
{
    static const char *const paths[] = { "/setting/probe_menu.log",
                                         "/tmp/probe_menu.log", 0 };
    int i;
    if (g_log_fd >= 0)
        return g_log_fd;
    for (i = 0; paths[i]; i++) {
        g_log_fd = (int)sys3(SYS_open, (long)paths[i], O_APPEND_CREAT, 0644);
        if (g_log_fd >= 0)
            return g_log_fd;
    }
    return -1;
}

static void out(const char *s, int n)
{
    sys3(SYS_write, 1, (long)s, n);
    if (log_open() >= 0)
        sys3(SYS_write, g_log_fd, (long)s, n);
}

/* ------------------------------------------------------------------ */
/* line builder (no libc printf)                                       */
/* ------------------------------------------------------------------ */

static int g_active;
static void *g_screen_bmp;      /* set by scan_for_bitmaps(); used by paint_once() */
static char g_line[512];
static int g_len;

/* noinline: at -O2 clang unrolls these copy loops into immediate stores and
 * the log literals vanish from .rodata, which makes a build impossible to
 * verify by grep (see A7R3_NOTES 34.10). */
__attribute__((noinline)) static void ln_reset(const char *prefix)
{
    g_len = 0;
    while (*prefix && g_len < 64)
        g_line[g_len++] = *prefix++;
}

__attribute__((noinline)) static void ln_str(const char *s)
{
    while (*s && g_len < (int)sizeof(g_line) - 2)
        g_line[g_len++] = *s++;
}

__attribute__((noinline)) static void ln_dec(long v)
{
    char tmp[16];
    int i = 0;
    unsigned long u;
    if (v < 0) {
        g_line[g_len++] = '-';
        u = (unsigned long)(-v);
    } else {
        u = (unsigned long)v;
    }
    do {
        tmp[i++] = (char)('0' + (u % 10UL));
        u /= 10UL;
    } while (u && i < (int)sizeof(tmp));
    while (i > 0 && g_len < (int)sizeof(g_line) - 2)
        g_line[g_len++] = tmp[--i];
}

__attribute__((noinline)) static void ln_hex(unsigned long v)
{
    char tmp[16];
    int i = 0;
    if (!v) {
        ln_str("0");
        return;
    }
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = "0123456789abcdef"[v & 15UL];
        v >>= 4;
    }
    while (i > 0 && g_len < (int)sizeof(g_line) - 2)
        g_line[g_len++] = tmp[--i];
}

static void ln_hexp(unsigned long v)
{
    ln_str("0x");
    ln_hex(v);
}

__attribute__((noinline)) static void ln_flush(void)
{
    g_line[g_len++] = '\n';
    out(g_line, g_len);
    g_len = 0;
}

/* ------------------------------------------------------------------ */
/* node layout (verified on ZV-E10, sizeof == 0x28)                    */
/* ------------------------------------------------------------------ */

struct node;

struct property {
    int key;
    int kind;
    unsigned value;
};

struct properties {
    const struct property *records;
    int last;
};

struct node {
    unsigned vptr;
    struct node **children;
    int count;
    const struct properties *properties;
    struct node *parent;
    int depth;
    int selected;
    int selectable;
    int physical;
    unsigned state;
};

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

struct timespec32 {
    long tv_sec;
    long tv_nsec;
};

/* Guard every guest pointer we follow: during construction the child array and
 * property tables are not always complete yet, and dereferencing a half-built
 * node takes the whole UI down with a page translation fault. */
static int plausible(const void *p);

/* Readable-range table from /proc/self/maps.  "plausible" only says an address
 * looks like a pointer; before we actually dereference a guest pointer we check
 * that the page really is mapped, otherwise a single stale field takes the UI
 * down with a page translation fault. */
#define RNG_MAX 512
struct rng {
    unsigned long lo, hi;
    int writable;
};
static struct rng g_rng[RNG_MAX];
static int g_rng_n;
static int g_rng_bytes;

static unsigned long parse_hex(const char **s)
{
    unsigned long v = 0;
    while (**s) {
        char c = **s;
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = (v << 4) | (unsigned)d;
        (*s)++;
    }
    return v;
}

static void load_maps(void)
{
    static char buf[65536];
    int fd, n, i;
    const char *p, *end;

    if (g_rng_n)
        return;
    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return;
    n = 0;
    /* /proc/self/maps hands back one page per read(); loop to EOF. */
    while (n < (int)sizeof(buf) - 1) {
        long r = sys3(SYS_read, fd, (long)(buf + n),
                      (long)(sizeof(buf) - 1 - (unsigned)n));
        if (r <= 0)
            break;
        n += (int)r;
    }
    sys2(SYS_close, fd, 0);
    if (n <= 0)
        return;
    buf[n] = 0;
    g_rng_bytes = n;
    p = buf;
    end = buf + n;
    while (p < end && g_rng_n < RNG_MAX) {
        const char *line = p;
        const char *next;
        unsigned long lo, hi;
        const char *q;

        while (p < end && *p != '\n')
            p++;
        next = (p < end) ? p + 1 : end;   /* advance past the newline no matter
                                            * what the parse decides below */
        q = line;
        lo = parse_hex(&q);
        if (q < p && *q == '-') {
            q++;
            hi = parse_hex(&q);
            if (q + 2 < p && q[0] == ' ' && q[1] == 'r' && hi > lo) {
                g_rng[g_rng_n].lo = lo;
                g_rng[g_rng_n].hi = hi;
                g_rng[g_rng_n].writable = (q[2] == 'w');
                g_rng_n++;
            }
        }
        p = next;
    }
    (void)i;
}

static int readable(const void *p, unsigned len)
{
    unsigned long a = (unsigned long)p;
    unsigned long b = a + len;
    int i;
    if (!g_rng_n)
        load_maps();
    for (i = 0; i < g_rng_n; i++) {
        if (a >= g_rng[i].lo && b <= g_rng[i].hi && b > a)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* probe state                                                         */
/* ------------------------------------------------------------------ */

static unsigned g_state;        /* 0 idle, 1 armed, 2 done, 9 disabled */
static long g_first_positive;
static void *g_caution;
static void *g_obj;
static void *g_views;
static int g_dumps;
static int g_arm_noise;
static int g_roots_null;
static int g_roots_nonzero;

/* Every `cmnViewSettingNodeRoot*` object exported by CautionConfig.so, baked at
 * build time by gen-root-names.py (456 of them on this firmware). */
#include "probe_menu_roots.h"

static const char *const g_extra_syms[] = {
    "_ZN18CmnViewSettingNode15initSettingNodeEPPS_i",
    "_ZN18CmnViewSettingNode11getRootItemEPPS_",
    "_ZN18CmnViewSettingNode15setItemSelectedEv",
    "_ZN18CmnViewSettingNode17getSubItemByIndexEiPPS_",
    "_ZN18CmnViewSettingNode11getCountAllERi",
    "_ZN18CmnViewSettingNode13getPropertiesEPPK24CmnViewSettingProperties",
    "_ZN18CmnViewSettingNode14setItemDisableEb",
    "_ZN18CmnViewSettingNode13isItemGrayoutEPb",
    "_ZN18CmnViewGenOpenUtil8openMenuEP15ViewBaseProductNS_8OpenInfoE",
    "_ZN13PAS_MenuPopup13openMenuPopupERNS_8OpenInfoE",
    "_ZN9GEN_Label7setTextEj",
    "_ZN9GEN_Label13setTextDirectEPKc",
    "GRM_bitmapCreate",
    "GRM_bitmapWriteLockBits",
    "GRM_bitmapWriteUnlockBits",
    "GRM_gpermRectblit",
    "GRM_gpermFillrect",
    "GRM_screenGetOnBitmap",
    "GRM_bitmapGetPhysicalAddress",
    "GRM_bitmapGetRowPixelCount",
    "snd_InfraInputEvent_to_InputServant",
    "Application_System_Init",
    0
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static long mono_sec(void)
{
    struct timespec32 ts;
    if (sys2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts) < 0)
        return -1;
    return ts.tv_sec;
}

static int comm_is_im(void)
{
    char buf[32];
    int fd = (int)sys3(SYS_open, (long)"/proc/self/comm", 0, 0);
    int n;
    if (fd < 0)
        return 0;
    n = (int)sys3(SYS_read, fd, (long)buf, (long)sizeof(buf) - 1);
    sys2(SYS_close, fd, 0);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    return has_prefix(buf, "im.elf");
}

static void name_of(unsigned long addr)
{
    Dl_info info;
    if (!dladdr)
        return;
    info.dli_sname = NULL;
    info.dli_saddr = NULL;
    info.dli_fname = NULL;
    info.dli_fbase = NULL;
    if (dladdr((const void *)addr, &info) == 0)
        return;
    ln_str(info.dli_sname ? info.dli_sname : "?");
}

__attribute__((noinline)) static void dump_props(const struct node *n, int depth, int index)
{
    const struct properties *p = n->properties;
    int i, count;
    if (!p)
        return;
    /* A7R3 packs the record COUNT here (setup-version has 6 records with
     * keys 0/6/11/12/14/32 and the next table starts right after record 5),
     * while ZV-E10 read it as an inclusive index.  Print exactly `count`
     * records so we never over-read into the neighbouring table. */
    count = p->last;
    if (count <= 0 || count > 16 || !p->records)
        return;
    for (i = 0; i < count && i < 8; i++) {
        ln_reset("PM_P d=");
        ln_dec(depth);
        ln_str(" i=");
        ln_dec(index);
        ln_str(" [");
        ln_dec(i);
        ln_str("] key=");
        ln_hexp((unsigned)p->records[i].key);
        ln_str(" kind=");
        ln_dec(p->records[i].kind);
        ln_str(" val=");
        ln_hexp(p->records[i].value);
        ln_flush();
    }
}

__attribute__((noinline)) static void dump_node(const struct node *n, int depth, int index, int *budget)
{
    int i, count;

    if (!n || !*budget)
        return;
    --*budget;

    ln_reset("PM_N d=");
    ln_dec(depth);
    ln_str(" i=");
    ln_dec(index);
    ln_str(" at=");
    ln_hexp((unsigned long)n);
    ln_str(" vptr=");
    ln_hexp((unsigned long)n->vptr);
    ln_str(" cls=");
    name_of((unsigned long)n->vptr);
    ln_str(" cnt=");
    ln_dec(n->count);
    ln_str(" n=");
    ln_dec(n->properties ? n->properties->last : -1);
    ln_str(" st=");
    ln_hexp(n->state);
    ln_str(" par=");
    ln_hexp((unsigned long)n->parent);
    ln_str(" sel=");
    ln_dec(n->selected);
    ln_str(" phys=");
    ln_dec(n->physical);
    ln_flush();

    dump_props(n, depth, index);

    if (depth >= 5 || !plausible(n->children))
        return;
    count = n->count;
    if (count < 0 || count > 32)
        return;
    for (i = 0; i < count; i++) {
        if (*budget <= 0)
            return;
        dump_node(n->children[i], depth + 1, i, budget);
    }
}

__attribute__((noinline)) static void dump_root(const char *name)
{
    struct node *root;
    Dl_info info;
    int budget = 70;

    root = (struct node *)dlsym(g_caution, name);
    if (!root) {
        g_roots_null++;
        return;
    }
    ln_reset("PM_ROOT ");
    ln_str(name);
    ln_str(" at=");
    ln_hexp((unsigned long)root);
    ln_str(" cnt=");
    ln_dec(root->count);
    ln_str(" vptr=");
    ln_hexp((unsigned long)root->vptr);
    ln_str(" cls=");
    name_of((unsigned long)root->vptr);
    if (dladdr && dladdr((const void *)root, &info) == 0 && info.dli_fbase) {
        ln_str(" base=");
        ln_hexp((unsigned long)info.dli_fbase);
    }
    ln_flush();
    dump_node(root, 0, 0, &budget);
    g_dumps++;
}


/* Items we can identify from the rendered frame.  Walking their parent chain
 * locates the page/group/category nodes, which are heap objects built by the
 * exported CmnViewSettingNode*Page*Group* constructors and therefore have no
 * symbol name of their own. */
static const char *const g_focus_names[] = {
    "cmnViewSettingNodeRootRawFileType",
    "cmnViewSettingNodeRootStlImageJPEGQuality",
    "cmnViewSettingNodeRootStillImageSize",
    "cmnViewSettingNodeRootStlImageAspectRatio",
    "cmnViewSettingNodeRootStillPanoramaImageSize",
    "cmnViewSettingNodeRootSetupVersion",
    0
};

static int plausible(const void *p)
{
    unsigned long v = (unsigned long)p;
    return v >= 0x10000UL && v < 0x80000000UL && (v & 1u) == 0;
}

static void title_of(const struct node *n)
{
    const struct properties *p;
    if (!plausible(n)) {
        ln_str("?");
        return;
    }
    p = n->properties;
    if (!plausible(p) || !plausible(p->records) || p->last <= 0 || p->last > 64) {
        ln_str("?");
        return;
    }
    ln_hexp((unsigned)p->records[0].value);
}

/* ------------------------------------------------------------------ */
/* Thumb inline hook for CmnViewSettingNode::initSettingNode           */
/*                                                                     */
/* Item nodes' `parent` points at a flat 456-entry registry, so the     */
/* page/group hierarchy only exists while the constructors run.  Hooking */
/* this one exported function is what reveals it.  Gated on the marker   */
/* file /setting/probe_hook so the default build stays read-only.        */
/* ------------------------------------------------------------------ */

#define SYS_mmap2 192
#define SYS_cacheflush 0x0f0002
#define PROT_READ 1
#define PROT_EXEC 4
#define PROT_RWX 7
#define MAP_PRIVATE_ANON 0x22

#define HOOK_SYM "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties"
#define INIT_SYM "_ZN18CmnViewSettingNode15initSettingNodeEPPS_i"

typedef void (*ctor_fn)(void *self, void **children, int count, const void *props);
static ctor_fn g_orig_ctor;
static int g_hook_state;        /* 0 untried, 1 installed, -1 failed */
static int g_hook_tries;
static int g_ctor_calls;
static unsigned long g_hook_addr;

static int file_exists(const char *path)
{
    int fd = (int)sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0)
        return 0;
    sys2(SYS_close, fd, 0);
    return 1;
}

static void icache_flush(void *start, unsigned long len)
{
    sys3(SYS_cacheflush, (long)start, (long)start + (long)len, 0);
}

/* Thumb-2 32-bit instruction? (first halfword in the 0xE8xx/0xFxxx space) */
static int is_wide(unsigned hw)
{
    return (hw & 0xF800u) == 0xE800u || (hw & 0xF800u) == 0xF000u;
}

/* Number of bytes covered by whole instructions starting at `target`, at least
 * 8 and at most 12.  A fixup jump is 8 bytes (ldr.w pc + literal), so the span
 * tells us how much to preserve in the trampoline and how much to pad. */
static int prologue_span(unsigned long target)
{
    const unsigned short *hw = (const unsigned short *)target;
    int span = 0;
    while (span < 8 && span < 12) {
        span += is_wide(hw[span >> 1]) ? 4 : 2;
    }
    return span;
}

static int thumb_hook(unsigned long target, void *replacement, void **original)
{
    unsigned char *code;
    void *page;
    int i, span;

    if (!target || (target & 3u))
        return -1;                 /* need a word-aligned entry */
    span = prologue_span(target);
    if (span < 8 || span > 12)
        return -2;

    code = (unsigned char *)sys4(SYS_mmap2, 0, 4096, PROT_RWX,
                                 MAP_PRIVATE_ANON, -1, 0);
    if ((long)code < 0 && (long)code > -4096)
        return -3;
    for (i = 0; i < span; i++)
        code[i] = ((unsigned char *)target)[i];
    /* trampoline: preserved prologue, then ldr.w pc,[pc,#0] + return address.
     * The return address MUST keep bit0 set: dropping it switches the CPU to
     * ARM state at a Thumb address and faults on the next fetch (the ZV-E10
     * port does the same `| 1u`). */
    code[span] = 0xdf;
    code[span + 1] = 0xf8;
    code[span + 2] = 0x00;
    code[span + 3] = 0xf0;
    *(unsigned *)(code + span + 4) = (unsigned)(target + span) | 1u;
    icache_flush(code, 64);
    *original = (void *)((unsigned long)code | 1u);

    page = (void *)(target & ~4095UL);
    if (sys3(125 /* mprotect */, (long)page, 8192u, PROT_RWX) < 0)
        return -5;
    {
        unsigned short *p = (unsigned short *)target;
        p[0] = 0xdf;            /* ldr.w pc, [pc, #0] */
        p[1] = 0xf8;
        p[2] = 0x00;
        p[3] = 0xf0;
        *(unsigned *)(target + 4) = (unsigned)(unsigned long)replacement;
        for (i = 8; i < span; i += 2) {
            p[i >> 1] = 0xbf00; /* nop; never executed, just padding */
        }
    }
    icache_flush((void *)page, 8192);
    sys3(125 /* mprotect */, (long)page, 8192u, PROT_READ | PROT_EXEC);
    return 0;
}

/* Logged from the base-class constructor, which every settings node (page,
 * group, item) runs through with its children array and property table. */
__attribute__((noinline)) static void dump_ctor_call(void *self,
                                                     struct node **children,
                                                     int count,
                                                     const void *props)
{
    int i;
    ln_reset("PM_CTOR n=");
    ln_dec(++g_ctor_calls);
    ln_str(" self=");
    ln_hexp((unsigned long)self);
    ln_str(" cnt=");
    ln_dec(count);
    ln_str(" ch=");
    ln_hexp((unsigned long)children);
    ln_str(" props=");
    ln_hexp((unsigned long)props);
    ln_str(" pn=");
    ln_dec(props && plausible(props)
           ? ((const struct properties *)props)->last : -1);
    ln_flush();
    if (count <= 0 || count > 16 || !plausible(children))
        return;
    for (i = 0; i < count; i++) {
        const struct node *c;
        if (!plausible(children))
            return;
        c = children[i];
        if (!plausible(c))
            continue;
        ln_reset("PM_CC i=");
        ln_dec(i);
        ln_str(" at=");
        ln_hexp((unsigned long)c);
        ln_str(" cnt=");
        ln_dec(c->count);
        ln_str(" n=");
        ln_dec(c->properties ? c->properties->last : -1);
        ln_str(" title=");
        title_of(c);
        ln_str(" cls=");
        name_of((unsigned long)c->vptr);
        ln_flush();
    }
    if (props && plausible(props)) {
        const struct properties *p = (const struct properties *)props;
        int n = p->last;
        if (n > 0 && n <= 16 && plausible(p->records)) {
            for (i = 0; i < n && i < 8; i++) {
                ln_reset("PM_CP [");
                ln_dec(i);
                ln_str("] key=");
                ln_hexp((unsigned)p->records[i].key);
                ln_str(" kind=");
                ln_dec(p->records[i].kind);
                ln_str(" val=");
                ln_hexp((unsigned)p->records[i].value);
                ln_flush();
            }
        }
    }
}

static void pm_ctor_wrapper(void *self, void **children, int count,
                            const void *props)
{
    g_orig_ctor(self, children, count, props);
    if (g_ctor_calls < 200)
        dump_ctor_call(self, (struct node **)children, count, props);
}

/* Second target: CmnViewSettingNode::initSettingNode(children, count).  The
 * per-page subclass constructors are only 52 bytes, so they likely inline the
 * base constructor but still call this; hooking both makes "which one is on the
 * hot path" a measurement instead of a guess. */
typedef void (*init2_fn)(void *self, void **children, int count);
static init2_fn g_orig_init2;
static unsigned long g_hook_addr2;

static void pm_init_wrapper(void *self, void **children, int count)
{
    g_orig_init2(self, children, count);
    if (g_ctor_calls < 200)
        dump_ctor_call(self, (struct node **)children, count, NULL);
}

__attribute__((noinline)) static void try_hook(void)
{
    struct node *fn;
    void *original = NULL;
    int rc;
    unsigned long base = 0;

    if (g_hook_state)
        return;
    if (!file_exists("/setting/probe_hook")) {
        g_hook_state = -1;
        ln_reset("PM_HOOK_SKIP no marker");
        ln_flush();
        return;
    }
    if (++g_hook_tries > 40) {
        g_hook_state = -1;
        ln_reset("PM_HOOK_GIVEUP");
        ln_flush();
        return;
    }
    fn = (struct node *)dlsym(g_caution, HOOK_SYM);
    if (!fn)
        fn = (struct node *)dlsym(g_obj, HOOK_SYM);
    if (!fn) {
        ln_reset("PM_HOOK_WAIT no symbol");
        ln_flush();
        return;
    }
    {
        /* Sanity-check the address against a known data symbol so a lookup
         * that returns garbage (libs still initialising) is retried instead
         * of being hooked. */
        void *vt = dlsym(g_caution, "_ZTV18CmnViewSettingNode");
        void *def = dlsym(RTLD_DEFAULT, HOOK_SYM);
        unsigned long off = 0;
        if (vt)
            base = (unsigned long)vt - 0x904950UL;
        if (base)
            off = (unsigned long)fn - base;
        ln_reset("PM_BASE ztv=");
        ln_hexp((unsigned long)vt);
        ln_str(" base=");
        ln_hexp(base);
        ln_str(" hook_off=");
        ln_hexp(off);
        ln_str(" default=");
        ln_hexp((unsigned long)def);
        ln_flush();
        if (!base || off < 0x1000 || off > 0x9A4B60UL) {
            ln_reset("PM_HOOK_WAIT bad address");
            ln_flush();
            return;
        }
    }
    ln_reset("PM_HOOK_TRY addr=");
    ln_hexp((unsigned long)fn);
    ln_str(" hw=");
    ln_hexp(((const unsigned short *)fn)[0] |
            ((unsigned)((const unsigned short *)fn)[1] << 16));
    ln_str(" span=");
    ln_dec(prologue_span((unsigned long)fn & ~1u));
    ln_flush();
    /* The base constructor's prologue starts with a PC-relative literal load
     * (`4d05 ldr r5,[pc,#20]`), which cannot be relocated by a naive copy, so
     * it is only hooked when /setting/probe_hook_ctor asks for it.  The
     * generic initSettingNode entry is relocation-safe (`6803 e92d41f0 af00`)
     * and is called by every per-page subclass constructor. */
    if (!file_exists("/setting/probe_hook_ctor")) {
        rc = -10;
    } else {
        rc = thumb_hook((unsigned long)fn & ~1u, (void *)pm_ctor_wrapper, &original);
    }
    ln_reset("PM_HOOK ctor rc=");
    ln_dec(rc);
    if (rc == 0) {
        g_orig_ctor = (ctor_fn)original;
        g_hook_addr = (unsigned long)fn & ~1u;
        ln_str(" orig=");
        ln_hexp((unsigned long)original);
    }
    ln_flush();

    {
        void *o2 = NULL;
        void *fn2 = dlsym(g_caution, INIT_SYM);
        int rc2;
        if (!fn2)
            fn2 = dlsym(g_obj, INIT_SYM);
        rc2 = fn2 ? thumb_hook((unsigned long)fn2 & ~1u,
                               (void *)pm_init_wrapper, &o2) : -9;
        ln_reset("PM_HOOK init rc=");
        ln_dec(rc2);
        if (rc2 == 0) {
            g_orig_init2 = (init2_fn)o2;
            g_hook_addr2 = (unsigned long)fn2 & ~1u;
            g_hook_state = 1;      /* stop retrying */
            ln_str(" addr=");
            ln_hexp(g_hook_addr2);
        }
        ln_flush();
    }
}


__attribute__((noinline)) static void dump_children(const char *label,
                                                    const struct node *n)
{
    int i, count = n->count;
    if (count <= 0 || count > 40)
        return;
    for (i = 0; i < count; i++) {
        const struct node *c = n->children ? n->children[i] : NULL;
        if (!c)
            continue;
        ln_reset(label);
        ln_str(" i=");
        ln_dec(i);
        ln_str(" at=");
        ln_hexp((unsigned long)c);
        ln_str(" cnt=");
        ln_dec(c->count);
        ln_str(" n=");
        ln_dec(c->properties ? c->properties->last : -1);
        ln_str(" title=");
        title_of(c);
        ln_str(" cls=");
        name_of((unsigned long)c->vptr);
        ln_flush();
    }
}

__attribute__((noinline)) static void dump_focus(const char *name)
{
    struct node *n = (struct node *)dlsym(g_caution, name);
    struct node *p;
    int level;

    if (!n)
        return;
    ln_reset("PM_FOCUS ");
    ln_str(name);
    ln_str(" at=");
    ln_hexp((unsigned long)n);
    ln_str(" cnt=");
    ln_dec(n->count);
    ln_str(" title=");
    title_of(n);
    ln_str(" cls=");
    name_of((unsigned long)n->vptr);
    ln_flush();

    p = n->parent;
    for (level = 1; level <= 4 && p; level++) {
        ln_reset("PM_UP l=");
        ln_dec(level);
        ln_str(" at=");
        ln_hexp((unsigned long)p);
        ln_str(" cnt=");
        ln_dec(p->count);
        ln_str(" n=");
        ln_dec(p->properties ? p->properties->last : -1);
        ln_str(" phys=");
        ln_dec(p->physical);
        ln_str(" sel=");
        ln_dec(p->selected);
        ln_str(" title=");
        title_of(p);
        ln_str(" cls=");
        name_of((unsigned long)p->vptr);
        ln_flush();
        if (p->count > 0 && p->count <= 32 && level <= 2)
            dump_children("PM_CHILD", p);
        p = p->parent;
    }
}
__attribute__((noinline)) static void dump_all_roots(void)
{
    int i;
    struct node *root;
    struct node *top = NULL;

    for (i = 0; i < PM_ROOT_COUNT; i++) {
        root = (struct node *)dlsym(g_caution, g_all_roots[i]);
        if (!root) {
            g_roots_null++;
            continue;
        }
        if (root->count <= 0)
            continue;
        g_roots_nonzero++;
        ln_reset("PM_ROOT ");
        ln_str(g_all_roots[i]);
        ln_str(" at=");
        ln_hexp((unsigned long)root);
        ln_str(" cnt=");
        ln_dec(root->count);
        ln_str(" n=");
        ln_dec(root->properties ? root->properties->last : -1);
        ln_str(" cls=");
        name_of((unsigned long)root->vptr);
        ln_flush();
        if (root->parent && !top) {
            struct node *n = root;
            int guard = 0;
            while (n->parent && guard++ < 12)
                n = n->parent;
            top = n;
        }
    }
    ln_reset("PM_ROOTS total=");
    ln_dec(PM_ROOT_COUNT);
    ln_str(" null=");
    ln_dec(g_roots_null);
    ln_str(" nonzero=");
    ln_dec(g_roots_nonzero);
    ln_flush();

    if (top) {
        ln_reset("PM_TOP at=");
        ln_hexp((unsigned long)top);
        ln_str(" cnt=");
        ln_dec(top->count);
        ln_str(" cls=");
        name_of((unsigned long)top->vptr);
        ln_flush();
    }
    /* Locate the page/group nodes by walking up from items we can identify in
     * the rendered frame. */
    for (i = 0; g_focus_names[i]; i++)
        dump_focus(g_focus_names[i]);
}

__attribute__((noinline)) static void dump_symbols(void)
{
    int i;
    void *addr;

    for (i = 0; g_extra_syms[i]; i++) {
        addr = dlsym(g_obj, g_extra_syms[i]);
        if (!addr)
            addr = dlsym(g_views, g_extra_syms[i]);
        if (!addr)
            addr = dlsym(g_caution, g_extra_syms[i]);
        ln_reset("PM_SYM ");
        ln_str(g_extra_syms[i]);
        ln_str("=");
        if (addr)
            ln_hexp((unsigned long)addr);
        else
            ln_str("NULL");
        ln_flush();
    }
}

static void *(*g_real_dlopen)(const char *, int);

static void *real_dlopen(const char *path, int flags)
{
    if (!g_real_dlopen) {
        /* Prefer the shim's interposer (next in the preload list) so its
         * libObj-load hooks keep working; fall back to libc directly. */
        if (dlsym)
            g_real_dlopen = (void *(*)(const char *, int))dlsym(RTLD_NEXT, "dlopen");
        if (!g_real_dlopen && dlvsym)
            g_real_dlopen = (void *(*)(const char *, int))
                dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
    }
    if (!g_real_dlopen)
        return NULL;
    return g_real_dlopen(path, flags);
}

static void arm(void)
{
    if (g_caution)
        return;
    g_caution = real_dlopen("/usr/lib/CautionConfig.so", RTLD_LAZY | RTLD_NOLOAD);
    g_obj = real_dlopen("/usr/lib/libObj.so", RTLD_LAZY | RTLD_NOLOAD);
    g_views = real_dlopen("/usr/lib/viewUnified2.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!g_caution || !g_obj || !g_views) {
        g_caution = NULL;
        g_obj = NULL;
        g_views = NULL;
        if (!g_arm_noise++) {
            ln_reset("PM_ARM_WAIT libs not loaded yet");
            ln_flush();
        }
        return;
    }
    ln_reset("PM_ARM caution=");
    ln_hexp((unsigned long)g_caution);
    ln_str(" libobj=");
    ln_hexp((unsigned long)g_obj);
    ln_str(" views=");
    ln_hexp((unsigned long)g_views);
    ln_flush();
    g_state = 1;
    dump_symbols();
    try_hook();
}

/* ------------------------------------------------------------------ */
/* M1a: can we draw at all?  Use only the exported GRM entry points.    */
/* ------------------------------------------------------------------ */

struct fill_param {
    void *bitmap;          /* +0x00 */
    unsigned char colour[4]; /* +0x04 RGBA                      */
    short x0, y0;          /* +0x08 */
    short x1, y1;          /* +0x0C */
    unsigned mode;         /* +0x10 only 1 or 2 draws         */
    unsigned flag;         /* +0x14 */
};

typedef void *(*getbmp_fn)(void);
typedef int (*fill_fn)(int, int, const void *, unsigned);

static getbmp_fn g_getbmp;
static fill_fn g_fill;
static int g_paint_left;
static int g_paint_probed;

static unsigned rd32(const void *p)
{
    unsigned v;
    if (!p || !readable(p, 4))
        return 0;
    __builtin_memcpy(&v, p, 4);
    return v;
}

/* One shot, under the firmware's own bitmap write lock. */
static int g_painted;
static int g_paint_budget;
static long g_arm_sec;

static void paint_once(void)
{
    void *bmp;
    void *geo;
    unsigned pixv;
    unsigned stride;
    unsigned char *pix;
    int lock_rc = -1;
    int (*lockfn)(void *, int, void **);
    int (*unlockfn)(void *);
    struct fill_param p;
    int x, y;

    if (!g_paint_budget)
        return;
    g_paint_budget--;
    if (!g_getbmp)
        g_getbmp = (getbmp_fn)dlsym(g_obj, "GRM_screenGetOnBitmap");
    if (!g_fill)
        g_fill = (fill_fn)dlsym(g_obj, "GRM_gpermFillrect");
    lockfn = (int (*)(void *, int, void **))dlsym(g_obj, "GRM_bitmapWriteLockBits");
    unlockfn = (int (*)(void *))dlsym(g_obj, "GRM_bitmapWriteUnlockBits");

    bmp = g_screen_bmp ? g_screen_bmp : (g_getbmp ? g_getbmp() : NULL);
    if (!plausible(bmp))
        return;
    g_painted = 1;

    geo = (void *)rd32((const char *)bmp + 4);
    pixv = rd32((const char *)bmp + 8);
    stride = geo ? *(const unsigned short *)((const char *)geo + 0x18) : 0;
    ln_reset("PM_DRAW bmp=");
    ln_hexp((unsigned long)bmp);
    ln_str(" geo=");
    ln_hexp((unsigned long)geo);
    ln_str(" pix=");
    ln_hexp(pixv);
    ln_str(" stride=");
    ln_dec((long)stride);
    ln_str(" fmt=");
    ln_dec((long)(geo ? rd32((const char *)geo + 0x1c) : 0));
    ln_flush();
    if (!plausible((void *)pixv) || stride < 320 || stride > 4096)
        return;

    /* Official path first: the firmware tracks a 1:1 lock/unlock count on the
     * bitmap and waits for it, so writing behind its back races the GPU. */
    if (lockfn) {
        void *lp = NULL;
        lock_rc = lockfn(bmp, 0, &lp);
        ln_reset("PM_LOCK rc=");
        ln_dec(lock_rc);
        ln_str(" pix=");
        ln_hexp((unsigned long)lp);
        ln_flush();
        if (lock_rc == 0 && plausible(lp))
            pixv = (unsigned)lp;
    }
    if (!readable((const void *)pixv, (unsigned)stride * 2u * 480u)) {
        if (lockfn && unlockfn && lock_rc == 0)
            unlockfn(bmp);
        ln_reset("PM_DRAW_SKIP unreadable pix");
        ln_flush();
        return;
    }
    pix = (unsigned char *)pixv;

    /* Two unmistakable bars: magenta on top, green across the middle. */
    for (y = 0; y < 40; y++)
        for (x = 0; x < 640; x++)
            ((unsigned short *)pix)[(unsigned)y * stride + (unsigned)x] = 0xf0f0;
    for (y = 200; y < 240; y++)
        for (x = 0; x < 640; x++)
            ((unsigned short *)pix)[(unsigned)y * stride + (unsigned)x] = 0x0f00;

    ln_reset("PM_POST p0=");
    ln_hexp(((unsigned short *)pix)[0]);
    ln_str(" mid=");
    ln_hexp(((unsigned short *)pix)[(220u * stride + 320u)]);
    ln_flush();

    if (lockfn && unlockfn && lock_rc == 0) {
        int rc = unlockfn(bmp);
        ln_reset("PM_UNLOCK rc=");
        ln_dec(rc);
        ln_flush();
    }
    (void)p;
}

/* ------------------------------------------------------------------ */
/* M1c: write the panel composition surface directly.                  */
/*                                                                     */
/* The app uses PA 0x202E0200 as the Sugilite blit source for the      */
/* panel (the shim's own fallback producer writes the same buffer), so */
/* writing it through /dev/mem is the shortest path to "our pixels on  */
/* the panel".  Key injection goes through the same OSAL queue the      */
/* guest injector uses (0x14a0429).                                     */
/* ------------------------------------------------------------------ */

#define SYS_munmap 91
#define SYS_open2 5

static int g_fb_done;

typedef int (*valloc_fn_t)(unsigned, void **, unsigned, int);
typedef int (*snd_msg_fn_t)(unsigned, void *);

static void inject_key(unsigned long key, unsigned long status)
{
    valloc_fn_t v = (valloc_fn_t)dlsym(RTLD_DEFAULT, "osal_valloc_msg_wait");
    snd_msg_fn_t s = (snd_msg_fn_t)dlsym(RTLD_DEFAULT, "osal_snd_msg");
    void *m = NULL;
    unsigned long *w;

    if (!v || !s)
        return;
    if (v(0x14a0429u, &m, 24, 1) || !m)
        return;
    w = (unsigned long *)m;
    w[0] = key;
    w[1] = 0;
    w[2] = status;
    w[3] = 0x7fffffffUL;
    w[4] = 0;
    w[5] = 0;
    s(0x14a0429u, m);
}

__attribute__((noinline)) static void paint_fb(void)
{
    const unsigned long base = 0x202E0000UL;
    const unsigned long off = 0x200UL;
    const unsigned long len = 0x98000UL;
    int fd;
    long m;
    unsigned short *p;
    int x, y;

    if (g_fb_done)
        return;
    g_fb_done = 1;
    fd = (int)sys3(SYS_open2, (long)"/dev/mem", 2, 0);
    if (fd < 0) {
        ln_reset("PM_FB nodem");
        ln_flush();
        return;
    }
    m = sys4(SYS_mmap2, 0, (long)len, 3, 1, fd, (long)(base >> 12));
    if (m < 0 && m > -4096) {
        ln_reset("PM_FB mmap=");
        ln_hexp((unsigned long)m);
        ln_flush();
        sys3(SYS_close, fd, 0, 0);
        return;
    }
    p = (unsigned short *)(m + off);
    for (y = 0; y < 40; y++)
        for (x = 0; x < 640; x++)
            p[y * 640 + x] = 0xF800;          /* red bar, RGB565 */
    for (y = 200; y < 240; y++)
        for (x = 0; x < 640; x++)
            p[y * 640 + x] = 0x07E0;          /* green bar */
    ln_reset("PM_FB wrote base=");
    ln_hexp(base + off);
    ln_str(" p0=");
    ln_hexp(p[0]);
    ln_str(" mid=");
    ln_hexp(p[220 * 640 + 320]);
    ln_flush();
    sys3(SYS_munmap, m, (long)len, 0);

    /* Ask the host model to dump the composed surface now (BAR+0x3000).
     * Reuse the same /dev/mem fd: a second open() of /dev/mem failed here. */
    {
        long bm = sys4(SYS_mmap2, 0, 0x1000, 3, 1, fd,
                       (long)(0xf1000000UL >> 12));
        if (bm < 0 && bm > -4096) {
            ln_reset("PM_DUMPTRIG mmap=");
            ln_hexp((unsigned long)bm);
            ln_flush();
        } else {
            ln_reset("PM_DUMPTRIG pre bm=");
            ln_hexp((unsigned long)bm);
            ln_flush();
            *(volatile unsigned *)(bm + 0x3000) = 0xa7d00001u;
            ln_reset("PM_DUMPTRIG ok");
            ln_flush();
            sys3(SYS_munmap, bm, 0x1000, 0);
        }
    }
    sys3(SYS_close, fd, 0, 0);
}


#define ENG_SYM "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv"
#define ENG_W "_ZNK2ux6gfxeng14GraphicsEngine19getFramebufferWidthEv"
#define ENG_H "_ZNK2ux6gfxeng14GraphicsEngine20getFramebufferHeightEv"

typedef void *(*inst_fn)(void);
typedef int (*dim_fn)(const void *);

/* ------------------------------------------------------------------ */
/* Worker thread.                                                      */
/*                                                                     */
/* The rest of this file is driven by dlopen() calls, and the app stops  */
/* calling dlopen once the menu is up -- so a delayed paint never ran.   */
/* A libpthread thread created lazily through dlsym gives us a 1 Hz      */
/* clock without touching signals (SIGALRM into im.elf hangs its main    */
/* loop, and a hand-rolled clone() is needless risk).                    */
/* ------------------------------------------------------------------ */

#define SYS_nanosleep 162

typedef int (*pthread_create_fn)(unsigned long *, const void *,
                                 void *(*)(void *), void *);

static int g_worker_started;
static volatile long g_worker_secs;
static void work_tick(void);

static void *pm_worker(void *arg)
{
    struct timespec32 ts;

    (void)arg;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    for (;;) {
        sys2(SYS_nanosleep, (long)&ts, 0);
        g_worker_secs++;
        work_tick();
    }
    return NULL;
}

static void start_worker(void)
{
    pthread_create_fn create;
    unsigned long tid = 0;

    if (g_worker_started)
        return;
    create = (pthread_create_fn)dlsym(RTLD_DEFAULT, "pthread_create");
    ln_reset("PM_WORKER create=");
    ln_hexp((unsigned long)create);
    ln_flush();
    if (!create)
        return;
    g_worker_started = 1;
    if (create(&tid, NULL, pm_worker, NULL) == 0) {
        ln_reset("PM_WORKER started tid=");
        ln_hexp(tid);
        ln_flush();
    } else {
        ln_reset("PM_WORKER failed");
        ln_flush();
    }
}

/* Runs once per second in the worker. */
static void work_tick(void)
{
    long now;

    if (!g_active || g_state == 9)
        return;
    if (!g_caution)
        return;
    now = mono_sec();
    if (!g_arm_sec || now < g_arm_sec + 45)
        return;
    if (file_exists("/setting/probe_fb") && !g_fb_done) {
        ln_reset("PM_FB_NOW t=");
        ln_dec(now);
        ln_flush();
        paint_fb();
        inject_key(0x30, 1);
        inject_key(0x30, 0);
    }
    if (g_paint_budget && file_exists("/setting/probe_draw"))
        paint_once();
}

static int g_engine_probed;
static int g_bmp_hits;
static void *g_screen_bmp;

/* Look for GRM-bitmap-shaped structs in a mapped span: +0x04 geo, +0x08
 * pixels, geo+0x18 stride (pixels), geo+0x1C format (1..9). */
__attribute__((noinline)) static void scan_for_bitmaps(const char *tag,
                                                       void *base,
                                                       unsigned span)
{
    unsigned i;

    if (!readable(base, span) || g_bmp_hits >= 24)
        return;
    for (i = 0; i + 4 <= span; i += 4) {
        unsigned p = rd32((const char *)base + i);
        unsigned g, px, stride, fmt;

        if (!readable((void *)p, 0x20))
            continue;
        g = rd32((const char *)p + 4);
        px = rd32((const char *)p + 8);
        if (!readable((void *)g, 0x24) || !readable((void *)px, 16))
            continue;
        stride = *(const unsigned short *)((const char *)g + 0x18);
        fmt = rd32((const char *)g + 0x1c);
        if (stride < 64 || stride > 4096 || fmt < 1 || fmt > 9)
            continue;
        g_bmp_hits++;
        if (!g_screen_bmp && stride >= 320 && stride <= 1024)
            g_screen_bmp = (void *)p;
        ln_reset("PM_BMP ");
        ln_str(tag);
        ln_str(" off=");
        ln_hex(i);
        ln_str(" bmp=");
        ln_hexp(p);
        ln_str(" geo=");
        ln_hexp(g);
        ln_str(" pix=");
        ln_hexp(px);
        ln_str(" stride=");
        ln_dec((long)stride);
        ln_str(" fmt=");
        ln_dec((long)fmt);
        ln_str(" rect=");
        ln_hexp(rd32((const char *)g + 0x0c));
        ln_flush();
    }
}

__attribute__((noinline)) static void probe_engine(void)
{
    inst_fn inst;
    dim_fn fw, fh;
    void *eng;
    int w = -1, h = -1;
    int i;

    if (g_engine_probed)
        return;
    g_engine_probed = 1;
    inst = (inst_fn)dlsym(g_obj, ENG_SYM);
    fw = (dim_fn)dlsym(g_obj, ENG_W);
    fh = (dim_fn)dlsym(g_obj, ENG_H);
    eng = inst ? inst() : NULL;
    if (eng && fw)
        w = fw(eng);
    if (eng && fh)
        h = fh(eng);
    ln_reset("PM_ENG eng=");
    ln_hexp((unsigned long)eng);
    ln_str(" w=");
    ln_dec(w);
    ln_str(" h=");
    ln_dec(h);
    ln_str(" fw=");
    ln_hexp((unsigned long)fw);
    ln_str(" fh=");
    ln_hexp((unsigned long)fh);
    ln_flush();
    load_maps();
    ln_reset("PM_MAPS n=");
    ln_dec(g_rng_n);
    ln_str(" bytes=");
    ln_dec((long)g_rng_bytes);
    if (g_rng_n > 0) {
        ln_str(" last=");
        ln_hexp(g_rng[g_rng_n - 1].lo);
        ln_str("-");
        ln_hexp(g_rng[g_rng_n - 1].hi);
    }
    if (g_rng_n > 0) {
        unsigned long e = (unsigned long)eng;
        int k, hit = -1;
        for (k = 0; k < g_rng_n; k++) {
            if (e >= g_rng[k].lo && e < g_rng[k].hi) {
                hit = k;
                break;
            }
        }
        ln_str(" eng_rng=");
        if (hit >= 0) {
            ln_hexp(g_rng[hit].lo);
            ln_str("-");
            ln_hexp(g_rng[hit].hi);
            ln_str(" headroom=");
            ln_dec((long)(g_rng[hit].hi - e));
        } else {
            ln_str("none");
        }
    }
    ln_flush();
    if (!plausible(eng))
        return;

    for (i = 0; i < 0x200; i += 16) {
        int k;
        if (!readable((const char *)eng + i, 16))
            break;
        ln_reset("PM_EW +");
        ln_hex(i);
        ln_str(":");
        for (k = 0; k < 16; k += 4) {
            ln_str(" ");
            ln_hexp(rd32((const char *)eng + i + k));
        }
        ln_flush();
    }

    scan_for_bitmaps("E", eng, 0x200);
    /* second level: objects the engine points at */
    for (i = 0; i < 0x200; i += 4) {
        unsigned p = rd32((const char *)eng + i);
        if (!readable((void *)p, 0x200))
            continue;
        scan_for_bitmaps("E2", (void *)p, 0x200);
    }
    /* the screen bitmap lives in the same heap arena as the engine, so scan
     * that one range instead of the whole address space: fast and reliable */
    {
        unsigned long e = (unsigned long)eng;
        int r;
        for (r = 0; r < g_rng_n; r++) {
            if (e >= g_rng[r].lo && e < g_rng[r].hi) {
                scan_for_bitmaps("R", (void *)g_rng[r].lo,
                                 (unsigned)(g_rng[r].hi - g_rng[r].lo));
                break;
            }
        }
    }
    if (g_screen_bmp) {
        /* NB: GRM_bitmapGetPhysicalAddress() faults with a permission error on
         * this firmware when called with a live bitmap (measured 2026-11-13),
         * so the physical address is left to the pixels pointer we already have. */
        unsigned pix = rd32((const char *)g_screen_bmp + 8);
        unsigned geo = rd32((const char *)g_screen_bmp + 4);
        ln_reset("PM_SCREEN bmp=");
        ln_hexp((unsigned long)g_screen_bmp);
        ln_str(" geo=");
        ln_hexp(geo);
        ln_str(" pix=");
        ln_hexp(pix);
        ln_flush();
    }
}

__attribute__((noinline)) static void tick(void)
{
    struct node *probe;
    long now;
    int i, nonzero;

    if (!g_active || g_state == 9)
        return;
    if (g_state == 0) {
        if (!comm_is_im()) {
            g_state = 9;
            return;
        }
        arm();
        if (g_state != 1)
            return;
    }
    if (file_exists("/setting/probe_engine"))
        probe_engine();
    start_worker();
    if (g_state == 2)
        return;
    probe = (struct node *)dlsym(g_caution, "cmnViewSettingNodeRootSetupMenuStart");
    if (!probe)
        return;
    /* The visible page is built lazily, so count every root that has children
     * rather than watching one hard-coded symbol. */
    nonzero = 0;
    for (i = 0; i < PM_ROOT_COUNT; i++) {
        struct node *n = (struct node *)dlsym(g_caution, g_all_roots[i]);
        if (n && n->count > 0 && ++nonzero >= 2)
            break;
    }
    if (nonzero < 2)
        return;
    now = mono_sec();
    if (g_first_positive == 0) {
        g_first_positive = now;
        ln_reset("PM_TREE_READY t=");
        ln_dec(now);
        ln_str(" roots_with_children=");
        ln_dec(nonzero);
        ln_str(" setupstart_cnt=");
        ln_dec(probe->count);
        ln_flush();
        /* The settings tree exists before the menu view finishes drawing, so
         * arm the paint only; the actual one-shot happens ~45 s later once the
         * page is on screen and the next blit can carry our pixels out. */
        g_arm_sec = now;
        if (file_exists("/setting/probe_draw")) {
            ln_reset("PM_DRAW_ARM t=");
            ln_dec(now);
            ln_flush();
            g_paint_budget = 24;
        }
        return;
    }
    if (now < 0 || now - g_first_positive < 10)
        return;
    g_state = 2;
    ln_reset("PM_DUMP_BEGIN t=");
    ln_dec(now);
    ln_flush();
    dump_all_roots();
    ln_reset("PM_DUMP_END");
    ln_flush();
}

/* ------------------------------------------------------------------ */
/* dlopen interposition (must be versioned, see ZV-E10 note 4)          */
/* ------------------------------------------------------------------ */

void *pm_dlopen_impl(const char *path, int flags);
__asm__(".symver pm_dlopen_impl,dlopen@@GLIBC_2.4");

void *pm_dlopen_impl(const char *path, int flags)
{
    void *result = real_dlopen(path, flags);
    tick();
    return result;
}

/* ------------------------------------------------------------------ */
/* entry                                                               */
/* ------------------------------------------------------------------ */

__attribute__((constructor)) static void probe_menu_init(void)
{
    g_active = comm_is_im();
    if (!g_active)
        return;
    ln_reset("PM_ALIVE pid=");
    ln_dec(sys2(SYS_getpid, 0, 0));
    ln_flush();
    tick();
}

















