/* edge_app.c — "Edge" mobile POC for NTOS, driven by uXAML markup (v3 ref).
 *
 * Reconstructs the Windows 10 Mobile Edge chrome with pixel measurements
 * taken from the real-device captures in refs/edge-screens/
 * (edge/analysis/EDGE_UI_REFERENCE.md): bottom #1F1F1F address bar, the
 * "..." actions sheet over a #7F7F7F scrim, the tab-center 2-column card
 * grid on black, the fullscreen hub with pivot icons, Clear-browsing-data
 * checkboxes, the reading-list dialog and the address editor with the
 * shared FINGERKB.DLL keyboard.
 *
 * Component reuse (the W10M shared-DLL model):
 *   - keyboard: loaded as a separate module; the host loader snaps our
 *     __imp_UIXGetSchemaFactory slot to fingerkb.dll's export, and its
 *     render/svf imports snap back to the exports below
 *   - controls + SVF icons: payload/phone/uixctl.c and svf.c compile into
 *     this DLL and paint through the Render vtable below (dark theme)
 *
 * The "web engine" is a deterministic mock: a Wikipedia-like article for
 * wikipedia hosts, a white page otherwise, the InPrivate page for private
 * empty tabs. No network, no HTML parser, no Chakra (pure stub DLL).
 */
#include "nt_app.h"
#include "uxaml.h"
#include "icons.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

/* ---- shared phone-framework ABI (mirror of payload/phone/phone.h) ----
 * phone.h cannot be included here (nt_app.h redefines the NT_IMPORT
 * macros), so the two vtables the app hosts are mirrored 1:1. They MUST
 * stay in sync with payload/phone/phone.h. */
typedef struct EdgeRender {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme; /* UIX_TH_* table (see phone.h) */
} EdgeRender;

typedef struct EdgeKb {
    U32 profile; /* 0x55495831 "UIX1" */
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
    void (*context)(int); /* go-key input scope: 0 arrow, 1 search */
} EdgeKb;

/* shell chrome (SHELLCHROME.DLL) mirror of phone.h Chrome: nav bar */
typedef struct EdgeChrome {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*expanded)(void);
    int (*navheight)(void);
    int (*navigation)(int);
} EdgeChrome;

/* shared control painters (payload/phone/uixctl.c, linked into this DLL) */
void ctl_button(void *dc, const EdgeRender *r, int x, int y, int w, int h,
                const char *label, int pressed);
void ctl_checkbox(void *dc, const EdgeRender *r, int x, int y, int checked,
                  const char *label);
void ctl_toggle(void *dc, const EdgeRender *r, int x, int y, int on);
void ctl_field(void *dc, const EdgeRender *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused);
/* SVF icon draw (payload/phone/svf.c, linked into this DLL) */
int SVFDrawTint(void *dc, const void *data, unsigned length, int x, int y,
                int w, int h, U32 tint);

/* IAT slot owned by this object: uixctl.c/svf.c reference it as extern
 * (compiled with -DNT_IAT_EXTERN_ONLY) and the loader cannot snap a
 * module's own export, so RunApplication initialises it manually. */
NT_DEFINE(render_SpGetThreadContext)

/* keyboard factory: imported from FINGERKB.DLL via the host loader */
NT_IMPORT(const EdgeKb *, UIXGetSchemaFactory, (void))
NT_DEFINE(UIXGetSchemaFactory)
/* nav-bar chrome: imported from SHELLCHROME.DLL (UIXChromeFactory is the
 * hosted-app surface: nav bar only, themed through edge_renderer) */
NT_IMPORT(const EdgeChrome *, UIXChromeFactory, (void))
NT_DEFINE(UIXChromeFactory)

/* ---- imports (host loader or device COREDLL snap these) ---- */
typedef int (*WndProcT)(void *, U32, U32, U32);
typedef struct {
    U32 style;
    WndProcT proc;
    int cls_extra, win_extra;
    void *instance, *icon, *cursor, *background;
    const W16 *menu, *name;
} WndClass;
typedef struct {
    void *hwnd;
    U32 message, wparam, lparam;
    U32 time;
    Pt pt;
    void *next;
} Msg;
typedef struct {
    void *hdc;
    int erase;
    Rect area;
    int restore, inc;
    unsigned char reserved[32];
} Paint;
typedef struct {
    U32 size, flags;
    Rect desktop, sip;
    U32 data_size;
    void *data;
} SipInfo;

NT_IMPORT(U32, RegisterClassW, (const void *))
NT_DEFINE(RegisterClassW)
NT_IMPORT(void *, CreateWindowExW, (U32, const W16 *, const W16 *, U32, int, int, int, int, void *, void *, void *, void *))
NT_DEFINE(CreateWindowExW)
NT_IMPORT(int, ShowWindow, (void *, int))
NT_DEFINE(ShowWindow)
NT_IMPORT(int, UpdateWindow, (void *))
NT_DEFINE(UpdateWindow)
NT_IMPORT(int, InvalidateRect, (void *, const Rect *, int))
NT_DEFINE(InvalidateRect)
NT_IMPORT(int, GetMessageW, (void *, void *, U32, U32))
NT_DEFINE(GetMessageW)
NT_IMPORT(int, TranslateMessage, (const void *))
NT_DEFINE(TranslateMessage)
NT_IMPORT(int, DispatchMessageW, (const void *))
NT_DEFINE(DispatchMessageW)
NT_IMPORT(void *, BeginPaint, (void *, void *))
NT_DEFINE(BeginPaint)
NT_IMPORT(int, EndPaint, (void *, const void *))
NT_DEFINE(EndPaint)
NT_IMPORT(void *, CreateSolidBrush, (U32))
NT_DEFINE(CreateSolidBrush)
NT_IMPORT(int, FillRect, (void *, const Rect *, void *))
NT_DEFINE(FillRect)
NT_IMPORT(int, DeleteObject, (void *))
NT_DEFINE(DeleteObject)
NT_IMPORT(void, PostQuitMessage, (int))
NT_DEFINE(PostQuitMessage)
NT_IMPORT(void *, CreateFileW, (const W16 *, U32, U32, void *, U32, U32, void *))
NT_DEFINE(CreateFileW)
NT_IMPORT(int, ReadFile, (void *, void *, U32, U32 *, void *))
NT_DEFINE(ReadFile)
NT_IMPORT(int, CloseHandle, (void *))
NT_DEFINE(CloseHandle)
NT_IMPORT(U32, GetFileSize, (void *, U32 *))
NT_DEFINE(GetFileSize)
NT_IMPORT(int, SipShowIM, (U32))
NT_DEFINE(SipShowIM)
NT_IMPORT(int, SipGetInfo, (SipInfo *))
NT_DEFINE(SipGetInfo)

/* ---- palette (measured from refs/edge-screens, dark theme) ---- */
#define C_BAR    RGB_(0x1F, 0x1F, 0x1F) /* address bar, icon bar, kb bg  */
#define C_SHEET  RGB_(0x2B, 0x2B, 0x2B) /* menu sheets, flyouts          */
#define C_PAGE   RGB_(0x00, 0x00, 0x00) /* tabs/hub/settings pages       */
#define C_CARD   RGB_(0x33, 0x33, 0x33) /* Clear button, preview areas   */
#define C_BTN    RGB_(0x2E, 0x2E, 0x2E) /* dialog buttons                */
#define C_SEP    RGB_(0x76, 0x76, 0x76) /* address-bar separators        */
#define C_INK    RGB_(0xFF, 0xFF, 0xFF)
#define C_MUT    RGB_(0x9A, 0x9A, 0x9A)
#define C_DIM    RGB_(0x6E, 0x6E, 0x6E)
#define C_DIS    RGB_(0x4C, 0x4C, 0x4C) /* disabled reader icon          */
#define C_BLUE   RGB_(0x00, 0x78, 0xD7)
#define C_LINK   RGB_(0x00, 0x9B, 0xE4) /* InPrivate page links          */
#define C_SCRIM  RGB_(0x7F, 0x7F, 0x7F) /* 50% dim over white content    */
#define C_WLINK  RGB_(0x33, 0x66, 0xCC) /* links inside mock web pages   */
#define C_PAGEBG RGB_(0xFF, 0xFF, 0xFF) /* mock web page background      */

