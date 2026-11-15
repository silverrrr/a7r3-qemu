/* menu_overlay.c -- new-menu overlay for the A7R3.
 *
 * Original work (MIT).  It links the zve10-derived renderer in menu_shell.c,
 * so builds that include menu_shell.c are noncommercial only
 * (see CREDITS.md and LICENSE).
 */

/* menu_overlay.c -- draw a complete "new menu" onto the A7R3 panel.
 *
 * Renders 640x480 RGBA4444:
 *   - the eight category icons (A7C2 asset set, original_assets.h)
 *   - a header title and a page counter
 *   - six setting rows with real Chinese captions (menu_labels.h, baked from
 *     the firmware's own wording with a host CJK font)
 *   - a selection bar and the footer hint
 *
 * The buffer is packed into the panel's layout and written to PA 0x202E0200
 * through /dev/mem every 200 ms.  Verified 2026-11-14 that pixels written there
 * are the panel content (A7R3_NOTES 44).
 *
 * Gate: /setting/newmenu must exist and the process must be im.elf.
 */
typedef unsigned long usize;
typedef unsigned short u16;
typedef unsigned int u32;

#include <stddef.h>
#include <stdint.h>

#include "original_assets.h"
#include "menu_labels.h"
#include "menu_digits.h"
#include "font_atlas.h"

#define MENU_W 640
#define MENU_H 480

extern void *dlsym(void *handle, const char *name) __attribute__((weak));
extern void *dlopen(const char *path, int flags) __attribute__((weak));

#define RTLD_DEFAULT ((void *)0)

#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_read 3
#define SYS_getpid 20
#define SYS_nanosleep 162
#define SYS_mmap2 192
#define SYS_munmap 91
#define SYS_unlink 10
#define SYS_clock_gettime 263

#define CLOCK_MONOTONIC 1
#define O_APPEND_CREAT 0x441

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

static int g_log = -1;

static int log_open(void)
{
    if (g_log >= 0)
        return g_log;
    g_log = (int)sys3(SYS_open, (long)"/tmp/newmenu.log", O_APPEND_CREAT, 0644);
    return g_log;
}

static void out(const char *s, int n)
{
    /* log file only: writing to the console floods the guest shell's tty and
     * makes /sh output unreadable */
    if (log_open() >= 0) {
        sys3(SYS_write, g_log, (long)s, n);
        return;
    }
    sys3(SYS_write, 1, (long)s, n);
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

static int file_exists(const char *path)
{
    int fd = (int)sys3(SYS_open, (long)path, 0, 0);
    if (fd < 0)
        return 0;
    sys2(SYS_close, fd, 0);
    return 1;
}

static long mono_sec(void)
{
    struct timespec32 ts;
    if (sys2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts) < 0)
        return -1;
    return ts.tv_sec;
}

/* ------------------------------------------------------------------ */
/* drawing (RGBA4444, same blend maths as the ZV-E10 menu_shell)        */
/* ------------------------------------------------------------------ */

static uint16_t g_lin[MENU_W * MENU_H];

struct box {
    int x, y, w, h;
};

static struct box clip(struct box b)
{
    struct box r = { 0, 0, 0, 0 };
    int x1 = b.x + b.w, y1 = b.y + b.h;
    if (b.w <= 0 || b.h <= 0 || x1 <= 0 || y1 <= 0 || b.x >= MENU_W || b.y >= MENU_H)
        return r;
    r.x = b.x < 0 ? 0 : b.x;
    r.y = b.y < 0 ? 0 : b.y;
    r.w = (x1 > MENU_W ? MENU_W : x1) - r.x;
    r.h = (y1 > MENU_H ? MENU_H : y1) - r.y;
    return r;
}

static void rect(struct box b, uint16_t color)
{
    int x, y;
    b = clip(b);
    color = (uint16_t)((color & 0xfff0u) | 0x000fu);
    for (y = b.y; y < b.y + b.h; y++)
        for (x = b.x; x < b.x + b.w; x++)
            g_lin[y * MENU_W + x] = color;
}

static uint16_t blend(uint16_t dst, uint16_t src, unsigned alpha)
{
    unsigned inv = 255u - alpha;
    unsigned r = (((src >> 12) & 15u) * alpha + ((dst >> 12) & 15u) * inv + 127u) / 255u;
    unsigned g = (((src >> 8) & 15u) * alpha + ((dst >> 8) & 15u) * inv + 127u) / 255u;
    unsigned b = (((src >> 4) & 15u) * alpha + ((dst >> 4) & 15u) * inv + 127u) / 255u;
    return (uint16_t)((r << 12) | (g << 8) | (b << 4) | 15u);
}

static void sprite(int x, int y, const struct a7_sprite *s)
{
    unsigned row, col;
    if (!s || !s->pixels)
        return;
    for (row = 0; row < s->height; row++) {
        int py = y + (int)row;
        if (py < 0 || py >= MENU_H)
            continue;
        for (col = 0; col < s->width; col++) {
            int px = x + (int)col;
            uint16_t v;
            if (px < 0 || px >= MENU_W)
                continue;
            v = s->pixels[row * s->width + col];
            g_lin[py * MENU_W + px] = blend(g_lin[py * MENU_W + px], v,
                                            (v & 15u) * 17u);
        }
    }
}

/* alpha-mask text, colour supplied by the caller */
static void text_at(int x, int y, const struct a7_text *t, uint16_t color)
{
    unsigned row, col;
    if (!t || !t->alpha)
        return;
    for (row = 0; row < t->height; row++) {
        int py = y + (int)row;
        if (py < 0 || py >= MENU_H)
            continue;
        for (col = 0; col < t->width; col++) {
            int px = x + (int)col;
            unsigned a = t->alpha[row * t->width + col];
            if (px < 0 || px >= MENU_W || !a)
                continue;
            g_lin[py * MENU_W + px] = blend(g_lin[py * MENU_W + px], color, a);
        }
    }
}

/* alpha-mask text, colour supplied by the caller */
static void label(int x, int y, const struct ov_label *t, uint16_t color)
{
    unsigned row, col;
    if (!t || !t->alpha)
        return;
    for (row = 0; row < t->h; row++) {
        int py = y + (int)row;
        if (py < 0 || py >= MENU_H)
            continue;
        for (col = 0; col < t->w; col++) {
            int px = x + (int)col;
            unsigned a = t->alpha[row * t->w + col];
            if (px < 0 || px >= MENU_W || !a)
                continue;
            g_lin[py * MENU_W + px] = blend(g_lin[py * MENU_W + px], color, a);
        }
    }
}

/* ------------------------------------------------------------------ */
/* "old menu <-> new menu" switch button, top-right corner             */
/*                                                                     */
/* The same 44x44 button is drawn in both states: inside the overlay   */
/* frame while it is open, and as a small patch over the native screen */
/* while it is closed (see badge_tick).  Tapping that corner toggles.  */
/* ------------------------------------------------------------------ */

#define SW_BTN 44
#define SW_BTN_X (MENU_W - SW_BTN - 4)   /* 592 */
#define SW_BTN_Y 4
#define SW_TAP_X (SW_BTN_X - 8)          /* generous touch box */
#define SW_TAP_Y (SW_BTN_Y + SW_BTN + 4)

/* filled triangle (dir = +1 points right, -1 points left) in a 12x12 box */
static void tri(int x, int y, int dir, uint16_t color)
{
    int dy;
    for (dy = 0; dy < 12; dy++) {
        int d = dy - 5;
        int span;
        if (d < 0)
            d = -d;
        span = 12 - d;
        if (dir > 0)
            rect((struct box){ x, y + dy, span, 1 }, color);
        else
            rect((struct box){ x + 12 - span, y + dy, span, 1 }, color);
    }
}

/* two arrows pointing opposite ways = "exchange" */
static void draw_switch_icon(int x, int y, int active)
{
    uint16_t fill = active ? 0x1b4e : 0x0c26;
    uint16_t line = 0xffff;
    uint16_t edge = active ? 0x7d3f : 0x555f;

    rect((struct box){ x, y, SW_BTN, SW_BTN }, edge);
    rect((struct box){ x + 1, y + 1, SW_BTN - 2, SW_BTN - 2 }, fill);
    /* top arrow: shaft + head pointing right */
    rect((struct box){ x + 8, y + 14, 16, 2 }, line);
    tri(x + 22, y + 9, 1, line);
    /* bottom arrow: shaft + head pointing left */
    rect((struct box){ x + 20, y + 28, 16, 2 }, line);
    tri(x + 10, y + 23, -1, line);
}

static int plausible(const void *p)
{
    unsigned long v = (unsigned long)p;
    return v >= 0x10000UL && v < 0x80000000UL && (v & 1u) == 0;
}

/* NOTE (2026-11-15): an earlier build patched
 * snd_InfraInputEvent_to_InputServant to see the physical MENU key.  It hooked
 * fine (rc=0) but the guest dropped into its exception shell on the first key
 * press: that function is on a hot path, so an 8-byte code patch can be hit
 * mid-execution (same failure mode as A7R3_NOTES 49).  The hook is gone;
 * "MENU = back" is now derived from the *present* the native UI makes when it
 * reacts to a key -- see native_presented() below. */

struct node {
    unsigned vptr;
    void **children;
    int count;
    const void *properties;
    void *parent;
    int depth, selected, selectable, physical;
    unsigned state;
};

/* properties: { record*, count } with record = { int32 key, int32 kind,
 * uint32 value }.  On A7R3 `count` is the number of records (unlike ZV-E10,
 * where the same field was a last index). */
struct prop_rec {
    int key;
    int kind;
    unsigned value;
};

struct props {
    const struct prop_rec *recs;
    int count;
};

/* Setting nodes per category page (6 rows each).  All names are exported by
 * CautionConfig.so; anything that does not resolve falls back to the baked
 * caption and a "0/0" value, so a wrong guess never breaks the menu. */
#define CAT_COUNT 8
#define ROW_COUNT 6

static const char *const g_cat_syms[CAT_COUNT][ROW_COUNT] = {
    /* 0: image quality (the original demo page) */
    { "cmnViewSettingNodeRootRawFileType",
      "cmnViewSettingNodeRootStlImageJPEGQuality",
      "cmnViewSettingNodeRootStillImageSize",
      "cmnViewSettingNodeRootStlImageAspectRatio",
      "cmnViewSettingNodeRootStillPanoramaImageSize",
      "cmnViewSettingNodeRootStlImageQuality" },
    /* 1: common / power & display */
    { "cmnViewSettingNodeRootAutoPowerOffTemp",
      "cmnViewSettingNodeRootTouchOperation",
      "cmnViewSettingNodeRootSetupCleaningMode",
      "cmnViewSettingNodeRootSettingSetupNtscPalSEL",
      "cmnViewSettingNodeRootAutoMonitorOff",
      "cmnViewSettingNodeRootBeep" },
    /* 2: shooting */
    { "cmnViewSettingNodeRootSilentShutter",
      "cmnViewSettingNodeRootSteadyShot",
      "cmnViewSettingNodeRootZebra",
      "cmnViewSettingNodeRootPeakingSettings",
      "cmnViewSettingNodeRootCustomGridLine",
      "cmnViewSettingNodeRootAudioMode" },
    /* 3: playback */
    { "cmnViewSettingNodeRootPlaybackDisplay",
      "cmnViewSettingNodeRootGroupingDisplay",
      "cmnViewSettingNodeRootMemoryCardDisplayCardSpace",
      "cmnViewSettingNodeRootCopy",
      "cmnViewSettingNodeRootAutoImageExtract",
      "cmnViewSettingNodeRootWriteDate" },
    /* 4: movie */
    { "cmnViewSettingNodeRootMovieFileFormat",
      "cmnViewSettingNodeRootMovieRecordingSetting",
      "cmnViewSettingNodeRootMovieExpoMode",
      "cmnViewSettingNodeRootMovieAudioRecord",
      "cmnViewSettingNodeRootMovieSteadyShot",
      "cmnViewSettingNodeRootFrameRateZako" },
    /* 5: network */
    { "cmnViewSettingNodeRootAccessPoint",
      "cmnViewSettingNodeRootBluetoothSetting",
      "cmnViewSettingNodeRootAirplaneMode",
      "cmnViewSettingNodeRootWlPowerSave",
      "cmnViewSettingNodeRootControlWithSmartPhone",
      "cmnViewSettingNodeRootDisplayMACAddress" },
    /* 6: setup / system */
    { "cmnViewSettingNodeRootSetupVersion",
      "cmnViewSettingNodeRootSetupLanguage",
      "cmnViewSettingNodeRootClockDateTimeSetup",
      "cmnViewSettingNodeRootMemoryCardFormat",
      "cmnViewSettingNodeRootCopyrightInformation",
      "cmnViewSettingNodeRootVersion" },
    /* 7: custom */
    { "cmnViewSettingNodeRootCustomFunction",
      "cmnViewSettingNodeRootCustomButtonH",
      "cmnViewSettingNodeRootCustomDialWheelSetup",
      "cmnViewSettingNodeRootCustomFunctionShooting",
      "cmnViewSettingNodeRootTouchPadSettings",
      "cmnViewSettingNodeRootDISPButtonMonitor" },
};