/* ---- tiny utils (no CRT on device) ---- */
static int xstrlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        ++n;
    return n;
}
static void xcopy(char *d, const char *s, int n)
{
    for (int i = 0; i < n; ++i)
        d[i] = s[i];
}
static int xeq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    int i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == 0 && b[i] == 0;
}
static unsigned xhash(const char *s)
{
    unsigned h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* ---- drawing (GDI FillRect backend, brush cache) ---- */
static U32 cached_color;
static void *cached_brush;
static Rect clip_rect = {0, 0, 320, 528};

static void clip_set(int l, int t, int r, int b)
{
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > 320) r = 320;
    if (b > 528) b = 528;
    clip_rect.left = l;
    clip_rect.top = t;
    clip_rect.right = r > l ? r : l;
    clip_rect.bottom = b > t ? b : t;
}

static void fill(void *dc, int l, int t, int r, int b, U32 c)
{
    Rect v;
    v.left = l > clip_rect.left ? l : clip_rect.left;
    v.top = t > clip_rect.top ? t : clip_rect.top;
    v.right = r < clip_rect.right ? r : clip_rect.right;
    v.bottom = b < clip_rect.bottom ? b : clip_rect.bottom;
    if (!dc || v.left >= v.right || v.top >= v.bottom)
        return;
    if (!cached_brush || c != cached_color) {
        if (cached_brush)
            NT_CALL(DeleteObject, (cached_brush));
        cached_brush = NT_CALL(CreateSolidBrush, (c));
        cached_color = c;
    }
    if (cached_brush)
        NT_CALL(FillRect, (dc, &v, cached_brush));
}

/* IconFillFn adapter for icons.c */
static void ifill(void *dc, int l, int t, int r, int b, unsigned c)
{
    fill(dc, l, t, r, b, c);
}

static int isqrt_(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n)
        ++r;
    return r;
}

static void fill_round(void *dc, int l, int t, int r, int b, U32 c, int rad)
{
    if (rad <= 0) {
        fill(dc, l, t, r, b, c);
        return;
    }
    int w = r - l, h = b - t;
    if (rad * 2 > w)
        rad = w / 2;
    if (rad * 2 > h)
        rad = h / 2;
    if (rad <= 0) {
        fill(dc, l, t, r, b, c);
        return;
    }
    fill(dc, l + rad, t, r - rad, b, c);
    fill(dc, l, t + rad, l + rad, b - rad, c);
    fill(dc, r - rad, t + rad, r, b - rad, c);
    for (int i = 0; i < rad; ++i) {
        int dx = isqrt_(rad * rad - (rad - i) * (rad - i));
        int xo = rad - dx;
        fill(dc, l + xo, t + i, l + rad, t + i + 1, c);
        fill(dc, r - rad, t + i, r - xo, t + i + 1, c);
        fill(dc, l + xo, b - i - 1, l + rad, b - i, c);
        fill(dc, r - rad, b - i - 1, r - xo, b - i, c);
    }
}

/* ---- text (baked 1-bpp fonts) ---- */
static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[0];
    int bd = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        int d = f->height - h;
        if (d < 0)
            d = -d;
        if (d < bd) {
            best = f;
            bd = d;
        }
    }
    return best;
}

static int text_w(const char *s, int h)
{
    const PhoneFont *f = pick_font(h);
    int w = 0;
    for (; s && *s; ++s) {
        int ch = (unsigned char)*s;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}

static void draw_text1(void *dc, int x, int y, const char *s, U32 c, int h, int align)
{
    if (!s || !*s)
        return;
    int w = text_w(s, h);
    if (align == 1)
        x -= w / 2;
    else if (align == 2)
        x -= w;
    const PhoneFont *f = pick_font(h);
    for (; *s; ++s) {
        int ch = (unsigned char)*s;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        const PhoneGlyph *g = &f->glyphs[ch - f->first];
        unsigned stride = ((unsigned)g->width + 7u) >> 3;
        for (int yy = 0; yy < f->height; ++yy) {
            const unsigned char *row = &f->bits[g->offset + (unsigned)yy * stride];
            int run = -1;
            for (int xx = 0; xx <= g->width; ++xx) {
                int bit = xx < g->width && ((row[(unsigned)xx >> 3] >> (xx & 7)) & 1u);
                if (bit && run < 0)
                    run = xx;
                else if (!bit && run >= 0) {
                    fill(dc, x + run, y + yy, x + xx, y + yy + 1, c);
                    run = -1;
                }
            }
        }
        x += g->advance + 1;
    }
}

static void draw_text(void *dc, int x, int y, const char *s, U32 c, int h,
                      int align, int bold)
{
    draw_text1(dc, x, y, s, c, h, align);
    if (bold)
        draw_text1(dc, x + 1, y, s, c, h, align);
}

/* truncate to pixel width, drawing "..." if cut */
static void draw_text_clamp(void *dc, int x, int y, const char *s, U32 c, int h,
                            int maxw)
{
    if (text_w(s, h) <= maxw) {
        draw_text(dc, x, y, s, c, h, 0, 0);
        return;
    }
    const PhoneFont *f = pick_font(h);
    int w = 0, n = 0;
    while (s[n]) {
        int ch = (unsigned char)s[n];
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        int adv = f->glyphs[ch - f->first].advance + 1;
        if (w + adv * 2 > maxw)
            break;
        w += adv;
        ++n;
    }
    char buf[48];
    if (n > 44)
        n = 44;
    xcopy(buf, s, n);
    buf[n] = 0;
    draw_text(dc, x, y, buf, c, h, 0, 0);
    draw_text(dc, x + w, y, "...", c, h, 0, 0);
}

/* ---- shared-component render context (dark Edge theme) ----
 * Serves uixctl.c (controls), svf.c (icons) and fingerkb.dll (keyboard):
 * the same vtable model as uixrender.dll, with the dark UIX_TH table. */
static const U32 edge_theme[] = {
    RGB_(0x1F, 0x1F, 0x1F), /* 0 SURFACE   keyboard backdrop        */
    RGB_(0x33, 0x33, 0x33), /* 1 KEY        normal key             */
    RGB_(0x4C, 0x4C, 0x4C), /* 2 KEY_ALT    shift/&123             */
    RGB_(0xFF, 0xFF, 0xFF), /* 3 INK        key text               */
    RGB_(0xFF, 0xFF, 0xFF), /* 4 KEY_GO     enter/search key fill  */
    RGB_(0x00, 0x00, 0x00), /* 5 GO_INK     glyph on the go key    */
    RGB_(0x00, 0x78, 0xD7), /* 6 ACCENT     checked/caret/links    */
    RGB_(0x33, 0x33, 0x33), /* 7 CTRL_BG    button fill            */
    RGB_(0xFF, 0xFF, 0xFF), /* 8 CTRL_INK   button label           */
    RGB_(0xCC, 0xCC, 0xCC), /* 9 CTRL_EDGE  unchecked border       */
    RGB_(0xFF, 0xFF, 0xFF), /* 10 FIELD_EDGE input border/hint     */
    RGB_(0x00, 0x00, 0x00), /* 11 NAV_BG    nav bar strip (black)  */
    RGB_(0xFF, 0xFF, 0xFF), /* 12 NAV_INK   nav bar glyphs (white) */
};

static void r_clip(int l, int t, int r, int b)
{
    clip_set(l, t, r, b);
}
static void r_fill(void *dc, int l, int t, int r, int b, U32 c)
{
    fill(dc, l, t, r, b, c);
}
static void r_text(void *dc, int x, int y, const char *s, U32 c, int h)
{
    draw_text(dc, x, y, s, c, h, 0, 0);
}
static void r_line(void *dc, int x, int y, int xx, int yy, U32 c)
{
    if (y == yy) {
        fill(dc, x < xx ? x : xx, y, (x > xx ? x : xx) + 1, y + 1, c);
        return;
    }
    if (x == xx) {
        fill(dc, x, y < yy ? y : yy, x + 1, (y > yy ? y : yy) + 1, c);
        return;
    }
    int dx = xx > x ? xx - x : x - xx, sx = x < xx ? 1 : -1;
    int dy = yy > y ? y - yy : yy - y, sy = y < yy ? 1 : -1, e = dx + dy;
    for (;;) {
        fill(dc, x, y, x + 1, y + 1, c);
        if (x == xx && y == yy)
            break;
        int e2 = e * 2;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
}
static void r_ellipse(void *dc, int cx, int cy, int rx, int ry, U32 c, int thick)
{
    if (rx < 1 || ry < 1 || rx > 100 || ry > 100)
        return;
    int ix = rx - thick, iy = ry - thick;
    for (int y = -ry; y <= ry; ++y) {
        int run = -999;
        for (int x = -rx; x <= rx + 1; ++x) {
            int in = x <= rx && x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry;
            if (thick > 0 && ix > 0 && iy > 0 &&
                x * x * iy * iy + y * y * ix * ix < ix * ix * iy * iy)
                in = 0;
            if (in && run == -999)
                run = x;
            if (!in && run != -999) {
                fill(dc, cx + run, cy + y, cx + x, cy + y + 1, c);
                run = -999;
            }
        }
    }
}
static void r_polygon(void *dc, const Pt *p, unsigned n, U32 c)
{
    if (!p || n < 3 || n > 255)
        return;
    int ymin = p[0].y, ymax = ymin;
    for (unsigned i = 1; i < n; ++i) {
        if (p[i].y < ymin)
            ymin = p[i].y;
        if (p[i].y > ymax)
            ymax = p[i].y;
    }
    if (ymin < clip_rect.top)
        ymin = clip_rect.top;
    if (ymax > clip_rect.bottom)
        ymax = clip_rect.bottom;
    for (int y = ymin; y < ymax; ++y) {
        int xs[255];
        unsigned used = 0;
        for (unsigned i = 0, j = n - 1; i < n; j = i++)
            if ((p[i].y <= y && p[j].y > y) || (p[j].y <= y && p[i].y > y)) {
                int num = (y - p[i].y) * (p[j].x - p[i].x);
                int den = p[j].y - p[i].y;
                int vx = p[i].x + num / den;
                xs[used++] = vx;
            }
        for (unsigned i = 1; i < used; ++i) {
            int v = xs[i];
            unsigned j = i;
            while (j && xs[j - 1] > v) {
                xs[j] = xs[j - 1];
                --j;
            }
            xs[j] = v;
        }
        for (unsigned i = 0; i + 1 < used; i += 2)
            fill(dc, xs[i], y, xs[i + 1] + 1, y + 1, c);
    }
}
static void r_surface(int b)
{
    (void)b;
}
static const EdgeRender edge_renderer = {
    0x55495831u, r_clip,  r_fill,     r_text, r_line,
    r_ellipse,   r_polygon, r_surface, edge_theme,
};

/* cross-module exports: fingerkb.dll snaps these (host loader) */
__attribute__((dllexport)) const EdgeRender *render_SpGetThreadContext(void)
{
    return &edge_renderer;
}
__attribute__((dllexport)) int svf_SVFDrawTint(void *dc, const void *data,
                                               unsigned length, int x, int y,
                                               int w, int h, U32 tint)
{
    return SVFDrawTint(dc, data, length, x, y, w, h, tint);
}

/* ---- model ---- */
#define MAX_TABS 8
#define MAX_URL 96
typedef struct {
    char title[40];
    char url[MAX_URL];
    char host[40];
    unsigned hash;
    int priv; /* InPrivate tab */
} Tab;

typedef struct {
    const char *host, *label;
    U32 color;
} Site;

static const Site SITES[6] = {
    {"en.wikipedia.org", "Wikipedia", RGB_(0x33, 0x33, 0x33)},
    {"github.com", "GitHub", RGB_(0x24, 0x29, 0x2E)},
    {"msdn.microsoft.com", "MSDN", RGB_(0x00, 0x72, 0xC6)},
    {"news.ycombinator.com", "Hacker News", RGB_(0xFF, 0x66, 0x00)},
    {"xania.fr", "Xania", RGB_(0x8B, 0x00, 0x00)},
    {"bing.com", "Bing", RGB_(0x00, 0x83, 0x73)},
};

typedef struct {
    const char *title, *host, *time;
} Item;
static Item favorites[12] = {
    {"Wikipedia", "en.wikipedia.org", ""},
    {"GitHub", "github.com", ""},
    {"Hacker News", "news.ycombinator.com", ""},
    {"MSN", "msn.com", ""},
    {"Xania", "xania.fr", ""},
};
static int fav_count = 5;
static Item reading_list[12] = {
    {"Stay on top of your reading", "msn.com", ""},
    {"Windows 10 Mobile: the best bits", "theverge.com", ""},
    {"Build 1607: what's new", "blogs.windows.com", ""},
};
static int rl_count = 3;
/* history seeded like the reference device capture (history.png) */
static Item hist_rows[8] = {
    {"Exam - Wikipedia", "en.wikipedia.org", "8:27 PM"},
    {"Test - Wikipedia", "en.wikipedia.org", "8:27 PM"},
    {"test - Search", "bing.com", "8:27 PM"},
    {"Download Microsoft Edge", "explore.microsoft.com", "8:26 PM"},
};
static int hist_count = 4;

static UxFile ux;
static Tab tabs[MAX_TABS];
static Tab closed_tabs[4];
static int closed_count;
static int tab_count = 1, tab_cur;
static int mode; /* see enum below */
enum {
    M_BROWSER = 0, M_MENU, M_TABS, M_HUB, M_ADDRESS, M_SETTINGS, M_CLEAR,
    M_DLG_READLIST, M_DLG_FAV, M_FIND, M_TABSMENU,
};
static int hub_pivot; /* 0 favorites 1 reading 2 history 3 downloads */
static char addr_buf[MAX_URL];
static int addr_len;
static char toast_text[80];
static void *g_wnd;
static const EdgeKb *g_kb;

/* runtime per-node flags (index-stable after parse) */
#define NS_TOGGLED 1u /* checkbox flipped at runtime */
#define NS_HIDDEN 2u  /* force invisible                */
#define NS_SHOWN 4u   /* force visible                  */
static unsigned char nstate[UX_MAX_NODES];

/* ---- layout cache ---- */
typedef struct {
    short x, y, w, h;
} NRect;
static NRect nrect[UX_MAX_NODES];

static int node_visible_idx(unsigned i)
{
    const UxNode *n = &ux.nodes[i];
    int mv = ux_int(&ux, n, UXA_VISIBLE, 1) != 0;
    if (nstate[i] & NS_HIDDEN)
        mv = 0;
    else if (nstate[i] & NS_SHOWN)
        mv = 1;
    return mv;
}

static int find_node(const char *name) /* global by automation name */
{
    for (uint16_t i = 0; i < ux.node_count; ++i) {
        const char *n = ux_str(&ux, &ux.nodes[i], UXA_NAME, 0);
        if (n && xeq(n, name))
            return i;
    }
    return -1;
}

static void flag_named(const char *name, int bit, int on)
{
    int i = find_node(name);
    if (i < 0)
        return;
    if (on)
        nstate[i] |= (unsigned char)bit;
    else
        nstate[i] &= (unsigned char)~bit;
}

/* ---- dynamic bindings (the XAML {Binding} substitute) ---- */
static const char *dyn_text(const char *name, const char *def)
{
    if (!name)
        return def;
    if (xeq(name, "HubTitle")) {
        static const char *names[4] = {"FAVORITES", "READING LIST", "HISTORY",
                                       "DOWNLOADS"};
        return names[hub_pivot & 3];
    }
    if (xeq(name, "RLNameText") || xeq(name, "FavNameText")) {
        Tab *t = &tabs[tab_cur];
        return t->title[0] ? t->title : "New tab";
    }
    return def;
}

static U32 dyn_color(const char *name, U32 def)
{
    if (!name)
        return def;
    if (xeq(name, "ReloadIcon"))
        return tabs[tab_cur].url[0] ? C_INK : C_SEP;
    if (xeq(name, "PivotIcon0") || xeq(name, "PivotIcon1") ||
        xeq(name, "PivotIcon2") || xeq(name, "PivotIcon3")) {
        int p = name[9] - '0';
        return hub_pivot == p ? RGB_(0x00, 0x5A, 0xA1) : C_INK;
    }
    return def;
}

static void node_text(const UxNode *n, const char **out)
{
    const char *t = ux_str(&ux, n, UXA_TEXT, "");
    const char *d = dyn_text(ux_str(&ux, n, UXA_NAME, 0), 0);
    *out = d ? d : t;
}

/* ---- layout: relative coords, auto flow in stacks/lists ---- */
static void layout_doc(const UxDoc *d, int bx, int by, int bw, int bh)
{
    if (!d || d->root == UX_NONE)
        return;
    unsigned stack[64];
    int sp = 0;
    stack[sp++] = d->root;
    nrect[d->root].x = (short)bx;
    nrect[d->root].y = (short)by;
    nrect[d->root].w = (short)bw;
    nrect[d->root].h = (short)bh;
    while (sp) {
        unsigned ni = stack[--sp];
        const UxNode *n = &ux.nodes[ni];
        NRect pr = nrect[ni];
        int acc = 0;
        for (int ci = 0; ci < (int)n->child_count; ++ci) {
            unsigned c = n->child[ci];
            const UxNode *cn = &ux.nodes[c];
            int cx = ux_int(&ux, cn, UXA_X, 0);
            int cy = ux_int(&ux, cn, UXA_Y, 0);
            int cw = ux_int(&ux, cn, UXA_W, 0);
            int ch = ux_int(&ux, cn, UXA_H, 0);
            if (n->kind == UX_STACKV || n->kind == UX_LIST) {
                cy = acc;
                acc += ch ? ch : 44;
                if (!cw)
                    cw = pr.w;
                cx = 0;
            } else if (n->kind == UX_STACKH) {
                cx = acc;
                acc += cw ? cw : 40;
                if (!ch)
                    ch = pr.h;
                cy = 0;
            } else {
                if (!cw)
                    cw = pr.w;
                if (!ch)
                    ch = pr.h;
            }
            nrect[c].x = (short)(pr.x + cx);
            nrect[c].y = (short)(pr.y + cy);
            nrect[c].w = (short)cw;
            nrect[c].h = (short)ch;
        }
        for (int ci = (int)n->child_count - 1; ci >= 0; --ci) {
            unsigned c = n->child[ci];
            if (ux.nodes[c].child_count && sp < 60)
                stack[sp++] = c;
        }
    }
}

/* ---- node painting (shared controls via uixctl painters) ---- */
static void paint_node(void *dc, unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    if (!node_visible_idx(ni))
        return;
    NRect r = nrect[ni];
    const char *name = ux_str(&ux, n, UXA_NAME, 0);
    switch (n->kind) {
    case UX_RECT: {
        if (ux_int(&ux, n, UXA_BORDER, 0)) {
            U32 c = ux_clr(&ux, n, UXA_COLOR, C_MUT);
            ic_rect(ifill, dc, r.x, r.y, r.w, r.h, 1, c);
        } else {
            U32 c = ux_clr(&ux, n, UXA_BG, C_INK);
            int rad = ux_int(&ux, n, UXA_RADIUS, 0);
            if (rad)
                fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, c, rad);
            else
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, c);
        }
        break;
    }
    case UX_ELLIPSE:
        ic_dot(ifill, dc, r.x + r.w / 2, r.y + r.h / 2, r.w / 2,
               ux_clr(&ux, n, UXA_BG, C_INK));
        break;
    case UX_GLYPH: {
        const char *t;
        node_text(n, &t);
        int id = ic_find(t);
        U32 col = dyn_color(name, ux_clr(&ux, n, UXA_COLOR, C_INK));
        int sz = r.w < r.h ? r.w : r.h;
        ic_draw(ifill, dc, r.x, r.y, sz, col, id);
        break;
    }
    case UX_TEXT:
    case UX_BUTTON: {
        if (n->kind == UX_BUTTON) {
            U32 bg = ux_clr(&ux, n, UXA_BG, 0xFFFFFFFFu);
            if (bg != 0xFFFFFFFFu) {
                int rad = ux_int(&ux, n, UXA_RADIUS, 0);
                if (rad)
                    fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg, rad);
                else
                    fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg);
            }
        }
        const char *t;
        node_text(n, &t);
        if (t && *t) {
            int size = ux_int(&ux, n, UXA_SIZE, 12);
            int align = ux_int(&ux, n, UXA_ALIGN, 0);
            int bold = ux_int(&ux, n, UXA_BOLD, 0);
            U32 col = dyn_color(name, ux_clr(&ux, n, UXA_COLOR, C_INK));
            int tx = r.x, ty = r.y;
            if (align == 1)
                tx += r.w / 2;
            else if (align == 2)
                tx += r.w;
            draw_text(dc, tx, ty, t, col, size, align, bold);
        }
        break;
    }
    case UX_CHECK: {
        /* shared uixctl painter: 20px accent box + label (clean_hist.png) */
        int on = ux_int(&ux, n, UXA_CHECKED, 0) || (nstate[ni] & NS_TOGGLED);
        const char *t;
        node_text(n, &t);
        ctl_checkbox(dc, &edge_renderer, r.x, r.y, on, t);
        break;
    }
    case UX_TOGGLE: {
        int on = ux_int(&ux, n, UXA_CHECKED, 0) || (nstate[ni] & NS_TOGGLED);
        ctl_toggle(dc, &edge_renderer, r.x, r.y, on);
        break;
    }
    case UX_TEXTBOX: {
        U32 bg = ux_clr(&ux, n, UXA_BG, C_BAR);
        int rad = ux_int(&ux, n, UXA_RADIUS, 2);
        fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg, rad);
        const char *t;
        node_text(n, &t);
        if (t && *t)
            draw_text(dc, r.x + 8, r.y + (r.h - 12) / 2, t, C_MUT, 12, 0, 0);
        break;
    }
    default:
        break;
    }
    for (uint8_t i = 0; i < n->child_count; ++i)
        paint_node(dc, n->child[i]);
}