static void *g_caution;
static struct node *g_nodes[CAT_COUNT][ROW_COUNT];
static int g_nodes_tried;

static void resolve_nodes(void)
{
    int c, i, found = 0;
    if (g_nodes_tried)
        return;
    g_nodes_tried = 1;
    if (!dlopen || !dlsym) {
        out("NM_NO_DL\n", 9);
        return;
    }
    g_caution = dlopen("/usr/lib/CautionConfig.so", 5 /* RTLD_LAZY|RTLD_NOLOAD */);
    log_hex("NM_caution=", (unsigned long)g_caution);
    for (c = 0; c < CAT_COUNT; c++) {
        for (i = 0; i < ROW_COUNT; i++) {
            g_nodes[c][i] = g_caution
                ? (struct node *)dlsym(g_caution, g_cat_syms[c][i]) : NULL;
            if (g_nodes[c][i])
                found++;
        }
    }
    log_hex("NM_nodes_found=", (unsigned long)found);
    log_hex("NM_node0=", (unsigned long)g_nodes[0][0]);
}

static unsigned g_category;
static unsigned g_selection;
static int g_open;   /* 1 = overlay visible (toggled by tap / web) */
static int g_dirty;  /* repaint on the next 20 ms beat */
static int g_page;   /* 0 = setting list, 1 = option popup (2nd level) */

/* After we hand the screen back, the native UI repaints its view.  Writing
 * into its buffers during that repaint is what crashed the guest before, so
 * stay quiet for a second. */
static long g_pause_until;

static void pause_paint(void)
{
    g_pause_until = mono_sec() + 1;
}

static int paint_paused(void)
{
    return g_pause_until && mono_sec() <= g_pause_until;
}

/* currently displayed row */
static struct node *cur_node(unsigned row)
{
    unsigned c = g_category % CAT_COUNT;
    if (row >= ROW_COUNT)
        row = 0;
    return g_nodes[c][row];
}

static void draw_number(int x, int y, unsigned value, unsigned total,
                        uint16_t color)
{
    unsigned digits[8];
    int n = 0, i;
    /* "value/total" with value already 1-based */
    if (value > 99u)
        value = 99u;
    if (total > 99u)
        total = 99u;
    if (value >= 10u)
        digits[n++] = (value / 10u) % 10u;
    digits[n++] = value % 10u;
    digits[n++] = 10;                 /* '/' */
    if (total >= 10u)
        digits[n++] = (total / 10u) % 10u;
    digits[n++] = total % 10u;
    for (i = 0; i < n; i++) {
        const struct ov_label *d = &ov_digits[digits[i]];
        label(x, y, d, color);
        x += d->w + 2;
    }
}

/* ------------------------------------------------------------------ */
/* menu content                                                        */
/* ------------------------------------------------------------------ */

#define ROW_COUNT 6
#define ROW_Y0 118
#define ROW_H 52

/* label indices per category (see LABELS in bake-labels.py) */
static const unsigned char g_rows[8][ROW_COUNT] = {
    { 1, 2, 3, 4, 5, 0 },   /* 闂佸搫鍊稿ú锝呪枎閵忋倕鍐€闁绘挸娴风涵鈧?/ JPEG闂佹眹鍨哄Σ鎺戭啅?/ JPEG闂佹悶鍎查崕鎶藉磿濮樺彉鐒婇柛婵嗗閸?/ 缂備焦鍎抽悘姘舵惎濠?/ 闂佺绻堥崝宥呪枍?/ 閻熸粍婢樺畷顒勫磿濮樿鲸瀚婚柕濞炬櫅濞?*/
    { 6, 7, 8, 9, 10, 11 }, /* 闂佸搫瀚晶浠嬪Φ濮樿鲸瀚婚柕濞炬櫅濞?/ 闂佺厧顨庢禍婊勬叏閳哄懎绀傞柧蹇ｅ亝缁?.. / 濠电偞鎸搁幊蹇曞閵夈儙鐔煎灳瀹曞洨顢?/ 闁荤喐鐟遍梽鍕箠濠婂牆绠肩€广儱瀚粙?*/
    { 6, 7, 8, 9, 10, 11 },
    { 1, 2, 3, 4, 5, 0 },
    { 6, 7, 8, 9, 10, 11 },
    { 6, 7, 8, 9, 10, 11 },
    { 1, 2, 3, 4, 5, 0 },
    { 6, 7, 8, 9, 10, 11 },
};

static int g_rows_logged;

/* defined below */
static const char *str_for_resid(unsigned resid);
static unsigned node_title_resid(const struct node *n);
static int text_top(int x, int top, const char *s, uint16_t color);
static int text_right(int right, int top, const char *s, uint16_t color);

#define TITLE_MY_MENU "\xe6\x88\x91\xe7\x9a\x84" "\xe8\x8f\x9c\xe5\x8d\x95"
/* "\xe8\xbf\x94\xe5\x9b\x9e" = 返回: MENU is the back key */
#define FOOTER_CLOSE "MENU " "\xe8\xbf\x94\xe5\x9b\x9e"

/* log one UTF-8 string so the host can verify the resolved captions */
static void log_utf8(const char *prefix, const char *s)
{
    int n = 0, pn = 0;
    if (!s)
        return;
    while (prefix[pn] && pn < 24)
        pn++;
    while (s[n] && n < 120)
        n++;
    out(prefix, pn);
    out(s, n);
    out("\n", 1);
}

/* --- apply a value through the firmware's own API ---------------------
 *
 * CmnViewSettingNode::setItemSelectedByIndex(int) resolves the child by index
 * and calls its virtual "select" slot (vtable +0xb8) -- the same path the
 * native menu takes when the user confirms an option, so the setting is
 * committed by the firmware rather than poked into its state.
 */
typedef int (*commit_fn)(void *self, int index);
typedef int (*getint2_fn)(void *self, int a, int *out);
static commit_fn g_commit_fn;
static commit_fn g_commit_sr;
static getint2_fn g_get_sr_idx;
static int g_commit_tried;

/* The parent's `selected` field holds the *SR number* of the current option,
 * not an array index; each child carries its own SR number in `state`. */
static struct node *child_by_sr(const struct node *n, unsigned sr)
{
    unsigned i;
    if (!n || !n->children)
        return NULL;
    for (i = 0; i < (unsigned)n->count; i++) {
        struct node *c = plausible(n->children[i])
            ? (struct node *)n->children[i] : NULL;
        if (c && c->state == sr)
            return c;
    }
    return NULL;
}

static const char *popup_text_of(struct node *n, int idx);
static void resolve_commit(void);

static const char *value_current(const struct node *n)
{
    struct node *m = (struct node *)n;
    int i;

    if (!m || !m->children || m->count <= 0)
        return NULL;
    /* `selected` holds the SR number; children[] is index based (option i+1
     * lives at children[i]), so the SR has to be mapped back to the index the
     * same way the firmware does it */
    resolve_commit();
    if (g_get_sr_idx) {
        for (i = 1; i <= m->count; i++) {
            int sr = -1;
            if (g_get_sr_idx(m, i, &sr) == 1 && sr == m->selected)
                return popup_text_of(m, i);
        }
    }
    if (m->selected >= 1 && m->selected <= m->count)
        return popup_text_of(m, m->selected);
    return NULL;
}

static const char *value_of(const struct node *n, unsigned idx)
{
    if (!n || !n->children || idx >= (unsigned)n->count || !n->children[idx])
        return NULL;
    return str_for_resid(
        node_title_resid((const struct node *)n->children[idx]));
}