/* ---- mock web content (the "engine") ---- */
/* token: a word + colour; used to lay out article text with inline links */
typedef struct {
    const char *s;
    U32 c;
} Tok;

#define BLK RGB_(0x00, 0x00, 0x00)
#define LNK C_WLINK

static void paint_tok_line(void *dc, int x, int y, const Tok *t, int n)
{
    for (int i = 0; i < n; ++i) {
        int w = text_w(t[i].s, 11);
        draw_text(dc, x, y, t[i].s, t[i].c, 11, 0, 0);
        /* links are underlined (webpage.png): 1px rule under the text */
        if (t[i].c == LNK && t[i].s[0])
            fill(dc, x, y + 11, x + w, y + 12, LNK);
        x += w;
    }
}

static void paint_wiki(void *dc)
{
    fill(dc, 0, 0, 320, 480, C_PAGEBG);
    /* article title + language tabs + outline buttons (webpage.png) */
    draw_text(dc, 16, 8, "Exam", BLK, 22, 0, 1);
    draw_text(dc, 16, 40, "Article", LNK, 11, 0, 0);
    fill(dc, 16, 51, 16 + text_w("Article", 11), 52, LNK);
    draw_text(dc, 66, 40, "Talk", LNK, 11, 0, 0);
    fill(dc, 66, 51, 66 + text_w("Talk", 11), 52, LNK);
    ic_rect(ifill, dc, 16, 60, 78, 80, 1, RGB_(0xA2, 0xA9, 0xB1));
    draw_text(dc, 26, 65, "Language", BLK, 11, 0, 0);
    ic_rect(ifill, dc, 242, 60, 304, 80, 1, RGB_(0xA2, 0xA9, 0xB1));
    draw_text(dc, 262, 65, "Edit", BLK, 11, 0, 0);
    draw_text(dc, 16, 90, "(Redirected from Test (assessment))",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    draw_text(dc, 16, 104, "For other uses, see Exam (disambiguation)",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    draw_text(dc, 16, 118, "and Examination (disambiguation).",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    /* body with inline links */
    static const Tok L1[] = {{"An ", BLK}, {"examination", BLK}, {" (exam or ", BLK},
        {"test", LNK}, {"), or ", BLK}, {"test", LNK}, {", is an ", BLK},
        {"educational", LNK}, {" ", BLK}, {"assessment", LNK}};
    static const Tok L2[] = {{"intended to measure a test-taker's ", BLK},
        {"knowledge", LNK}, {", ", BLK}, {"skill", LNK}, {", ", BLK},
        {"aptitude", LNK}, {",", BLK}};
    static const Tok L3[] = {{"", BLK}, {"physical fitness", LNK}, {", or ", BLK},
        {"classification", LNK}, {" in many other", BLK}};
    static const Tok L4[] = {{"topics (e.g. ", BLK}, {"beliefs", LNK}, {" or ", BLK},
        {"personal opinion", LNK}, {"). A ", BLK}, {"test", LNK}, {" may", BLK}};
    static const Tok L5[] = {{"be administered ", BLK}, {"verbally", LNK}, {", on ", BLK},
        {"paper", LNK}, {", on a ", BLK}, {"computer", LNK}, {", or in a", BLK}};
    static const Tok L6[] = {{"formal or informal setting. An ", BLK},
        {"exam", LNK}, {" may be", BLK}};
    static const Tok L7[] = {{"administered by an ", BLK}, {"examination board", LNK},
        {", an", BLK}};
    static const Tok L8[] = {{"instructor, a ", BLK}, {"proctor", LNK},
        {", or a ", BLK}, {"testing service", LNK}, {".", BLK}};
    static const Tok L9[] = {{"The ", BLK}, {"Wikipedia", LNK},
        {" article on examinations", BLK}};
    static const Tok L10[] = {{"covers the history of ", BLK}, {"testing", LNK},
        {" in", BLK}};
    static const Tok L11[] = {{"education and ", BLK}, {"psychometrics", LNK},
        {".", BLK}};
    static const Tok L12[] = {{"This page is rendered locally by the NTOS mock",
                               RGB_(0x54, 0x59, 0x5D)}};
    int y = 140;
    paint_tok_line(dc, 16, y, L1, 10); y += 17;
    paint_tok_line(dc, 16, y, L2, 7); y += 17;
    paint_tok_line(dc, 16, y, L3, 4); y += 17;
    paint_tok_line(dc, 16, y, L4, 7); y += 17;
    paint_tok_line(dc, 16, y, L5, 7); y += 17;
    paint_tok_line(dc, 16, y, L6, 3); y += 17;
    paint_tok_line(dc, 16, y, L7, 3); y += 17;
    paint_tok_line(dc, 16, y, L8, 5); y += 24;
    paint_tok_line(dc, 16, y, L9, 3); y += 17;
    paint_tok_line(dc, 16, y, L10, 3); y += 17;
    paint_tok_line(dc, 16, y, L11, 3); y += 24;
    draw_text(dc, 16, y, "This page is rendered locally by the NTOS mock",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    draw_text(dc, 16, y + 13, "engine. No network stack involved.",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
}

static void paint_page(void *dc)
{
    Tab *t = &tabs[tab_cur];
    if (!t->url[0]) {
        if (t->priv) {
            /* InPrivate new tab: uXAML page over the content host */
            const UxDoc *d = ux_find_doc(&ux, "InPrivatePage");
            if (d) {
                layout_doc(d, 0, 0, 320, 528);
                paint_node(dc, d->root);
            }
        } else {
            /* home state per home.png: plain white page */
            fill(dc, 0, 0, 320, 480, C_PAGEBG);
        }
        return;
    }
    int wiki = 0;
    for (const char *p = t->host; *p; ++p)
        if (p[0] == 'w' && p[1] == 'i' && p[2] == 'k' && p[3] == 'i')
            wiki = 1;
    if (wiki) {
        paint_wiki(dc);
        return;
    }
    fill(dc, 0, 0, 320, 480, C_PAGEBG);
    draw_text(dc, 16, 16, t->title, BLK, 18, 0, 1);
    draw_text(dc, 16, 48, t->host, LNK, 12, 0, 0);
    fill(dc, 16, 60, 16 + text_w(t->host, 12), 61, LNK); /* underlined link */
    static const int P1[4] = {288, 260, 274, 180};
    for (int i = 0; i < 4; ++i)
        fill(dc, 16, 78 + i * 16, 16 + P1[i], 78 + i * 16 + 8,
             RGB_(0xEA, 0xEC, 0xF0));
    static const int P2[4] = {288, 274, 252, 210};
    for (int i = 0; i < 4; ++i)
        fill(dc, 16, 156 + i * 16, 16 + P2[i], 156 + i * 16 + 8,
             RGB_(0xEA, 0xEC, 0xF0));
    draw_text(dc, 16, 240, "This page is rendered locally by the NTOS mock",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    draw_text(dc, 16, 254, "engine. No network stack involved.",
              RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
}

/* two-tone URL: subdomain grey, domain white (webpage.png).
 * Private tabs carry the small blue InPrivate glyph before the text
 * (inpriv.png: ~16px at x104, then "Search or browse"/URL from x124). */
static void paint_address_url(void *dc)
{
    Tab *t = &tabs[tab_cur];
    if (!t->url[0])
        return;
    int ti = find_node("AddressText");
    if (ti < 0)
        return;
    NRect r = nrect[ti];
    if (t->priv)
        ic_draw(ifill, dc, r.x - 20, r.y - 3, 16, C_BLUE, IC_INPRIVATE);
    const char *h = t->host;
    const char *dot = h;
    while (*dot && *dot != '.')
        ++dot;
    if (*dot && dot[1]) {
        char pre[24];
        int n = (int)(dot - h) + 1; /* include the dot */
        if (n > 23)
            n = 23;
        xcopy(pre, h, n);
        pre[n] = 0;
        draw_text_clamp(dc, r.x, r.y, pre, C_MUT, 12, 60);
        draw_text_clamp(dc, r.x + text_w(pre, 12), r.y, dot + 1, C_INK, 12, 130);
    } else {
        draw_text_clamp(dc, r.x, r.y, h, C_INK, 12, 126);
    }
}

/* ---- tab center cards: 2-col grid on black (tabs/manytabs.png) ----
 * Reference geometry (pixel-audited): card 142x146 at x12/x166,
 * header 30px (#1F1F1F) with the 1px-stroke default window icon at 13px,
 * dim #8A8A8A title, 12px close cross; thumbnail below with a 1px
 * #1F1F1F frame on LEFT/RIGHT/BOTTOM — the minimized-tab border the
 * real device shows. Pitch 148. */
static int card_col_x(int col)
{
    return 12 + col * 154;
}

#define C_TABTITLE RGB_(0x8A, 0x8A, 0x8A) /* dimmer than pure white  */
#define C_CROSS   RGB_(0xB0, 0xB0, 0xB0) /* card close cross         */

static void paint_tab_cards(void *dc)
{
    for (int i = 0; i < tab_count; ++i) {
        int col = i % 2, row = i / 2;
        int cx = card_col_x(col), cy = 44 + row * 148;
        if (cy > 420)
            break;
        Tab *t = &tabs[i];
        /* header strip: #1F1F1F, 30px tall */
        fill(dc, cx, cy, cx + 142, cy + 30, C_BAR);
        if (t->priv)
            ic_draw(ifill, dc, cx + 21, cy + 9, 13, C_MUT, IC_INPRIVATE);
        else
            ic_draw(ifill, dc, cx + 21, cy + 9, 13, C_MUT, IC_TABS);
        draw_text_clamp(dc, cx + 42, cy + 10, t->title, C_TABTITLE, 11, 92);
        ic_draw(ifill, dc, cx + 128, cy + 9, 12, C_CROSS, IC_CLOSE);
        /* thumbnail: white for loaded pages, black for empty tabs */
        if (t->url[0])
            fill(dc, cx, cy + 30, cx + 142, cy + 146, C_PAGEBG);
        else
            fill(dc, cx, cy + 30, cx + 142, cy + 146, C_PAGE);
        /* minimized-tab frame: 1px #1F1F1F on left/right/bottom */
        fill(dc, cx, cy + 30, cx + 1, cy + 146, C_BAR);
        fill(dc, cx + 141, cy + 30, cx + 142, cy + 146, C_BAR);
        fill(dc, cx, cy + 145, cx + 142, cy + 146, C_BAR);
    }
}

/* ---- hub content (pivot panes; history.png / edge4.png) ---- */
static void paint_hub_rows(void *dc, const Item *items, int count, int icon)
{
    int y = 96;
    for (int i = 0; i < count && y < 500; ++i, y += 50) {
        ic_draw(ifill, dc, 15, y + 2, 20, C_MUT, icon);
        draw_text_clamp(dc, 44, y + 2, items[i].title, C_INK, 14, 230);
        if (items[i].time && items[i].time[0])
            draw_text(dc, 308, y + 2, items[i].time, C_MUT, 11, 2, 0);
        draw_text_clamp(dc, 44, y + 22, items[i].host, C_MUT, 11, 230);
    }
}

static void paint_hub_content(void *dc)
{
    if (hub_pivot == 0) {
        int y = 96;
        for (int i = 0; i < fav_count && y < 500; ++i, y += 50) {
            U32 pc = C_SHEET;
            for (unsigned s = 0; s < 6; ++s)
                if (xeq(favorites[i].host, SITES[s].host))
                    pc = SITES[s].color;
            fill_round(dc, 15, y + 2, 35, y + 22, pc, 4);
            char c2[2] = {favorites[i].title[0], 0};
            draw_text(dc, 25, y + 4, c2, C_INK, 11, 1, 1);
            draw_text_clamp(dc, 44, y + 2, favorites[i].title, C_INK, 14, 240);
            draw_text_clamp(dc, 44, y + 22, favorites[i].host, C_MUT, 11, 240);
        }
    } else if (hub_pivot == 1) {
        paint_hub_rows(dc, reading_list, rl_count, IC_READLIST);
    } else if (hub_pivot == 2) {
        draw_text(dc, 18, 104, "Last hour", C_INK, 11, 0, 1);
        int y = 126;
        for (int i = 0; i < hist_count && y < 500; ++i, y += 50) {
            U32 pc = C_SHEET;
            for (unsigned s = 0; s < 6; ++s)
                if (xeq(hist_rows[i].host, SITES[s].host))
                    pc = SITES[s].color;
            fill_round(dc, 15, y + 2, 35, y + 22, pc, 4);
            char c2[2] = {hist_rows[i].title[0], 0};
            draw_text(dc, 25, y + 4, c2, C_INK, 11, 1, 1);
            draw_text_clamp(dc, 44, y + 2, hist_rows[i].title, C_INK, 14, 210);
            draw_text(dc, 308, y + 2, hist_rows[i].time, C_MUT, 11, 2, 0);
            draw_text_clamp(dc, 44, y + 22, hist_rows[i].host, C_MUT, 11, 230);
        }
    } else {
        /* empty downloads pane (edge4.png) */
        draw_text(dc, 10, 110, "You'll see current and past downloads here.",
                  C_INK, 14, 0, 0);
    }
}

/* ---- navigation ---- */
static void set_lock_flag(void)
{
    int loaded = tabs[tab_cur].url[0] != 0;
    int priv = tabs[tab_cur].priv;
    flag_named("AddressLock", NS_SHOWN, loaded);
    /* placeholder hides once a URL is loaded; private tabs get the small
     * blue InPrivate glyph at x104 + a re-indented placeholder (inpriv.png) */
    flag_named("AddressHint", NS_HIDDEN, loaded || priv);
    flag_named("AddressHintPriv", NS_SHOWN, priv && !loaded);
    flag_named("PrivChip", NS_SHOWN, priv && !loaded);
}

static void hist_add(const char *host, const char *title)
{
    if (hist_count >= 8)
        hist_count = 8 - 1;
    for (int i = hist_count; i > 0; --i)
        hist_rows[i] = hist_rows[i - 1];
    hist_rows[0].host = host;
    hist_rows[0].title = title;
    hist_rows[0].time = "8:27 PM";
    ++hist_count;
}

static void nav_url(const char *url)
{
    Tab *t = &tabs[tab_cur];
    const char *p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p') {
        while (*p && *p != '/')
            ++p;
        if (p[0] == '/' && p[1] == '/')
            p += 2;
        else
            p = url;
    }
    int n = xstrlen(p);
    if (n > MAX_URL - 1)
        n = MAX_URL - 1;
    xcopy(t->url, p, n);
    t->url[n] = 0;
    int hl = 0;
    while (p[hl] && p[hl] != '/' && hl < 39)
        ++hl;
    xcopy(t->host, p, hl);
    t->host[hl] = 0;
    t->hash = xhash(t->url);
    const char *title = t->host;
    for (unsigned s = 0; s < 6; ++s)
        if (xeq(t->host, SITES[s].host))
            title = SITES[s].label;
    int tl = xstrlen(title);
    if (tl > 39)
        tl = 39;
    xcopy(t->title, title, tl);
    t->title[tl] = 0;
    if (!t->title[0]) {
        xcopy(t->title, "Page", 4);
        t->title[4] = 0;
    }
    hist_add(t->host, t->title);
    set_lock_flag();
}

static void tab_new(int priv)
{
    if (tab_count >= MAX_TABS)
        return;
    Tab *t = &tabs[tab_count++];
    xcopy(t->title, "New tab", 7);
    t->title[7] = 0;
    t->url[0] = 0;
    t->host[0] = 0;
    t->hash = 0;
    t->priv = priv;
    tab_cur = tab_count - 1;
    set_lock_flag();
}

static void tab_close_idx(int idx)
{
    if (tab_count <= 1) {
        tabs[0].url[0] = 0;
        tabs[0].host[0] = 0;
        xcopy(tabs[0].title, "New tab", 8);
        tabs[0].priv = 0;
        tab_cur = 0;
        set_lock_flag();
        return;
    }
    if (closed_count < 4)
        closed_tabs[closed_count++] = tabs[idx];
    for (int i = idx; i + 1 < tab_count; ++i)
        tabs[i] = tabs[i + 1];
    --tab_count;
    if (tab_cur >= tab_count)
        tab_cur = tab_count - 1;
    if (tab_cur > idx)
        --tab_cur;
    set_lock_flag();
}

/* ---- toast (NotificationBar substitute) ---- */
static void toast(const char *s)
{
    int i = 0;
    while (s && s[i] && i < 78) {
        toast_text[i] = s[i];
        ++i;
    }
    toast_text[i] = 0;
}

/* ---- repaint ---- */
static void paint_browser(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "SmallScreenPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root); /* chrome incl. PageHost background */
    clip_set(0, 0, 320, 480);
    paint_page(dc);
    clip_set(0, 0, 320, 528);
    paint_address_url(dc);
}

static void paint_all(void *dc)
{
    const UxDoc *d;
    if (mode == M_BROWSER || mode == M_FIND) {
        paint_browser(dc);
        if (mode == M_FIND && (d = ux_find_doc(&ux, "FindOnPageBar"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_MENU) {
        paint_browser(dc);
        if ((d = ux_find_doc(&ux, "ActionsMenu"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_TABS || mode == M_TABSMENU) {
        if ((d = ux_find_doc(&ux, "TabCenterPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            /* doc first (black page, "Tabs" title, toolbar strip), then the
             * cards clipped to the content area: a 6th tab's row 2 stops at
             * the toolbar like on the device (manytabs.png) */
            paint_node(dc, d->root);
            clip_set(0, 0, 320, 480);
            paint_tab_cards(dc);
            clip_set(0, 0, 320, 528);
        }
        if (mode == M_TABSMENU && (d = ux_find_doc(&ux, "TabsContextMenu"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_HUB) {
        if ((d = ux_find_doc(&ux, "HubPanel"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* active pivot underline (history.png: x88..131, y42..44) */
            fill(dc, hub_pivot * 44, 42, hub_pivot * 44 + 44, 45,
                 RGB_(0x00, 0x5A, 0xA1));
            flag_named("HubClear", NS_HIDDEN, hub_pivot != 2);
            paint_hub_content(dc);
        }
    } else if (mode == M_ADDRESS) {
        if ((d = ux_find_doc(&ux, "AddressEditBoxPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* shared fingerkb.dll paints the SIP (from y271 backdrop) */
            if (g_kb)
                g_kb->paint(dc);
            /* toolbar row over the SIP backdrop (tabs icon, type.png) */
            fill(dc, 0, 270, 320, 317, C_BAR);
            ic_draw(ifill, dc, 14, 284, 20, C_INK, IC_TABS);
            /* typed text + caret in the white field */
            if (addr_len) {
                draw_text_clamp(dc, 11, 244, addr_buf, RGB_(0x00, 0x00, 0x00),
                                12, 280);
                int cx = 11 + text_w(addr_buf, 12);
                if (cx < 306)
                    fill(dc, cx + 2, 236, cx + 4, 266, C_BLUE);
            }
        }
    } else if (mode == M_SETTINGS) {
        if ((d = ux_find_doc(&ux, "SettingsPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* hairline separators between rows */
            for (int i = 1; i <= 6; ++i)
                fill(dc, 12, 48 + i * 48, 308, 49 + i * 48, RGB_(0x1F, 0x1F, 0x1F));
        }
    } else if (mode == M_CLEAR) {
        if ((d = ux_find_doc(&ux, "ClearBrowsingDataPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_DLG_READLIST || mode == M_DLG_FAV) {
        const char *doc = mode == M_DLG_READLIST ? "AddToReadingListDialog"
                                                 : "AddToFavoritesDialog";
        if ((d = ux_find_doc(&ux, doc))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* bordered input field via the shared ctl_field painter */
            int fi = find_node(mode == M_DLG_READLIST ? "RLName" : "FavName");
            if (fi >= 0) {
                NRect r = nrect[fi];
                Tab *t = &tabs[tab_cur];
                const char *txt = t->title[0] ? t->title : "";
                ctl_field(dc, &edge_renderer, r.x, r.y, r.w, r.h, txt,
                          "Test", 1);
            }
        }
    }
    /* notification bar (toast) sits above the address bar */
    if (toast_text[0]) {
        fill(dc, 0, 428, 320, 480, C_BAR);
        fill(dc, 0, 428, 320, 429, C_SEP);
        draw_text_clamp(dc, 16, 449, toast_text, C_INK, 11, 250);
    }
}

/* ---- hit testing ---- */
static const UxNode *hit_test(const UxDoc *d, int x, int y)
{
    const UxNode *best = 0;
    if (!d || d->root == UX_NONE)
        return 0;
    unsigned lo = d->root, hi = d->root + d->node_count;
    if (hi > ux.node_count)
        hi = ux.node_count;
    for (unsigned i = lo; i < hi; ++i) {
        const UxNode *n = &ux.nodes[i];
        if ((n->kind != UX_BUTTON && n->kind != UX_CHECK &&
             n->kind != UX_TOGGLE) || !node_visible_idx(i))
            continue;
        NRect r = nrect[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            if (n->kind == UX_CHECK || n->kind == UX_TOGGLE) {
                nstate[i] ^= NS_TOGGLED;
                best = n; /* toggled; no command dispatch needed */
                continue;
            }
            best = n;
        }
    }
    return best;
}

/* ---- command dispatch ---- */
static void do_command(const char *cmd)
{
    if (xeq(cmd, "tabs"))
        mode = M_TABS;
    else if (xeq(cmd, "menu"))
        mode = M_MENU;
    else if (xeq(cmd, "address")) {
        mode = M_ADDRESS;
        addr_len = 0;
        addr_buf[0] = 0;
        if (g_kb)
            g_kb->dock(0);
    } else if (xeq(cmd, "reload"))
        toast("Refreshing...");
    else if (xeq(cmd, "reader"))
        toast("This page can't be shown in reading view");
    else if (xeq(cmd, "fwd"))
        ; /* disabled: no forward stack in the POC */
    else if (xeq(cmd, "share"))
        toast("Microsoft Edge can't share right now because the page is still loading.");
    else if (xeq(cmd, "addread"))
        mode = M_DLG_READLIST;
    else if (xeq(cmd, "addfav"))
        mode = M_DLG_FAV;
    else if (xeq(cmd, "findonpage"))
        mode = M_FIND;
    else if (xeq(cmd, "pintostart"))
        toast("Pinned to Start");
    else if (xeq(cmd, "settings"))
        mode = M_SETTINGS;
    else if (xeq(cmd, "addr-tabs"))
        mode = M_TABS;
    else if (xeq(cmd, "tabs-back") || xeq(cmd, "hub-back") ||
             xeq(cmd, "addr-close") || xeq(cmd, "set-back") ||
             xeq(cmd, "find-close") || xeq(cmd, "dialog-cancel") ||
             xeq(cmd, "close"))
        mode = M_BROWSER;
    else if (xeq(cmd, "clear-back"))
        mode = M_SETTINGS;
    else if (xeq(cmd, "hub-favorites")) { hub_pivot = 0; mode = M_HUB; }
    else if (xeq(cmd, "hub-reading"))   { hub_pivot = 1; mode = M_HUB; }
    else if (xeq(cmd, "hub-history"))   { hub_pivot = 2; mode = M_HUB; }
    else if (xeq(cmd, "hub-downloads")) { hub_pivot = 3; mode = M_HUB; }
    else if (xeq(cmd, "pivot-0")) hub_pivot = 0;
    else if (xeq(cmd, "pivot-1")) hub_pivot = 1;
    else if (xeq(cmd, "pivot-2")) hub_pivot = 2;
    else if (xeq(cmd, "pivot-3")) hub_pivot = 3;
    else if (xeq(cmd, "hubclear")) {
        hist_count = 0;
        toast("All clear!");
    } else if (xeq(cmd, "newtab"))
        tab_new(0);
    else if (xeq(cmd, "newprivate"))
        tab_new(1);
    else if (xeq(cmd, "tabsmenu"))
        mode = M_TABSMENU;
    else if (xeq(cmd, "ctx-refresh-all"))
        mode = M_TABS;
    else if (xeq(cmd, "ctx-duplicate")) {
        if (tab_count < MAX_TABS) {
            tabs[tab_count] = tabs[tab_cur];
            tab_cur = tab_count++;
        }
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-close")) {
        tab_close_idx(tab_cur);
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-close-others")) {
        Tab keep = tabs[tab_cur];
        tabs[0] = keep;
        tab_count = 1;
        tab_cur = 0;
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-close-right")) {
        tab_count = tab_cur + 1;
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-close-all")) {
        tab_close_idx(0);
        while (tab_count > 1)
            tab_close_idx(1);
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-reopen")) {
        if (closed_count > 0 && tab_count < MAX_TABS) {
            tabs[tab_count++] = closed_tabs[--closed_count];
            tab_cur = tab_count - 1;
        }
        mode = M_TABS;
    } else if (xeq(cmd, "cleardata"))
        mode = M_CLEAR;
    else if (xeq(cmd, "doclear")) {
        hist_count = 0;
        toast("All clear!");
        mode = M_SETTINGS;
    } else if (xeq(cmd, "rl-add")) {
        if (rl_count < 12) {
            reading_list[rl_count].title = tabs[tab_cur].title[0]
                                               ? tabs[tab_cur].title
                                               : "New tab";
            reading_list[rl_count].host = tabs[tab_cur].host[0]
                                              ? tabs[tab_cur].host
                                              : "about:start";
            reading_list[rl_count].time = "";
            ++rl_count;
        }
        mode = M_BROWSER;
    } else if (xeq(cmd, "fav-add")) {
        if (fav_count < 12) {
            favorites[fav_count].title = tabs[tab_cur].title[0]
                                             ? tabs[tab_cur].title
                                             : "New tab";
            favorites[fav_count].host = tabs[tab_cur].host[0]
                                            ? tabs[tab_cur].host
                                            : "about:start";
            favorites[fav_count].time = "";
            ++fav_count;
        }
        mode = M_BROWSER;
    } else if (xeq(cmd, "link-cloud") || xeq(cmd, "link-bing") ||
               xeq(cmd, "advanced") || xeq(cmd, "feedback") ||
               xeq(cmd, "helpabout") || xeq(cmd, "priv-link") ||
               xeq(cmd, "fav-folder") || xeq(cmd, "find-type") ||
               xeq(cmd, "find-prev") || xeq(cmd, "find-next"))
        toast("Not available in this POC");
    /* "noop" and unknown commands: silently ignored */
}

/* ---- window proc ---- */
static void process_tap(int x, int y)
{
    const UxDoc *d;
    const UxNode *b;
    if (mode == M_ADDRESS) {
        if (y >= 317 && g_kb) {
            /* shared SIP: fingerkb.dll owns the keyboard hit zones */
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int c = g_kb->input(WM_DOWN_, 1, lp);
            if (c == 13) { /* enter */
                if (addr_len)
                    nav_url(addr_buf);
                mode = M_BROWSER;
            } else if (c == 8) {
                if (addr_len)
                    addr_buf[--addr_len] = 0;
            } else if (c >= 32 && c < 127 && addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = (char)c;
                addr_buf[addr_len] = 0;
            }
            return;
        }
        if (y < 223) { /* scrim: dismiss */
            mode = M_BROWSER;
            return;
        }
        if ((d = ux_find_doc(&ux, "AddressEditBoxPage")) &&
            (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    if (mode == M_TABS) {
        /* tab card grid hot zones (cards painted procedurally):
         * 142x146 cards at x12/x166, top y44, pitch 148 */
        if (x >= 12 && x < 308 && y >= 44 && y < 470) {
            int col = x >= 166 ? 1 : 0;
            int row = (y - 44) / 148;
            int idx = row * 2 + col;
            int cx = card_col_x(col), cy = 44 + row * 148;
            if (idx < tab_count) {
                if (x >= cx + 132 && y < cy + 30)
                    tab_close_idx(idx); /* close X in the header */
                else {
                    tab_cur = idx; /* switch */
                    set_lock_flag();
                    mode = M_BROWSER;
                }
            }
            return;
        }
        if ((d = ux_find_doc(&ux, "TabCenterPage")) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    if (mode == M_TABSMENU) {
        if ((d = ux_find_doc(&ux, "TabsContextMenu")) &&
            (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        else
            mode = M_TABS; /* light dismiss */
        return;
    }
    if (mode == M_HUB) {
        /* content rows navigate (history/favorites/reading list);
         * favorites/reading rows start at y96, history rows at y126
         * under the "Last hour" group header */
        const Item *rows = 0;
        int count = 0, base = 96;
        if (hub_pivot == 0) {
            rows = favorites;
            count = fav_count;
        } else if (hub_pivot == 1) {
            rows = reading_list;
            count = rl_count;
        } else if (hub_pivot == 2) {
            rows = hist_rows;
            count = hist_count;
            base = 126;
        }
        if (rows && y >= base && y < base + count * 50) {
            int idx = (y - base) / 50;
            if (idx >= 0 && idx < count) {
                nav_url(rows[idx].host);
                mode = M_BROWSER;
                return;
            }
        }
        if ((d = ux_find_doc(&ux, "HubPanel")) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    const char *docn = mode == M_MENU ? "ActionsMenu"
                    : mode == M_SETTINGS ? "SettingsPage"
                    : mode == M_CLEAR ? "ClearBrowsingDataPage"
                    : mode == M_DLG_READLIST ? "AddToReadingListDialog"
                    : mode == M_DLG_FAV ? "AddToFavoritesDialog"
                    : mode == M_FIND ? "FindOnPageBar"
                    : "SmallScreenPage";
    if ((d = ux_find_doc(&ux, docn)) && (b = hit_test(d, x, y))) {
        if (b->kind == UX_CHECK || b->kind == UX_TOGGLE)
            return; /* toggled in hit_test */
        const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
        if (cmd && *cmd)
            do_command(cmd);
        else if (mode == M_MENU)
            mode = M_BROWSER; /* scrim dismiss */
    } else if (mode == M_MENU) {
        mode = M_BROWSER; /* scrim tap dismisses */
    } else if (mode == M_FIND) {
        mode = M_BROWSER; /* tap on the page closes the find bar */
    }
}

static void addr_key(unsigned wp)
{
    if (wp == 8) {
        if (addr_len)
            addr_buf[--addr_len] = 0;
    } else if (wp == 13) {
        if (addr_len)
            nav_url(addr_buf);
        mode = M_BROWSER;
    } else if (addr_len < MAX_URL - 2 &&
               ((wp >= 'a' && wp <= 'z') || (wp >= 'A' && wp <= 'Z') ||
                (wp >= '0' && wp <= '9') || wp == '.' || wp == '/' ||
                wp == '-' || wp == ' ')) {
        addr_buf[addr_len++] = (char)wp;
        addr_buf[addr_len] = 0;
    }
}

static int wndproc(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    if (msg == WM_PAINT_) {
        Paint ps;
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            clip_set(0, 0, 320, 528);
            paint_all(dc);
        }
        NT_CALL(EndPaint, (hwnd, &ps));
        return 0;
    }
    if (msg == WM_KEY_ || msg == WM_CHAR_) {
        toast_text[0] = 0;
        if (wp == 0x1b) { /* hardware back: nested dismiss */
            if (mode == M_BROWSER)
                NT_CALL(PostQuitMessage, (0));
            else if (mode == M_CLEAR)
                mode = M_SETTINGS;
            else if (mode == M_TABSMENU)
                mode = M_TABS;
            else
                mode = M_BROWSER;
        } else if (mode == M_ADDRESS) {
            addr_key(wp);
        } else if (mode == M_BROWSER && (wp == 's' || wp == 'S')) {
            /* POC shortcut into the settings page (e2e reachability) */
            mode = M_SETTINGS;
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        toast_text[0] = 0; /* any input dismisses the toast */
        process_tap(x, y);
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

/* ---- markup load: \fls0\edge.uxaml, single read ---- */
#define UX_FILE_MAX (48 * 1024)
static unsigned char ux_blob[UX_FILE_MAX];

static int load_markup(void)
{
    static const W16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 'e', 'd', 'g', 'e',
                               '.', 'u', 'x', 'a', 'm', 'l', 0};
    void *h = NT_CALL(CreateFileW, (path, 0x80000000u, 0, 0, 3 /*OPEN_EXISTING*/, 0, 0));
    if (!h || h == (void *)(uintptr_t)0xFFFFFFFFu)
        return -1;
    U32 size = NT_CALL(GetFileSize, (h, 0));
    if (size == 0xFFFFFFFFu || size > UX_FILE_MAX) {
        NT_CALL(CloseHandle, (h));
        return -1;
    }
    U32 got = 0;
    int ok = NT_CALL(ReadFile, (h, ux_blob, size, &got, 0));
    NT_CALL(CloseHandle, (h));
    if (!ok || got != size)
        return -1;
    const char *err = 0;
    if (ux_parse(ux_blob, got, &ux, &err))
        return -1;
    return 0;
}

/* ---- entry ---- */
static const W16 cls_name[] = {'E', 'd', 'g', 'e', 'M', 'o', 'b', 'i', 'l', 'e', 0};

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u; /* file missing: ERROR_FILE_NOT_FOUND */
    tab_count = 1;
    tab_cur = 0;
    xcopy(tabs[0].title, "New tab", 8);
    /* host-side IAT snap: uixctl.c + svf.c (linked into this DLL) share
     * the render-context slot the device loader would snap per module */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
    /* shared keyboard DLL (FINGERKB.DLL), snapped by the host loader */
    g_kb = NT_CALL(UIXGetSchemaFactory, ());
    if (!g_kb || g_kb->profile != 0x55495831u)
        g_kb = 0;
    WndClass wc;
    wc.style = 0;
    wc.proc = wndproc;
    wc.cls_extra = wc.win_extra = 0;
    wc.instance = wc.icon = wc.cursor = wc.background = 0;
    wc.menu = 0;
    wc.name = cls_name;
    NT_CALL(RegisterClassW, (&wc));
    g_wnd = NT_CALL(CreateWindowExW, (0, cls_name, cls_name, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    NT_CALL(ShowWindow, (g_wnd, 1));
    NT_CALL(UpdateWindow, (g_wnd));
    Msg msg;
    int rc;
    while ((rc = NT_CALL(GetMessageW, (&msg, g_wnd, 0, 0))) > 0) {
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    return 0;
}

int DllMainCRTStartup_impl(void *h, U32 r, void *r2);
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