static void resolve_commit(void)
{
    if (g_commit_tried)
        return;
    g_commit_tried = 1;
    if (!g_caution && dlopen)
        g_caution = dlopen("/usr/lib/CautionConfig.so", 5);
    if (g_caution && dlsym) {
        g_commit_fn = (commit_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode22setItemSelectedByIndexEi");
        g_commit_sr = (commit_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode22setItemSelectedBySRNumEi");
        g_get_sr_idx = (getint2_fn)dlsym(
            g_caution, "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    }
    log_hex("NM_commit=", (unsigned long)g_commit_fn);
    log_hex("NM_commit_sr=", (unsigned long)g_commit_sr);
    log_hex("NM_getsr=", (unsigned long)g_get_sr_idx);
}

static void commit_step(int dir)
{
    struct node *n = cur_node(g_selection);
    unsigned cnt, i, cur;
    const char *before, *after;

    if (!n || n->count <= 0 || !n->children) {
        out("NM_COMMIT none\n", 15);
        return;
    }
    resolve_commit();
    if (!g_commit_sr) {
        out("NM_COMMIT noapi\n", 16);
        return;
    }
    cnt = (unsigned)n->count;
    cur = (unsigned)n->selected;
    /* SR numbers are 1..count and are only obtainable through the firmware's
     * own getter (the child's `state` field is NOT the SR number). */
    {
        int cur_idx = -1;
        int new_sr = -1;
        int tgt;
        if (!g_get_sr_idx) {
            out("NM_COMMIT noget\n", 17);
            return;
        }
        for (i = 1; i <= cnt; i++) {
            int sr = -1;
            if (g_get_sr_idx(n, (int)i, &sr) == 1 && sr == (int)cur) {
                cur_idx = (int)i;
                break;
            }
        }
        if (cur_idx < 0) {
            out("NM_COMMIT nosr\n", 15);
            return;
        }
        tgt = cur_idx + dir;
        if (tgt < 1)
            tgt = (int)cnt;
        if (tgt > (int)cnt)
            tgt = 1;
        if (g_get_sr_idx(n, tgt, &new_sr) != 1 || new_sr < 0) {
            out("NM_COMMIT nosr2\n", 16);
            return;
        }
        before = value_current(n);
        g_commit_sr(n, new_sr);
        after = value_current(n);
        log_hex("NM_COMMIT row=", (unsigned long)g_selection);
        log_hex("NM_from=", (unsigned long)cur);
        log_hex("NM_to=", (unsigned long)new_sr);
    log_hex("NM_selected=", (unsigned long)n->selected);
        log_utf8("NM_BEFORE ", before ? before : "?");
        log_utf8("NM_AFTER ", after ? after : "?");
    }
}

/* ------------------------------------------------------------------ */
/* second level: option popup                                          */
/*                                                                     */
/* Tapping the already selected row opens this page.  It lists every   */
/* option of the row (SR numbers resolved through the firmware's own   */
/* getter), marks the current one, and applying an entry calls          */
/* setItemSelectedBySRNum -- the same call the native menu makes.       */
/* ------------------------------------------------------------------ */

#define POPUP_X 70
#define POPUP_Y 80
#define POPUP_W 500
#define POPUP_TITLE_H 46
#define POPUP_OPT_H 40
#define POPUP_OPTS 7
#define POPUP_MORE_H 26
#define POPUP_H (POPUP_TITLE_H + POPUP_OPTS * POPUP_OPT_H + POPUP_MORE_H + 8)

static int g_opt_first;             /* 1-based index of the first entry */
static int g_opt_hover;             /* 1-based index highlighted by keys */
static unsigned g_opt_total;        /* number of options in the row */
static int g_opt_sr[POPUP_OPTS];
static const char *g_opt_text[POPUP_OPTS];

static int popup_sr_of(struct node *n, int idx)
{
    int sr = -1;
    if (!n || idx < 1)
        return -1;
    resolve_commit();
    if (g_get_sr_idx && g_get_sr_idx(n, idx, &sr) == 1 && sr > 0)
        return sr;
    return idx;
}

/* children[] is index based (children[i] belongs to option i+1) */
static const char *popup_text_of(struct node *n, int idx)
{
    struct node *c;
    if (!n || !n->children || idx < 1 || idx > n->count)
        return NULL;
    c = plausible(n->children[idx - 1])
        ? (struct node *)n->children[idx - 1] : NULL;
    return c ? str_for_resid(node_title_resid(c)) : NULL;
}

static void popup_fill(void)
{
    struct node *n = cur_node(g_selection);
    int i;

    for (i = 0; i < POPUP_OPTS; i++) {
        int idx = g_opt_first + i;
        int sr = 0;
        const char *t = NULL;
        if (idx <= (int)g_opt_total) {
            sr = popup_sr_of(n, idx);
            t = popup_text_of(n, idx);
        }
        g_opt_sr[i] = (idx <= (int)g_opt_total) ? sr : 0;
        g_opt_text[i] = t;
    }
}

static void popup_open(void)
{
    struct node *n = cur_node(g_selection);
    int i, cur_idx = -1, sr;

    g_page = 1;
    g_dirty = 1;
    g_opt_total = n ? (unsigned)n->count : 0;
    if (!n || n->count <= 0) {
        g_opt_total = 0;
        g_opt_first = 1;
        g_opt_hover = 1;
        popup_fill();
        out("NM_POPUP empty\n", 15);
        return;
    }
    for (i = 1; i <= (int)g_opt_total; i++) {
        sr = popup_sr_of(n, i);
        if (sr >= 0 && sr == n->selected) {
            cur_idx = i;
            break;
        }
    }
    if (cur_idx < 0)
        cur_idx = 1;
    g_opt_first = ((cur_idx - 1) / POPUP_OPTS) * POPUP_OPTS + 1;
    g_opt_hover = cur_idx;
    popup_fill();
    log_hex("NM_POPUP open total=", (unsigned long)g_opt_total);
    log_hex("NM_POPUP cur=", (unsigned long)cur_idx);
    log_utf8("NM_POPUP title ",
             str_for_resid(node_title_resid(n)) ?
             str_for_resid(node_title_resid(n)) : "?");
}

static void popup_move(int dir)
{
    int last_page, page, i;

    if (g_opt_total <= POPUP_OPTS) {
        /* straight navigation inside the single page */
        g_opt_hover += dir;
        if (g_opt_hover < 1)
            g_opt_hover = 1;
        if (g_opt_hover > (int)g_opt_total)
            g_opt_hover = (int)g_opt_total;
        g_dirty = 1;
        return;
    }
    last_page = ((int)g_opt_total - 1) / POPUP_OPTS;
    page = (g_opt_hover - 1) / POPUP_OPTS;
    i = (g_opt_hover - 1) % POPUP_OPTS;
    i += dir;
    if (i < 0) {
        if (page > 0) {
            page--;
            i = POPUP_OPTS - 1;
        } else {
            i = 0;
        }
    } else if (i >= POPUP_OPTS || g_opt_hover + dir > (int)g_opt_total) {
        if (page < last_page && g_opt_hover + dir <= (int)g_opt_total) {
            page++;
            i = 0;
        } else {
            i = (int)g_opt_total - page * POPUP_OPTS - 1;
            if (i > POPUP_OPTS - 1)
                i = POPUP_OPTS - 1;
        }
    }
    g_opt_hover = page * POPUP_OPTS + i + 1;
    g_opt_first = page * POPUP_OPTS + 1;
    popup_fill();
    g_dirty = 1;
    log_hex("NM_POPUP hover=", (unsigned long)g_opt_hover);
}

static void popup_page(int dir)
{
    int last_first;

    if (g_opt_total <= POPUP_OPTS)
        return;
    last_first = (((int)g_opt_total - 1) / POPUP_OPTS) * POPUP_OPTS + 1;
    g_opt_first += dir * POPUP_OPTS;
    if (g_opt_first < 1)
        g_opt_first = 1;
    if (g_opt_first > last_first)
        g_opt_first = last_first;
    if (g_opt_hover < g_opt_first)
        g_opt_hover = g_opt_first;
    if (g_opt_hover > g_opt_first + POPUP_OPTS - 1)
        g_opt_hover = g_opt_first + POPUP_OPTS - 1;
    if (g_opt_hover > (int)g_opt_total)
        g_opt_hover = (int)g_opt_total;
    popup_fill();
    g_dirty = 1;
    log_hex("NM_POPUP page=", (unsigned long)g_opt_first);
}

static void popup_apply(int idx)
{
    struct node *n = cur_node(g_selection);
    int sr, i;

    if (!n || idx < 1 || idx > (int)g_opt_total) {
        out("NM_POPUP badidx\n", 15);
        return;
    }
    sr = popup_sr_of(n, idx);
    resolve_commit();
    if (!g_commit_sr || sr <= 0) {
        out("NM_POPUP noapi\n", 15);
        return;
    }
    g_commit_sr(n, sr);
    log_hex("NM_POPUP apply idx=", (unsigned long)idx);
    log_hex("NM_POPUP sr=", (unsigned long)sr);
    log_hex("NM_POPUP selected=", (unsigned long)n->selected);
    log_utf8("NM_POPUP value ",
             value_current(n) ? value_current(n) : "?");
    /* refresh the list row behind the popup, then go back to it */
    for (i = 0; i < POPUP_OPTS; i++)
        g_opt_sr[i] = 0;
    g_page = 0;
    g_dirty = 1;
}

/* cheap 50% dim outside the popup (no divisions: one shift per channel) */
static void dim_out(struct box box)
{
    struct box parts[4];
    int i;

    parts[0] = (struct box){ 0, 0, MENU_W, box.y };
    parts[1] = (struct box){ 0, box.y + box.h, MENU_W,
                             MENU_H - (box.y + box.h) };
    parts[2] = (struct box){ 0, box.y, box.x, box.h };
    parts[3] = (struct box){ box.x + box.w, box.y,
                             MENU_W - (box.x + box.w), box.h };
    for (i = 0; i < 4; i++) {
        struct box b = clip(parts[i]);
        int x, y;
        for (y = b.y; y < b.y + b.h; y++) {
            u16 *row = &g_lin[y * MENU_W];
            for (x = b.x; x < b.x + b.w; x++)
                row[x] = (u16)(((row[x] >> 1) & 0x7777u) | 0x000fu);
        }
    }
}

static void popup_draw(void)
{
    struct node *n = cur_node(g_selection);
    int rows = (int)g_opt_total - g_opt_first + 1;
    struct box box;
    const char *title = n ? str_for_resid(node_title_resid(n)) : NULL;
    int i;

    if (rows < 1)
        rows = 1;
    if (rows > POPUP_OPTS)
        rows = POPUP_OPTS;
    box = (struct box){ POPUP_X, POPUP_Y, POPUP_W,
                        POPUP_TITLE_H + rows * POPUP_OPT_H +
                        (g_opt_total > (unsigned)POPUP_OPTS ? POPUP_MORE_H : 0) + 8 };
    dim_out(box);

    rect(box, 0x1112);
    rect((struct box){ box.x, box.y, box.w, 2 }, 0x666f);
    rect((struct box){ box.x, box.y + box.h - 2, box.w, 2 }, 0x666f);
    rect((struct box){ box.x, box.y, 2, box.h }, 0x666f);
    rect((struct box){ box.x + box.w - 2, box.y, 2, box.h }, 0x666f);
    rect((struct box){ box.x + 2, box.y + POPUP_TITLE_H - 1, box.w - 4, 1 }, 0x333f);

    if (title)
        text_top(box.x + 18, box.y + 10, title, 0xffff);
    if (g_opt_total > POPUP_OPTS)
        draw_number(box.x + box.w - 74, box.y + 10,
                    (unsigned)((g_opt_first - 1) / POPUP_OPTS + 1),
                    (unsigned)((g_opt_total + POPUP_OPTS - 1) / POPUP_OPTS),
                    0x889f);

    if (g_opt_total == 0) {
        text_top(box.x + 18, box.y + POPUP_TITLE_H + 8,
                 "\xe6\x97\xa0\xe5\x8f\xaf\xe7\x94\xa8"
                 "\xe9\x80\x89\xe9\xa1\xb9", 0x889f);
        return;
    }
    for (i = 0; i < POPUP_OPTS; i++) {
        int idx = g_opt_first + i;
        int y = box.y + POPUP_TITLE_H + i * POPUP_OPT_H;
        int cur, hov;
        uint16_t fg = 0xdddf;
        if (idx > (int)g_opt_total || g_opt_sr[i] <= 0)
            continue;
        cur = (n && (int)n->selected == g_opt_sr[i]);
        hov = (idx == g_opt_hover);
        rect((struct box){ box.x + 10, y, box.w - 20, POPUP_OPT_H - 2 },
             hov ? 0xd520 : (cur ? 0x0a4e : 0x000f));
        /* radio mark: hollow square, filled when it is the current value */
        rect((struct box){ box.x + 22, y + 12, 14, 2 }, 0x889f);
        rect((struct box){ box.x + 22, y + 24, 14, 2 }, 0x889f);
        rect((struct box){ box.x + 22, y + 12, 2, 14 }, 0x889f);
        rect((struct box){ box.x + 34, y + 12, 2, 14 }, 0x889f);
        if (cur) {
            rect((struct box){ box.x + 26, y + 16, 6, 6 }, 0xffff);
            fg = 0xffff;
        }
        if (g_opt_text[i])
            text_top(box.x + 48, y + 8, g_opt_text[i], fg);
        else
            draw_number(box.x + 48, y + 8, (unsigned)idx, 0, fg);
    }
    if (g_opt_total > POPUP_OPTS) {
        int y = box.y + POPUP_TITLE_H + rows * POPUP_OPT_H + 2;
        /* "下一页" -- the strip always pages forward and wraps to page 1 */
        text_top(box.x + 18, y,
                 "\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa1\xb5", 0x889f);
    }
}

/* one step back: popup -> list -> native menu (restores the native frame) */
static void overlay_close(int restore);
static void overlay_open(void);

static void nm_back(void)
{
    if (!g_open)
        return;
    if (g_page) {
        g_page = 0;
        out("NM_BACK popup\n", 14);
    } else {
        overlay_close(1);
        out("NM_BACK close\n", 14);
    }
    g_dirty = 1;
}
static void build_frame(void)
{
    struct box frame = { 0, 0, MENU_W, MENU_H };
    int i;
    int log_rows = !g_rows_logged;

    rect(frame, 0x000f);                       /* black background */

    /* category strip */
    for (i = 0; i < 8; i++) {
        const struct a7_sprite *s = (i == (int)g_category)
            ? &a7_tab_selected[i] : &a7_tab_normal[i];
        sprite(4 + i * 58, 2, s);
    }

    /* header title + page counter */
    rect((struct box){ 0, 56, MENU_W, 44 }, 0x111f);
    text_top(20, 64, TITLE_MY_MENU, 0xdddf);
    draw_number(MENU_W - 76, 62, g_category + 1u, CAT_COUNT, 0xdddf);

    /* rows */
    for (i = 0; i < ROW_COUNT; i++) {
        struct box row = { 0, ROW_Y0 + i * ROW_H, MENU_W, ROW_H - 2 };
        int selected = ((unsigned)i == g_selection);
        uint16_t color = selected ? 0xffff : 0xdddf;
        const char *title = NULL, *value = NULL;
        unsigned sel = 0, cnt = 0;

        rect(row, selected ? 0xd520 : 0x111f);

        {
            struct node *nd = cur_node((unsigned)i);
            if (nd) {
                title = str_for_resid(node_title_resid(nd));
                cnt = (unsigned)nd->count;
                value = value_current(nd);
                if (cnt > 0)
                    sel = (unsigned)nd->selected;
            }
        }

        if (title)
            text_top(24, row.y + 12, title, color);
        else
            label(24, row.y + 12, &ov_labels[g_rows[g_category & 7u][i]], color);

        if (value) {
            text_right(MENU_W - 48, row.y + 12, value, color);
        } else if (cnt > 0) {
            draw_number(MENU_W - 170, row.y + 12, sel + 1u, cnt, color);
        } else {
            draw_number(MENU_W - 170, row.y + 12, 0u, 0u,
                        selected ? 0xffff : 0x88af);
        }
        /* ">": this row opens a second level */
        if (cnt > 0) {
            int cx = MENU_W - 30, cy = row.y + 26;
            int k;
            for (k = 0; k < 7; k++) {
                rect((struct box){ cx + k, cy - 8 + k, 2, 2 }, color);
                rect((struct box){ cx + k, cy + 8 - k, 2, 2 }, color);
            }
        }

        if (log_rows && i < 6) {
            log_hex("NM_ROW i=", (unsigned long)i);
            log_utf8("NM_TITLE", title ? title : "<baked>");
            log_utf8("NM_VALUE", value ? value : "<n/m>");
        }
    }
    g_rows_logged = 1;

    /* footer + the old/new switch button in the top-right corner */
    text_top(MENU_W - 190, MENU_H - 40, FOOTER_CLOSE, 0xdddf);
    draw_switch_icon(SW_BTN_X, SW_BTN_Y, 1);
    if (g_page)
        popup_draw();
}

/* ------------------------------------------------------------------ */
/* text rendering with the firmware's own bitmap font                  */
/*                                                                     */
/* font_atlas.h is baked from MYingHeiC-GB18030-SJ (the .ltt the native */
/* UI loads) into 4-bit alpha glyphs, which is exactly the panel's      */
/* per-channel depth.  Glyph offsets are relative to the baseline.      */
/* ------------------------------------------------------------------ */

static const struct ov_glyph *glyph_lookup(unsigned cp)
{
    int lo = 0, hi = OV_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned v = ov_glyphs[mid].cp;
        if (v == cp)
            return &ov_glyphs[mid];
        if (v < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static unsigned utf8_next(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned c = p[0];
    if (c < 0x80u) {
        *pp = p + 1;
        return c;
    }
    if ((c & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {
        *pp = p + 2;
        return ((c & 0x1fu) << 6) | (p[1] & 0x3fu);
    }
    if ((c & 0xf0u) == 0xe0u && (p[1] & 0xc0u) == 0x80u &&
        (p[2] & 0xc0u) == 0x80u) {
        *pp = p + 3;
        return ((c & 0x0fu) << 12) | ((p[1] & 0x3fu) << 6) | (p[2] & 0x3fu);
    }
    if ((c & 0xf8u) == 0xf0u && (p[1] & 0xc0u) == 0x80u &&
        (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {
        *pp = p + 4;
        return ((c & 0x07u) << 18) | ((p[1] & 0x3fu) << 12) |
               ((p[2] & 0x3fu) << 6) | (p[3] & 0x3fu);
    }
    *pp = p + 1;
    return 0xfffdu;
}

/* The firmware prefixes some captions with private-use icon codepoints
 * (U+E08C ...).  The native menu does not draw icons on these rows, and the
 * font has no glyphs for them, so they are skipped like the native view does. */
static int skip_cp(unsigned cp)
{
    return cp < 0x20u || (cp >= 0xe000u && cp <= 0xf8ffu) || cp == 0xfffdu;
}

static int text_width(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;
    while (p && *p) {
        unsigned cp = utf8_next(&p);
        const struct ov_glyph *g;
        if (skip_cp(cp))
            continue;
        g = glyph_lookup(cp);
        if (g)
            w += g->adv;
    }
    return w;
}

/* draw UTF-8 text with the baseline at `baseline`; returns the advance */
static int text_utf8(int x, int baseline, const char *s, uint16_t color)
{
    const unsigned char *p = (const unsigned char *)s;
    int pen = x;

    while (p && *p) {
        unsigned cp = utf8_next(&p);
        const struct ov_glyph *g;
        int gx, gy;
        unsigned row, col;
        if (skip_cp(cp))
            continue;
        g = glyph_lookup(cp);
        if (!g)
            continue;
        gx = pen + g->x;
        gy = baseline + g->y;
        for (row = 0; row < g->h; row++) {
            int py = gy + (int)row;
            if (py < 0 || py >= MENU_H)
                continue;
            for (col = 0; col < g->w; col++) {
                int px = gx + (int)col;
                unsigned o = g->off + (row * g->w + col) / 2u;
                unsigned a = (row * g->w + col) & 1u
                    ? (ov_glyph_bits[o] & 0x0fu) : (ov_glyph_bits[o] >> 4);
                if (px < 0 || px >= MENU_W || !a)
                    continue;
                g_lin[py * MENU_W + px] =
                    blend(g_lin[py * MENU_W + px], color, a * 17u);
            }
        }
        pen += g->adv;
    }
    return pen - x;
}

/* draw text with its top at `top` (baseline = top + ascent) */
static int text_top(int x, int top, const char *s, uint16_t color)
{
    return text_utf8(x, top + OV_FONT_ASCENT, s, color);
}

/* right-align text so that it ends at `right`; returns the width */
static int text_right(int right, int top, const char *s, uint16_t color)
{
    int w = text_width(s);
    text_top(right - w, top, s, color);
    return w;
}

/* ------------------------------------------------------------------ */
/* firmware string table: resid 0x02xxxxxx -> id 0x1Exxxxxx -> UTF-8    */
/*                                                                     */
/* The native UI resolves menu labels through /usr/share/app/           */
/* string_simplifiedchinese.uxc; we parse the same container at runtime */
/* so captions are exactly the firmware's own wording.                  */
/* ------------------------------------------------------------------ */

#define STRTAB_PATH "/usr/share/app/string_simplifiedchinese.uxc"
#define STRTAB_MAX 8192
#define STRTAB_BYTES 400000

static unsigned char g_strtab[STRTAB_BYTES];
static unsigned g_st_id[STRTAB_MAX];
static unsigned g_st_off[STRTAB_MAX];
static int g_st_n = -1;

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

static int strtab_load(void)
{
    long fd, n = 0, total = 0;
    const unsigned char *p, *end;

    if (g_st_n >= 0)
        return g_st_n;
    g_st_n = 0;
    fd = sys3(SYS_open, (long)STRTAB_PATH, 0, 0);
    if (fd < 0) {
        log_hex("NM_STRTAB open=-1", 0);
        return 0;
    }
    while (total < STRTAB_BYTES) {
        n = sys3(SYS_read, fd, (long)(g_strtab + total), STRTAB_BYTES - total);
        if (n <= 0)
            break;
        total += n;
    }
    sys2(SYS_close, fd, 0);
    log_hex("NM_STRTAB bytes=", (unsigned long)total);
    if (total < 64)
        return 0;

    p = g_strtab + 16;                 /* records start after the offset table */
    end = g_strtab + total;
    while (p + 8 < end && g_st_n < STRTAB_MAX) {
        unsigned id = rd32(p);
        const unsigned char *t = p + 4;
        if (id >= 0x1e000000u && id < 0x1f000000u && t[0] >= 0x20u) {
            const unsigned char *z = t;
            while (z < end && *z && (z - t) < 200)
                z++;
            if (z < end && *z == 0 && z > t) {
                g_st_id[g_st_n] = id;
                g_st_off[g_st_n] = (unsigned)(t - g_strtab);
                g_st_n++;
                p = (const unsigned char *)(((unsigned long)(z + 4)) & ~3UL);
                continue;
            }
        }
        p = (const unsigned char *)(((unsigned long)(p + 4)) & ~3UL);
    }
    log_hex("NM_STRTAB entries=", (unsigned long)g_st_n);
    return g_st_n;
}

static const char *str_for_resid(unsigned resid)
{
    unsigned id;
    int i;

    if (resid < 0x02000000u || resid >= 0x03000000u)
        return NULL;
    if (!strtab_load())
        return NULL;
    id = 0x1e000000u | (resid & 0x00ffffffu);
    /* the table is mostly ascending, but a linear scan is cheap enough here */
    for (i = 0; i < g_st_n; i++) {
        if (g_st_id[i] == id)
            return (const char *)(g_strtab + g_st_off[i]);
    }
    return NULL;
}

/* title resid of a node (properties key 0), 0 if unknown */
static unsigned node_title_resid(const struct node *n)
{
    const struct prop_rec *r;
    const struct props *pr;
    int i;

    if (!n || !n->properties)
        return 0;
    pr = (const struct props *)n->properties;
    if (!pr->recs || pr->count <= 0 || pr->count > 64)
        return 0;
    r = pr->recs;
    for (i = 0; i < pr->count; i++) {
        if (r[i].key == 0)
            return r[i].value;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* panel write                                                         */
/* ------------------------------------------------------------------ */

/* Last content we left in the panel, plus whether we have one. */
static u16 g_last[MENU_W * MENU_H];
static int g_have_last;

static int same_pixels(const u16 *a, const u16 *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void copy_pixels(u16 *dst, const u16 *src, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static int put_panel_at(unsigned long pa, int gate)
{
    const unsigned long base = pa & ~0xfffUL;
    const unsigned long off = pa & 0xfffUL;
    long m;
    int fd;
    u16 *fb;
    unsigned x, y;

    fd = (int)sys3(SYS_open, (long)"/dev/mem", 2, 0);
    if (fd < 0)
        return -1;
    m = sys4(SYS_mmap2, 0, 0x98000, 3, 1, fd, (long)(base >> 12));
    if (m < 0 && m > -4096) {
        sys2(SYS_close, fd, 0);
        return -2;
    }
    fb = (u16 *)(m + off);
    /* Idle gate: if the panel no longer holds what we last wrote, the native UI
     * has repainted.  Writing during a repaint is exactly what crashed the guest
     * (A7R3_NOTES 50), so skip this round and try again on the next tick. */
    if (gate && g_have_last && !same_pixels(fb, g_last, MENU_W * MENU_H)) {
        copy_pixels(g_last, fb, MENU_W * MENU_H);
        sys3(SYS_munmap, m, 0x98000, 0);
        sys2(SYS_close, fd, 0);
        return -3;
    }
    for (y = 0; y < MENU_H; y++) {
        /* Morton 8x8 tiled, rows flipped: inverse of the model's dump. */
        unsigned yy = MENU_H - 1u - y;
        unsigned rowbase = (yy >> 3) * 80u;
        for (x = 0; x < MENU_W; x++) {
            unsigned z = (x & 1u) | ((yy & 1u) << 1) | ((x & 2u) << 1) |
                         ((yy & 2u) << 2) | ((x & 4u) << 2) |
                         ((yy & 4u) << 3);
            unsigned o = (rowbase + (x >> 3)) * 64u + z;
            fb[o] = g_lin[y * MENU_W + x];
        }
    }
    if (gate) {
        copy_pixels(g_last, fb, MENU_W * MENU_H);
        g_have_last = 1;
    }
    sys3(SYS_munmap, m, 0x98000, 0);
    sys2(SYS_close, fd, 0);
    return 0;
}

static int put_panel(void)
{
    return put_panel_at(0x202E0200UL, 1);
}

/* Present/flip buffers: the app copies the render target here when it presents,
 * so pixels written here stay until the next present. */
static int put_flip(void)
{
    int rc = 0;

    if (!file_exists("/setting/nm_flip"))
        return 0;
    if (put_panel_at(0x23A80000UL, 0) != 0)
        rc = -1;
    if (put_panel_at(0x23B16000UL, 0) != 0)
        rc = -1;
    if (rc == 0) {
        /* keep the guest log small: /tmp is a 12 MB vfat partition */
        static unsigned flip_logs;
        if ((flip_logs++ % 20u) == 0)
            out("NM_FLIP\n", 8);
    }
    return rc;
}

static volatile int g_frames;
static long g_first_paint;
static volatile int g_busy_ticks;
static volatile int g_present_ready;

/* --- taking the screen and handing it back --------------------------
 *
 * The native app only redraws when *it* has something new, so simply
 * stopping our writes would leave our menu on the glass forever.  Instead the
 * native frame is captured the moment the overlay opens and written back
 * (with the corner button) when the user switches back by hand.
 * ------------------------------------------------------------------ */

#define RENDER_PA 0x202E0200UL

static u16 g_native_bg[MENU_W * MENU_H];
static int g_saved_ok;

static int panel_grab(void)
{
    const unsigned long base = RENDER_PA & ~0xfffUL;
    const unsigned long off = RENDER_PA & 0xfffUL;
    long m;
    int fd;
    const u16 *fb;
    unsigned x, y;

    fd = (int)sys3(SYS_open, (long)"/dev/mem", 0, 0);
    if (fd < 0)
        return -1;
    m = sys4(SYS_mmap2, 0, 0x98000, 1, 1, fd, (long)(base >> 12));
    if (m < 0 && m > -4096) {
        sys2(SYS_close, fd, 0);
        return -2;
    }
    fb = (const u16 *)(m + off);
    for (y = 0; y < MENU_H; y++) {
        unsigned yy = MENU_H - 1u - y;
        unsigned rowbase = (yy >> 3) * 80u;
        for (x = 0; x < MENU_W; x++) {
            unsigned z = (x & 1u) | ((yy & 1u) << 1) | ((x & 2u) << 1) |
                         ((yy & 2u) << 2) | ((x & 4u) << 2) |
                         ((yy & 4u) << 3);
            g_native_bg[y * MENU_W + x] =
                fb[(rowbase + (x >> 3)) * 64u + z];
        }
    }
    sys3(SYS_munmap, m, 0x98000, 0);
    sys2(SYS_close, fd, 0);
    g_saved_ok = 1;
    out("NM_GRAB\n", 8);
    return 0;
}

static void panel_restore(void)
{
    unsigned i;

    if (!g_saved_ok)
        return;
    for (i = 0; i < (unsigned)(MENU_W * MENU_H); i++)
        g_lin[i] = g_native_bg[i];
    draw_switch_icon(SW_BTN_X, SW_BTN_Y, 0);
    put_panel_at(RENDER_PA, 0);
    put_flip();
    g_saved_ok = 0;
    out("NM_RESTORE\n", 11);
}

static void overlay_open(void)
{
    if (g_open)
        return;
    if (g_saved_ok)
        g_saved_ok = 0;
    panel_grab();
    g_open = 1;
    g_selection = 0;
    g_page = 0;
    g_dirty = 1;
    out("NM_OPEN\n", 8);
}

static void overlay_close(int restore)
{
    if (!g_open) {
        g_page = 0;
        return;
    }
    g_open = 0;
    g_page = 0;
    g_dirty = 1;
    if (restore)
        panel_restore();
    pause_paint();
}

/* Prototype input channel: the serial shell writes one character into
 * /setting/nm_key ('u'/'d'/'c'); we consume and unlink it.  Hooking the real
 * input path is the next step, but this keeps the demo controllable without
 * touching the firmware's input service. */
/* dump the option list of the selected row: index, caption, per-node flags */
static void list_children(void)
{
    struct node *n = cur_node(g_selection);
    unsigned i;

    if (!n || !n->children) {
        out("NM_LIST none\n", 13);
        return;
    }
    log_hex("NM_LIST row=", (unsigned long)g_selection);
    log_hex("NM_LIST count=", (unsigned long)n->count);
    log_hex("NM_LIST selected=", (unsigned long)n->selected);
    log_hex("NM_LIST state=", (unsigned long)n->state);
    for (i = 0; i < (unsigned)n->count && i < 10; i++) {
        struct node *c = plausible(n->children[i])
            ? (struct node *)n->children[i] : NULL;
        log_hex("NM_OPT idx=", (unsigned long)i);
        log_utf8("NM_OPT_TXT ", value_of(n, i));
        if (c) {
            log_hex("NM_OPT sel=", (unsigned long)c->selected);
            log_hex("NM_OPT st=", (unsigned long)c->state);
            log_hex("NM_OPT cnt=", (unsigned long)c->count);
        }
    }
}

/* ------------------------------------------------------------------ */
/* touch input                                                         */
/*                                                                     */
/* InputService::callback_tp stores the last touch position into a      */
/* libObj .bss struct:                                                */
/*   ldr r3,[pc,#0x68] / ldr r3,[r5,r3] / strd r10,r11,[r3]           */
/*   -> *(GOT + 0x2c64) == libObj + 0x0129eb20, first two words = x,y  */
/* Polling it needs no code patching, so it cannot destabilise the UI. */
/* ------------------------------------------------------------------ */

#define LIBOBJ_GRM_CREATE 0x636bd9UL
#define LIBOBJ_TOUCH 0x0129eb20UL
#define TOUCH_TAP_MS 2500u

static unsigned long libobj_base(void)
{
    void *obj, *fn;
    if (!dlopen || !dlsym)
        return 0;
    obj = dlopen("/usr/lib/libObj.so", 5);
    if (!obj)
        return 0;
    fn = dlsym(obj, "GRM_bitmapCreate");
    if (!fn)
        return 0;
    return (unsigned long)fn - LIBOBJ_GRM_CREATE;
}

static unsigned now_ms(void)
{
    struct timespec32 ts;
    if (sys2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts) != 0)
        return 0;
    return (unsigned)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static volatile int *touch_cell(void)
{
    static unsigned long base;
    if (!base)
        base = libobj_base();
    return base ? (volatile int *)(base + LIBOBJ_TOUCH) : NULL;
}

static int g_touch_x = -1000, g_touch_y = -1000;
static int g_selftest_done;
static unsigned g_settle_at;
static int g_settle_done;
static int g_tap_stage;
static unsigned g_tap_next;
static unsigned g_tap_ms;
static unsigned g_last_tap_ms;   /* same, for the "native presented" heuristic */

static void handle_touch(int x, int y)
{
    int row;

    log_hex("NM_TOUCH x=", (unsigned long)x);
    log_hex("NM_TOUCH y=", (unsigned long)y);
    if (x < 0 || y < 0 || x >= MENU_W || y >= MENU_H)
        return;
    g_last_tap_ms = now_ms();

    /* the switch button sits in the top-right corner in both states */
    if (x >= SW_TAP_X && y <= SW_TAP_Y) {
        if (g_open)
            overlay_close(1);
        else
            overlay_open();
        log_hex("NM_SWITCH open=", (unsigned long)g_open);
        return;
    }

    if (!g_open) {
        /* anything else on the native screen belongs to the native UI */
        return;
    }
    if (g_page) {                        /* option popup is modal */
        struct box box;
        int rows = (int)g_opt_total - g_opt_first + 1;
        if (rows < 1)
            rows = 1;
        if (rows > POPUP_OPTS)
            rows = POPUP_OPTS;
        box = (struct box){ POPUP_X, POPUP_Y, POPUP_W,
                            POPUP_TITLE_H + rows * POPUP_OPT_H +
                            (g_opt_total > (unsigned)POPUP_OPTS ?
                             POPUP_MORE_H : 0) + 8 };
        if (x < box.x || x >= box.x + box.w ||
            y < box.y || y >= box.y + box.h) {
            nm_back();                   /* tap outside = back */
            return;
        }
        row = (y - box.y - POPUP_TITLE_H) / POPUP_OPT_H;
        if (row >= 0 && row < POPUP_OPTS) {
            int idx = g_opt_first + row;
            if (idx <= (int)g_opt_total && g_opt_sr[row] > 0) {
                log_hex("NM_POPUP tap=", (unsigned long)idx);
                popup_apply(idx);
                return;
            }
        }
        /* "更多" strip at the bottom pages forward (wraps to page 1) */
        if (g_opt_total > (unsigned)POPUP_OPTS &&
            y >= box.y + POPUP_TITLE_H + rows * POPUP_OPT_H) {
            popup_page(g_opt_first + POPUP_OPTS > (int)g_opt_total ? -99 : 1);
            log_hex("NM_POPUP page=", (unsigned long)g_opt_first);
        }
        return;
    }
    if (y >= 434) {                     /* footer: back to the native menu */
        nm_back();
        return;
    }
    if (y < 56) {                       /* category strip */
        row = x / 58;
        if (row >= 0 && row < 8) {
            g_category = (unsigned)row;
            g_dirty = 1;
            log_hex("NM_TAP cat=", (unsigned long)row);
        }
        return;
    }
    if (y >= ROW_Y0 && y < ROW_Y0 + ROW_COUNT * ROW_H) {
        row = (y - ROW_Y0) / ROW_H;
        if (row == (int)g_selection) {
            /* second tap on the selected row opens its second-level page */
            log_hex("NM_TAP2 row=", (unsigned long)row);
            popup_open();
        } else {
            g_selection = (unsigned)row;
            g_dirty = 1;
        }
        g_tap_ms = now_ms();
        log_hex("NM_TAP row=", (unsigned long)row);
    }
}

static void poll_touch(void)
{
    volatile int *tp = touch_cell();
    int x, y;
    if (!tp)
        return;
    x = tp[0];
    y = tp[1];
    if (x == g_touch_x && y == g_touch_y)
        return;
    if (x == 0 && y == 0)          /* "no touch" placeholder */
        return;
    g_touch_x = x;
    g_touch_y = y;
    handle_touch(x, y);
}

/* write a synthetic touch position, so the reaction can be tested without a
 * real panel (the driver normally writes exactly these two words) */
static void simulate_tap(int row)
{
    static unsigned n;
    volatile int *tp = touch_cell();
    if (!tp) {
        out("NM_SIM notouch\n", 15);
        return;
    }
    /* nudge the coordinates so every synthetic tap is a distinct event */
    n++;
    tp[0] = MENU_W / 2 + (int)(n & 3u) * 3;
    tp[1] = ROW_Y0 + row * ROW_H + ROW_H / 2 + (int)(n & 1u) * 2;
    log_hex("NM_SIM row=", (unsigned long)row);
}

/* one-shot self test: try every exported "select/apply" entry point and report
 * what actually changes (parent selected, child state, resolved value) */
typedef int (*sel_index_fn)(void *self, int index);
typedef int (*sel_sr_fn)(void *self, int srnum);
typedef void (*sel_void_fn)(void *self);

static void dump_row(const char *tag)
{
    struct node *n = cur_node(g_selection);
    unsigned i;
    log_utf8("NM_TEST_TAG ", tag);
    if (!n)
        return;
    log_hex("NM_TEST parent_sel=", (unsigned long)n->selected);
    log_hex("NM_TEST parent_state=", (unsigned long)n->state);
    log_utf8("NM_TEST value ", value_of(n, (unsigned)n->selected));
    for (i = 0; i < (unsigned)n->count && i < 4; i++) {
        struct node *c = plausible(n->children[i])
            ? (struct node *)n->children[i] : NULL;
        log_hex("NM_TEST child=", (unsigned long)i);
        if (c) {
            log_hex("NM_TEST  state=", (unsigned long)c->state);
            log_hex("NM_TEST  sel=", (unsigned long)c->selected);
        }
    }
}

static void dump_props(const char *tag)
{
    struct node *n = cur_node(g_selection);
    const struct props *pr;
    int i;

    log_utf8("NM_PROPS_TAG ", tag);
    if (!n || !n->properties)
        return;
    pr = (const struct props *)n->properties;
    if (!pr->recs || pr->count <= 0 || pr->count > 64)
        return;
    log_hex("NM_PROPS n=", (unsigned long)pr->count);
    for (i = 0; i < pr->count; i++) {
        log_hex("NM_PROP key=", (unsigned long)pr->recs[i].key);
        log_hex("NM_PROP kind=", (unsigned long)pr->recs[i].kind);
        log_hex("NM_PROP val=", (unsigned long)pr->recs[i].value);
    }
}

static void dump_node_props(const struct node *n, const char *tag)
{
    const struct props *pr;
    int i;

    log_utf8("NM_NP_TAG ", tag);
    if (!n || !n->properties)
        return;
    log_hex("NM_NP sel=", (unsigned long)n->selected);
    log_hex("NM_NP state=", (unsigned long)n->state);
    log_utf8("NM_NP txt ", str_for_resid(node_title_resid(n)));
    pr = (const struct props *)n->properties;
    if (!pr->recs || pr->count <= 0 || pr->count > 64)
        return;
    for (i = 0; i < pr->count; i++) {
        log_hex("NM_NP  key=", (unsigned long)pr->recs[i].key);
        log_hex("NM_NP   val=", (unsigned long)pr->recs[i].value);
    }
}

typedef int (*getint_fn)(void *self, int *out);
typedef int (*getint2_fn)(void *self, int a, int *out);
typedef int (*getint1_fn)(void *self, int a);

/* Report the node's SR bookkeeping through the firmware's own getters, so the
 * meaning of `selected` / child `state` is established rather than guessed. */
static void sr_report(const char *tag)
{
    struct node *n = cur_node(g_selection);
    getint_fn f_srsel;
    getint2_fn f_sridx, f_state;
    getint1_fn f_issel;
    int i, v;

    log_utf8("NM_SR_TAG ", tag);
    if (!n || !g_caution)
        return;
    f_srsel = (getint_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode20getSRNumSelectedItemERi");
    f_sridx = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    f_state = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getStateByIndexEiRi");
    f_issel = (getint1_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode21isItemSelectedByIndexEi");
    v = -12345;
    if (f_srsel) {
        int rc = f_srsel(n, &v);
        log_hex("NM_SR sel_rc=", (unsigned long)rc);
        log_hex("NM_SR sel_sr=", (unsigned long)v);
    }
    log_hex("NM_SR raw_selected=", (unsigned long)n->selected);
    for (i = 0; i < n->count && i < 6; i++) {
        int sr = -12345, st = -12345;
        log_hex("NM_SR idx=", (unsigned long)i);
        if (f_sridx) {
            int rc = f_sridx(n, i, &sr);
            log_hex("NM_SR  sr_rc=", (unsigned long)rc);
            log_hex("NM_SR  sr=", (unsigned long)sr);
        }
        if (f_state) {
            int rc = f_state(n, i, &st);
            log_hex("NM_SR  st_rc=", (unsigned long)rc);
            log_hex("NM_SR  st=", (unsigned long)st);
        }
        if (f_issel)
            log_hex("NM_SR  issel=", (unsigned long)f_issel(n, i));
        if (n->children && n->children[i])
            log_hex("NM_SR  node_state=",
                    (unsigned long)((struct node *)n->children[i])->state);
    }
}

static void self_test(void)
{
    getint2_fn f_sridx;
    unsigned row;

    out("NM_SELFTEST begin\n", 18);
    resolve_commit();
    if (!g_commit_sr) {
        out("NM_SELFTEST noapi\n", 18);
        return;
    }
    f_sridx = (getint2_fn)dlsym(g_caution,
        "_ZN18CmnViewSettingNode15getSRNumByIndexEiRi");
    if (!f_sridx) {
        out("NM_SELFTEST noget\n", 18);
        return;
    }
    for (row = 0; row < ROW_COUNT; row++) {
        struct node *n = cur_node(row);
        int srs[8], k = 0, i;
        if (!n || !n->children || n->count <= 0)
            continue;
        log_hex("NM_TEST row=", (unsigned long)row);
        log_hex("NM_TEST count=", (unsigned long)n->count);
        log_hex("NM_TEST sel=", (unsigned long)n->selected);
        for (i = 1; i <= n->count && i < 8; i++) {
            int sr = -1;
            int rc = f_sridx(n, i, &sr);
            log_hex("NM_TEST idx=", (unsigned long)i);
            log_hex("NM_TEST  sr_rc=", (unsigned long)rc);
            log_hex("NM_TEST  sr=", (unsigned long)sr);
            if (rc == 1 && sr > 0)
                srs[k++] = sr;
        }
        for (i = 0; i < k; i++) {
            if (srs[i] != (int)n->selected) {
                log_hex("NM_TEST set_sr=", (unsigned long)srs[i]);
                g_commit_sr(n, srs[i]);
                log_hex("NM_TEST sel_after=", (unsigned long)n->selected);
                log_utf8("NM_TEST val_after ", value_current(n));
                break;
            }
        }
    }
    out("NM_SELFTEST end\n", 16);
    g_settle_at = now_ms() / 1000u + 10u;
    /* after the settle report, exercise the touch path with two synthetic taps
     * on the selected row (tap = select, second tap = apply) */
    g_tap_stage = 1;
    g_tap_next = now_ms() / 1000u + 14u;
}

/* Report whether the committed selections survived (i.e. the firmware's own
 * setting state kept them) or reverted to the previous values. */
static void settle_report(void)
{
    unsigned r;

    out("NM_SETTLE begin\n", 16);
    for (r = 0; r < ROW_COUNT; r++) {
        struct node *n = cur_node(r);
        log_hex("NM_SETTLE row=", (unsigned long)r);
        if (n) {
            log_hex("NM_SETTLE sel=", (unsigned long)n->selected);
            log_utf8("NM_SETTLE val ", value_current(n));
        }
    }
}

/* --- present guard ---------------------------------------------------
 *
 * The app only rewrites the flip buffers when it presents, so a canary word is
 * enough to notice "we were overwritten": poll it cheaply and repaint at once
 * instead of waiting for the next full frame tick.
 */
#define FLIP_A 0x23A80000UL
#define FLIP_B 0x23B16000UL

static u16 *g_flip_va;
static u16 *g_flip_vb;
static unsigned g_flip_canary[2];
static int g_flip_ok;

static int flip_map(void)
{
    long m, fd;
    if (g_flip_ok)
        return 1;
    fd = sys3(SYS_open, (long)"/dev/mem", 2, 0);
    if (fd < 0) {
        out("NM_FLIPMAP nofd\n", 15);
        return 0;
    }
    m = sys4(SYS_mmap2, 0, 0x200000, 3, 1, fd, (long)(0x23A00000UL >> 12));
    if (m < 0 && m > -4096) {
        sys2(SYS_close, fd, 0);
        log_hex("NM_FLIPMAP fail=", (unsigned long)m);
        return 0;
    }
    /* one mapping covers 0x23A00000..0x23BFFFFF (both flip buffers) */
    g_flip_va = (u16 *)(m + (FLIP_A - 0x23A00000UL));
    g_flip_vb = (u16 *)(m + (FLIP_B - 0x23A00000UL));
    g_flip_ok = 1;
    log_hex("NM_FLIPMAP m=", (unsigned long)m);
    log_hex("NM_FLIPMAP a=", (unsigned long)g_flip_va);
    log_hex("NM_FLIPMAP va0=", (unsigned long)g_flip_va[0]);
    return 1;
}

static void flip_note(void)
{
    if (!g_flip_ok)
        return;
    g_flip_canary[0] = g_flip_va[0] | ((unsigned)g_flip_va[1] << 16);
    g_flip_canary[1] = g_flip_vb[0] | ((unsigned)g_flip_vb[1] << 16);
}

/* returns 1 and repaints when the app has presented over our frame */
static int flip_guard(void)
{
    unsigned a, b;

    if (!g_flip_ok)
        return 0;
    a = g_flip_va[0] | ((unsigned)g_flip_va[1] << 16);
    b = g_flip_vb[0] | ((unsigned)g_flip_vb[1] << 16);
    if (a == g_flip_canary[0] && b == g_flip_canary[1])
        return 0;
    /* The app only presents when *it* has something new to show.  While the
     * overlay owns the screen and nothing we just did explains a present, that
     * present means a native key was pressed -> act as "back". */
    if (g_open && (now_ms() - g_last_tap_ms) > 1500u) {
        if (g_page) {
            g_page = 0;
            g_last_tap_ms = now_ms();        /* absorb the follow-up frames */
            out("NM_NATIVE_BACK page\n", 21);
            g_dirty = 1;
        } else {
            overlay_close(0);              /* the app repaints itself */
            out("NM_NATIVE_BACK close\n", 22);
        }
        return 1;
    }
    out("NM_REPRESENT\n", 13);
    put_flip();
    flip_note();
    return 1;
}

/* --- switch button while the native menu is on screen ----------------
 *
 * The native UI keeps presenting its own frames, so the only place the
 * button can survive is the presented buffer itself.  It is a 44x44 patch
 * written straight into both flip buffers on every 20 ms beat; a present
 * can wipe it, the next beat puts it back (same mechanism as the present
 * guard for the full overlay).
 * ------------------------------------------------------------------ */

#define BADGE_W SW_BTN
#define BADGE_H SW_BTN
static u16 g_badge[BADGE_W * BADGE_H];
static int g_badge_ready;
static int g_badge_logged;

static void badge_make(void)
{
    int x, y;
    if (g_badge_ready)
        return;
    g_badge_ready = 1;
    for (y = 0; y < BADGE_H; y++)
        for (x = 0; x < BADGE_W; x++)
            g_lin[y * MENU_W + x] = 0x000f;
    draw_switch_icon(0, 0, 0);
    for (y = 0; y < BADGE_H; y++)
        for (x = 0; x < BADGE_W; x++)
            g_badge[y * BADGE_W + x] = g_lin[y * MENU_W + x];
}

/* Morton 8x8 tiled + row flipped layout, one rectangle only */
static void flip_put_rect(u16 *dst, int x0, int y0, int w, int h, const u16 *src)
{
    int x, y;
    for (y = 0; y < h; y++) {
        unsigned yy = MENU_H - 1u - (unsigned)(y0 + y);
        unsigned rowbase = (yy >> 3) * 80u;
        for (x = 0; x < w; x++) {
            unsigned xx = (unsigned)(x0 + x);
            unsigned z = (xx & 1u) | ((yy & 1u) << 1) | ((xx & 2u) << 1) |
                         ((yy & 2u) << 2) | ((xx & 4u) << 2) |
                         ((yy & 4u) << 3);
            dst[(rowbase + (xx >> 3)) * 64u + z] = src[y * w + x];
        }
    }
}

static int badge_enabled(void)
{
    return !file_exists("/setting/nm_nobadge");
}

/* Write (or refresh) the switch button into one 640x480 RGBA4444 surface,
 * Morton tiled and row flipped, touching only the button's pixels.  `verify`
 * skips the write when the region already holds the button, so a surface the
 * app repaints is only poked once per repaint instead of on every tick. */
static int panel_put_badge(unsigned long pa, int verify)
{
    const unsigned long base = pa & ~0xfffUL;
    const unsigned long off = pa & 0xfffUL;
    long m;
    int fd;
    u16 *fb;
    int x, y, differs = 0;

    fd = (int)sys3(SYS_open, (long)"/dev/mem", 2, 0);
    if (fd < 0)
        return -1;
    m = sys4(SYS_mmap2, 0, 0x98000, 3, 1, fd, (long)(base >> 12));
    if (m < 0 && m > -4096) {
        sys2(SYS_close, fd, 0);
        return -2;
    }
    fb = (u16 *)(m + off);
    if (verify) {
        for (y = 0; y < BADGE_H && !differs; y++) {
            unsigned yy = MENU_H - 1u - (unsigned)(SW_BTN_Y + y);
            unsigned rowbase = (yy >> 3) * 80u;
            for (x = 0; x < BADGE_W; x++) {
                unsigned xx = (unsigned)(SW_BTN_X + x);
                unsigned z = (xx & 1u) | ((yy & 1u) << 1) |
                             ((xx & 2u) << 1) | ((yy & 2u) << 2) |
                             ((xx & 4u) << 2) | ((yy & 4u) << 3);
                if (fb[(rowbase + (xx >> 3)) * 64u + z] !=
                    g_badge[y * BADGE_W + x]) {
                    differs = 1;
                    break;
                }
            }
        }
        if (!differs) {
            sys3(SYS_munmap, m, 0x98000, 0);
            sys2(SYS_close, fd, 0);
            return 1;
        }
    }
    for (y = 0; y < BADGE_H; y++) {
        unsigned yy = MENU_H - 1u - (unsigned)(SW_BTN_Y + y);
        unsigned rowbase = (yy >> 3) * 80u;
        for (x = 0; x < BADGE_W; x++) {
            unsigned xx = (unsigned)(SW_BTN_X + x);
            unsigned z = (xx & 1u) | ((yy & 1u) << 1) | ((xx & 2u) << 1) |
                         ((yy & 2u) << 2) | ((xx & 4u) << 2) |
                         ((yy & 4u) << 3);
            fb[(rowbase + (xx >> 3)) * 64u + z] = g_badge[y * BADGE_W + x];
        }
    }
    sys3(SYS_munmap, m, 0x98000, 0);
    sys2(SYS_close, fd, 0);
    return 0;
}

static void badge_tick(void)
{
    if (paint_paused())
        return;
    if (!badge_enabled())
        return;
    badge_make();
    /* the panel scans out the render target (0x202E0200): gate the write on
     * "the app has drawn over it", so a repaint is never raced */
    panel_put_badge(0x202E0200UL, 1);
    if (g_flip_ok) {
        flip_put_rect(g_flip_va, SW_BTN_X, SW_BTN_Y, BADGE_W, BADGE_H, g_badge);
        flip_put_rect(g_flip_vb, SW_BTN_X, SW_BTN_Y, BADGE_W, BADGE_H, g_badge);
    }
    if (!g_badge_logged) {
        g_badge_logged = 1;
        out("NM_BADGE on\n", 13);
    }
}

/* synthetic touch injection for testing without a panel:
 * the host writes "x y" into /tmp/nm_touch and then the key 'T' */
static int parse_int(const char **p)
{
    int v = 0, s = 1;
    while (**p && (**p < '0' || **p > '9') && **p != '-')
        (*p)++;
    if (**p == '-') {
        s = -1;
        (*p)++;
    }
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v * s;
}

static void touch_from_file(void)
{
    char buf[40];
    const char *p = buf;
    int fd, n, x, y;

    fd = (int)sys3(SYS_open, (long)"/tmp/nm_touch", 0, 0);
    if (fd < 0) {
        out("NM_TF nofd\n", 11);
        return;
    }
    n = (int)sys3(SYS_read, fd, (long)buf, 39);
    sys2(SYS_close, fd, 0);
    sys3(SYS_unlink, (long)"/tmp/nm_touch", 0, 0);
    if (n <= 0)
        return;
    buf[n < 39 ? n : 39] = 0;
    x = parse_int(&p);
    y = parse_int(&p);
    log_hex("NM_TF x=", (unsigned long)x);
    log_hex("NM_TF y=", (unsigned long)y);
    handle_touch(x, y);
}

static void poll_key(void)
{
    char buf[8];
    int fd = (int)sys3(SYS_open, (long)"/tmp/nm_key", 0, 0);
    int n;
    if (fd < 0)
        return;
    n = (int)sys3(SYS_read, fd, (long)buf, 7);
    sys2(SYS_close, fd, 0);
    log_hex("NM_KEYREAD n=", (unsigned long)n);
    if (n <= 0) {
        /* The writer may have created the file but not written it yet; do NOT
         * unlink an empty file or the write lands in a deleted inode and the
         * key is lost.  Retry on the next tick. */
        return;
    }
    sys3(SYS_unlink, (long)"/tmp/nm_key", 0, 0);
    switch (buf[0]) {
    case 'u':
        if (g_open && g_page)
            popup_move(-1);
        else
            g_selection = (g_selection + ROW_COUNT - 1u) % ROW_COUNT;
        g_dirty = 1;
        break;
    case 'd':
        if (g_open && g_page)
            popup_move(1);
        else
            g_selection = (g_selection + 1u) % ROW_COUNT;
        g_dirty = 1;
        break;
    case 'c':
        g_category = (g_category + 1u) & 7u;
        g_dirty = 1;
        break;
    case 'e':                      /* apply: step the value forward */
        if (g_open && g_page)
            popup_apply(g_opt_hover);
        else
            commit_step(1);
        g_dirty = 1;
        break;
    case 'p':                      /* apply: step the value back */
        commit_step(-1);
        g_dirty = 1;
        break;
    case 'n':                      /* open the second level of this row */
        if (g_open) {
            if (g_page)
                popup_apply(g_opt_hover);
            else
                popup_open();
            g_dirty = 1;
        }
        break;
    case 'b':                      /* MENU = back */
        nm_back();
        break;
    case 'T':                      /* synthetic touch from /tmp/nm_touch */
        touch_from_file();
        break;
    case 'l':                      /* dump the option list of this row */
        list_children();
        break;
    case 't':                      /* synthetic tap on the selected row */
        simulate_tap((int)g_selection);
        break;
    case 's':                      /* run the option/commit self test */
        self_test();
        break;
    case 'x':                      /* close the overlay: stop painting */
        overlay_close(1);
        out("NM_CLOSE\n", 9);
        break;
    case 'o':                      /* re-open the overlay */
        overlay_open();
        break;
    default:
        break;
    }
    log_hex("NM_KEY=", (unsigned long)buf[0]);
}

static void log_painted(void);

static void render_and_send(void)
{
    static const char hex[] = "0123456789abcdef";
    build_frame();
    if (g_frames == 0) {
        int fd = (int)sys3(SYS_open, (long)"/tmp/menu_lin.raw", 0x241, 0644);
        if (fd >= 0) {
            long off = 0;
            long total = (long)(MENU_W * MENU_H * 2);
            while (off < total) {
                long w = sys3(SYS_write, fd, (long)((char *)g_lin + off),
                              total - off);
                if (w <= 0)
                    break;
                off += w;
            }
            sys2(SYS_close, fd, 0);
            log_hex("NM_LIN_DUMP=", (unsigned long)off);
        }
    }
    {
        int rc;
        if (g_present_ready) {
            /* present hook active: the wrapper drops this frame into the surface
             * the next submit blits from, so no direct panel write is needed */
            g_frames++;
            log_painted();
            return;
        }
        put_flip();
        rc = put_panel();
        if (rc == -3) {
            /* native UI repainted; wait for it to go idle instead of fighting */
            g_busy_ticks++;
            return;
        }
        if (rc != 0) {
            out("NM_PANEL_FAIL\n", 14);
            return;
        }
    }
    g_frames++;
    log_painted();
}

/* "NM_OVERLAY c=<category> s=<selection>" every 5th frame; also what the run
 * script watches for before it injects keys. */
static void log_painted(void)
{
    static const char hex[] = "0123456789abcdef";
    char b[28];
    int n = 0;

    if ((g_frames % 5) != 1)
        return;
    b[n++] = 'N'; b[n++] = 'M'; b[n++] = '_'; b[n++] = 'O';
    b[n++] = 'V'; b[n++] = 'E'; b[n++] = 'R'; b[n++] = 'L';
    b[n++] = 'A'; b[n++] = 'Y'; b[n++] = ' ';
    b[n++] = 'c'; b[n++] = '='; b[n++] = hex[g_category & 7u];
    b[n++] = ' '; b[n++] = 's'; b[n++] = '=';
    b[n++] = hex[g_selection & 0xfu];
    b[n++] = '\n';
    out(b, n);
}


/* ------------------------------------------------------------------ */
/* PRESENT_HOOK_BLOCK                                                  */
/*                                                                     */
/* Every frame is submitted through libObj+0x719510:                   */
/*   movw r1,#0x8211 / movt r1,#0xc004  (ioctl CMD_WAIT_A)            */
/*   str r6,[r7,#4] ; blx ioctl ; retry while the slot is not consumed */
/* Writing our frame into the surface that job blits from wins by      */
/* construction: the GPU copies it to the panel in the same job.       */
/* ------------------------------------------------------------------ */

#define SYS_mprotect2 125
#define SYS_cacheflush2 0x0f0002
#define PROT_RWX2 7
#define PROT_RX2 5

typedef long (*submit_fn)(long);

static submit_fn g_submit_orig;
static int g_present_tried;
static void *g_surface;          /* linear 640x480 RGBA4444 surface */
static int g_present_hits;

/* --- /proc/self/maps, so we never dereference a stale pointer --- */
#define PM_RNG_MAX 512
struct pm_rng {
    unsigned long lo, hi;
    int writable;
};
static struct pm_rng g_rng[PM_RNG_MAX];
static int g_rng_n;
static int g_rng_loaded;

static unsigned long pm_hex(const char **s)
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

static void pm_load_maps(void)
{
    static char buf[65536];
    int fd, n;
    const char *p, *end;

    if (g_rng_loaded)
        return;
    g_rng_loaded = 1;
    fd = (int)sys3(SYS_open, (long)"/proc/self/maps", 0, 0);
    if (fd < 0)
        return;
    n = 0;
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
    p = buf;
    end = buf + n;
    while (p < end && g_rng_n < PM_RNG_MAX) {
        const char *line = p, *next, *q;
        unsigned long lo, hi;
        while (p < end && *p != '\n')
            p++;
        next = (p < end) ? p + 1 : end;
        q = line;
        lo = pm_hex(&q);
        if (q < p && *q == '-') {
            q++;
            hi = pm_hex(&q);
            if (q + 2 < p && q[0] == ' ' && q[1] == 'r' && hi > lo) {
                g_rng[g_rng_n].lo = lo;
                g_rng[g_rng_n].hi = hi;
                g_rng[g_rng_n].writable = (q[2] == 'w');
                g_rng_n++;
            }
        }
        p = next;
    }
}

static int pm_readable(const void *p, unsigned len)
{
    unsigned long a = (unsigned long)p, b = a + len;
    int i;
    if (!g_rng_loaded)
        pm_load_maps();
    for (i = 0; i < g_rng_n; i++)
        if (a >= g_rng[i].lo && b <= g_rng[i].hi && b > a)
            return 1;
    return 0;
}

static unsigned pm_rd32(const void *p)
{
    unsigned v;
    if (!p || !pm_readable(p, 4))
        return 0;
    __builtin_memcpy(&v, p, 4);
    return v;
}

/* find a GRM bitmap: +0x04 geo, +0x08 pixels, geo+0x18 stride, geo+0x1C fmt */
static void pm_scan(const void *base, unsigned span)
{
    unsigned i;
    if (!pm_readable(base, span))
        return;
    for (i = 0; i + 4 <= span && !g_surface; i += 4) {
        unsigned p = pm_rd32((const char *)base + i);
        unsigned g, px, stride, fmt;
        if (!pm_readable((void *)p, 0x20))
            continue;
        g = pm_rd32((const char *)p + 4);
        px = pm_rd32((const char *)p + 8);
        if (!pm_readable((void *)g, 0x24) || !pm_readable((void *)px, 0x40))
            continue;
        stride = *(const unsigned short *)((const char *)g + 0x18);
        fmt = pm_rd32((const char *)g + 0x1c);
        if (stride != 640 || fmt < 1 || fmt > 9)
            continue;
        if (!pm_readable((void *)px, 640u * 480u * 2u))
            continue;
        g_surface = (void *)px;
        log_hex("NM_SURFACE=", (unsigned long)px);
    }
}

typedef void *(*eng_inst_fn)(void);
typedef void *(*dlopen_fn2)(const char *, int);

static void present_wrapper_args(long a);
static long present_wrapper(long a, long b, long c, long d);

static void find_surface(void)
{
    dlopen_fn2 dop = (dlopen_fn2)dlsym(RTLD_DEFAULT, "dlopen");
    void *obj;
    eng_inst_fn inst;
    void *eng;
    int i;

    if (g_surface || !dop)
        return;
    obj = dop("/usr/lib/libObj.so", 5);
    log_hex("NM_obj2=", (unsigned long)obj);
    if (!obj)
        return;
    inst = (eng_inst_fn)dlsym(obj, "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    eng = inst ? inst() : NULL;
    log_hex("NM_eng=", (unsigned long)eng);
    if (!eng)
        return;
    pm_load_maps();
    for (i = 0; i < g_rng_n && !g_surface; i++) {
        if (eng < (void *)g_rng[i].lo || eng >= (void *)g_rng[i].hi)
            continue;
        pm_scan((void *)g_rng[i].lo,
                (unsigned)(g_rng[i].hi - g_rng[i].lo));
    }
}

static void icache(const void *s, unsigned long n)
{
    sys3(SYS_cacheflush2, (long)s, (long)s + (long)n, 0);
}

static int wide16b(unsigned hw)
{
    return (hw & 0xF800u) == 0xE800u || (hw & 0xF800u) == 0xF000u;
}

static int hook_thumb2(unsigned long target, void *repl, void **orig)
{
    unsigned char *code;
    void *page;
    unsigned i;
    unsigned short *addr = (unsigned short *)target;

    if (!target || (target & 3u))
        return -1;
    for (i = 0; i < 4; i++)
        if (wide16b(addr[i]))
            return -2;
    code = (unsigned char *)sys4(SYS_mmap2, 0, 4096, PROT_RWX2, 0x22, -1, 0);
    if ((long)code < 0 && (long)code > -4096)
        return -3;
    for (i = 0; i < 8; i++)
        code[i] = ((unsigned char *)target)[i];
    code[8] = 0xdf; code[9] = 0xf8; code[10] = 0x00; code[11] = 0xf0;
    *(unsigned *)(code + 12) = (unsigned)(target + 8u) | 1u;
    icache(code, 32);
    *orig = (void *)((unsigned long)code | 1u);
    page = (void *)(target & ~4095UL);
    if (sys3(SYS_mprotect2, (long)page, 8192u, PROT_RWX2) < 0)
        return -5;
    {
        unsigned short *p = (unsigned short *)target;
        p[0] = 0xdf; p[1] = 0xf8; p[2] = 0x00; p[3] = 0xf0;
        *(unsigned *)(target + 4) = (unsigned)(unsigned long)repl;
    }
    icache((void *)page, 8192);
    sys3(SYS_mprotect2, (long)page, 8192u, PROT_RX2);
    return 0;
}

/* Copy our frame into the surface the job blits from, then submit as usual. */
static int pm_writable(const void *p, unsigned len)
{
    unsigned long a = (unsigned long)p, b = a + len;
    int i;
    if (!g_rng_loaded)
        pm_load_maps();
    for (i = 0; i < g_rng_n; i++)
        if (g_rng[i].writable && a >= g_rng[i].lo && b <= g_rng[i].hi && b > a)
            return 1;
    return 0;
}

static void present_wrapper_args(long a)
{
    /* Observation build: log the submitted descriptor and do not touch any
     * surface until we know which surface the job actually uses. */
    if (g_present_hits < 24) {
        g_present_hits++;
        log_hex("NM_PRESENT a=", (unsigned long)a);
        if (pm_readable((const void *)a, 32)) {
            const unsigned *w = (const unsigned *)a;
            log_hex("NM_PRESENT w0=", w[0]);
            log_hex("NM_PRESENT w1=", w[1]);
            log_hex("NM_PRESENT w2=", w[2]);
            log_hex("NM_PRESENT w3=", w[3]);
            log_hex("NM_PRESENT w4=", w[4]);
            log_hex("NM_PRESENT w5=", w[5]);
        }
    }
}

static long present_wrapper(long a, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    present_wrapper_args(a);
    return g_submit_orig(a);
}

static void install_present_hook(void)
{
    void *obj;
    dlopen_fn2 dop = (dlopen_fn2)dlsym(RTLD_DEFAULT, "dlopen");
    void *fn;
    void *orig = NULL;
    unsigned long base;
    int rc;

    if (g_present_tried)
        return;
    g_present_tried = 1;
    if (!dop)
        return;
    obj = dop("/usr/lib/libObj.so", 5);
    if (!obj)
        return;
    fn = dlsym(obj, "GRM_bitmapCreate");
    if (!fn)
        return;
    base = (unsigned long)fn - 0x636bd9UL;    /* GRM_bitmapCreate file vaddr */
    log_hex("NM_objbase=", base);
    rc = hook_thumb2(base + 0x719510UL, (void *)present_wrapper, &orig);
    log_hex("NM_present_rc=", (unsigned long)rc);
    if (rc == 0) {
        g_submit_orig = (submit_fn)orig;
        g_present_ready = 1;
        /* read the patched bytes back: proves the write really landed */
        {
            const unsigned char *b = (const unsigned char *)(base + 0x719510UL);
            unsigned w;
            __builtin_memcpy(&w, b, 4);
            log_hex("NM_patch_verify=", w);
            log_hex("NM_patch_tgt=", (unsigned long)w);
            log_hex("NM_patch_addr=", (unsigned long)(base + 0x719510UL));
            log_hex("NM_wrapper=", (unsigned long)(void *)present_wrapper);
        }
    }
}

static void *worker(void *arg)
{
    struct timespec32 ts;
    unsigned beat = 0;

    (void)arg;
    ts.tv_sec = 0;
    ts.tv_nsec = 20000000;             /* 20 ms: cheap present guard */
    for (;;) {
        long now;
        sys2(SYS_nanosleep, (long)&ts, 0);
        beat++;
        if ((beat % 5) != 0) {
            /* between full frames: watch for a present overwriting us and poll
             * the touch position (two words, cheap) so quick taps are caught */
            if (g_first_paint) {
                poll_touch();
                if (g_open) {
                    if (g_dirty && !paint_paused()) {
                        g_dirty = 0;
                        render_and_send();
                        flip_note();
                    } else if (!paint_paused()) {
                        flip_guard();
                    }
                } else if (!paint_paused()) {
                    badge_tick();
                }
            }
            continue;
        }
        if (!file_exists("/setting/newmenu"))
            continue;
        now = mono_sec();
        /* Install the present hook while the UI is still quiet (boot idle),
         * so we never patch code a render thread is executing. */
        if (file_exists("/setting/nm_present") && now >= 60 && !g_submit_orig) {
            find_surface();
            install_present_hook();
        }
        if (now < 60)
            continue;
        if (g_first_paint == 0) {
            g_first_paint = now;
            log_hex("NM_FIRST_PAINT=", (unsigned long)now);
            resolve_nodes();
            flip_map();
            /* a key file left over from a previous run must not fire now */
            sys3(SYS_unlink, (long)"/tmp/nm_key", 0, 0);
            sys3(SYS_unlink, (long)"/tmp/nm_key.tmp", 0, 0);
        }
        poll_key();
        if (!g_open) {
            /* closed: keep watching the touch cell for the switch-button tap,
             * and keep the corner button painted over the native frames */
            poll_touch();
            badge_tick();
            continue;
        }
        if (paint_paused()) {
            poll_touch();
            continue;
        }
        poll_touch();
        if (!g_selftest_done && now >= 75 && file_exists("/setting/nm_selftest")) {
            g_selftest_done = 1;
            self_test();
        }
        if (g_settle_at && !g_settle_done && now >= (long)g_settle_at) {
            g_settle_done = 1;
            settle_report();
        }
        if (g_tap_stage && now >= (long)g_tap_next) {
            out("NM_TAPTEST\n", 11);
            simulate_tap((int)g_selection);
            if (g_tap_stage == 1) {
                g_tap_stage = 2;
                g_tap_next = g_tap_next + 1u;
            } else {
                g_tap_stage = 0;
            }
        }
        g_dirty = 0;
        render_and_send();
        flip_note();
    }
    return NULL;
}

typedef int (*pthread_create_fn)(unsigned long *, const void *,
                                 void *(*)(void *), void *);

__attribute__((constructor)) static void menu_overlay_init(void)
{
    pthread_create_fn create;
    unsigned long tid = 0;

    if (!comm_is_im())
        return;
    out("NM_ALIVE\n", 9);
    create = (pthread_create_fn)dlsym(RTLD_DEFAULT, "pthread_create");
    if (!create) {
        out("NM_NO_PTHREAD\n", 14);
        return;
    }
    if (create(&tid, NULL, worker, NULL) == 0)
        log_hex("NM_worker_tid=", tid);
    else
        out("NM_WORKER_FAIL\n", 15);
}






