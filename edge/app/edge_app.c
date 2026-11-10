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

/* CHAKRA.DLL stub entry (script forwarding; tiny expression evaluator) */
NT_IMPORT(int, JsRunScript, (const W16 *, unsigned, const W16 *, void **))
NT_DEFINE(JsRunScript)

static char find_query[40];
static int find_len, find_sip, find_active, find_nmatch;
static int fm_x[24], fm_y[24], fm_w[24];

/* mdrender host hooks: file IO + renderer + svf + the chakra stub */
int md_read_file(const char *name, unsigned char *buf, int cap);
U32 md_rgb(int r, int g, int b);
void md_fill(void *dc, int l, int t, int r, int b, U32 c);
void md_text(void *dc, int x, int y, const char *s, int len, U32 c, int h, int bold);
int md_text_w(const char *s, int len, int h);
void md_svfdraw(void *dc, const unsigned char *data, unsigned len, int x, int y, int w, int h);
int md_chakra(const char *script);

/* shared control painters (payload/phone/uixctl.c, linked into this DLL) */
void ctl_button(void *dc, const EdgeRender *r, int x, int y, int w, int h,
                const char *label, int pressed);
void ctl_checkbox(void *dc, const EdgeRender *r, int x, int y, int checked,
                  const char *label);
void ctl_toggle(void *dc, const EdgeRender *r, int x, int y, int on);
void ctl_toggle_ex(void *dc, const EdgeRender *r, int x, int y, int on, int dim);
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

/* MARKDOWN.DLL: the shared web-document engine (html4/css1/markdown core,
 * box-model layout with floats, find-in-page, reader mode). Edge hosts it
 * through an MdHost callback table; other apps can do the same. */
#include "markdown.h"
NT_IMPORT(void, md_set_host, (const MdHost *))
NT_IMPORT(int, md_load, (int, const char *, int))
NT_IMPORT(void, md_layout, (int))
NT_IMPORT(void, md_reader, (int))
NT_IMPORT(int, md_reader_is, (void))
NT_IMPORT(void, md_paint, (const MdHost *, int, int, int, int))
NT_IMPORT(void, md_size, (int *, int *))
NT_IMPORT(const char *, md_title, (int *))
NT_IMPORT(int, md_find, (const char *, int *, int *, int *, int))
NT_IMPORT(const char *, md_find_text, (int, int *))
NT_IMPORT(int, md_find_style, (int, int *, int *))

NT_IMPORT(int, md_link_at, (int, int, const char **, int *))
NT_IMPORT(int, md_anchor_scroll, (int))
NT_DEFINE(md_set_host)
NT_DEFINE(md_load)
NT_DEFINE(md_layout)
NT_DEFINE(md_reader)
NT_DEFINE(md_reader_is)
NT_DEFINE(md_paint)
NT_DEFINE(md_size)
NT_DEFINE(md_title)
NT_DEFINE(md_find)
NT_DEFINE(md_find_text)
NT_DEFINE(md_find_style)

NT_DEFINE(md_link_at)
NT_DEFINE(md_anchor_scroll)
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
NT_IMPORT(const W16 *, GetCommandLineW, (void))
NT_DEFINE(GetCommandLineW)

/* ---- palette (measured from refs/edge-screens, dark theme) ---- */
/* ---- app theme ----
 * Light is the NTOS system default (refs/edge-screens/light); dark is the
 * classic Edge chrome. InPrivate swaps the light surfaces for grey, like
 * the light reference capture. 't' (POC shortcut) toggles at runtime. */
typedef struct {
    U32 bar, sheet, page, card, btn, sep, ink, mut, dim, dis, blue, link,
        scrim, wlink, pagebg, tabtitle, cross, hubbg, sheetrow;
} EdgeTh;
static const EdgeTh TH_LIGHT = {
    RGB_(0xFF, 0xFF, 0xFF), /* bar: white address/toolbar               */
    RGB_(0xE7, 0xE7, 0xE7), /* sheet: menu sheets                        */
    RGB_(0xF7, 0xF3, 0xF7), /* page: hub/settings surfaces              */
    RGB_(0xE7, 0xE7, 0xE7), /* card: Clear button, preview areas        */
    RGB_(0xF7, 0xF3, 0xF7), /* btn: dialog buttons                      */
    RGB_(0xCE, 0xCF, 0xCE), /* sep: separators                          */
    RGB_(0x00, 0x00, 0x00), /* ink                                      */
    RGB_(0x7B, 0x7D, 0x7B), /* mut                                      */
    RGB_(0x4A, 0x49, 0x4A), /* dim                                      */
    RGB_(0xA5, 0xA6, 0xA5), /* dis: disabled reader icon                */
    RGB_(0x00, 0x78, 0xD7), /* blue                                     */
    RGB_(0x00, 0x62, 0xB1), /* link: InPrivate page links               */
    RGB_(0x7F, 0x7F, 0x7F), /* scrim                                   */
    RGB_(0x06, 0x45, 0xAD), /* wlink: links inside web pages            */
    RGB_(0xFF, 0xFF, 0xFF), /* pagebg: web page background              */
    RGB_(0x31, 0x30, 0x31), /* tabtitle: card titles                    */
    RGB_(0x5A, 0x5D, 0x5A), /* cross: card close glyph                  */
    RGB_(0xF7, 0xF3, 0xF7), /* hubbg: hub surface                       */
    RGB_(0xF7, 0xF3, 0xF7), /* sheetrow: menu sheet rows                */
};
static const EdgeTh TH_DARK = {
    RGB_(0x1F, 0x1F, 0x1F), RGB_(0x2B, 0x2B, 0x2B), RGB_(0x00, 0x00, 0x00),
    RGB_(0x33, 0x33, 0x33), RGB_(0x2E, 0x2E, 0x2E), RGB_(0x76, 0x76, 0x76),
    RGB_(0xFF, 0xFF, 0xFF), RGB_(0x9A, 0x9A, 0x9A), RGB_(0x6E, 0x6E, 0x6E),
    RGB_(0x4C, 0x4C, 0x4C), RGB_(0x00, 0x78, 0xD7), RGB_(0x00, 0x9B, 0xE4),
    RGB_(0x7F, 0x7F, 0x7F), RGB_(0x33, 0x66, 0xCC), RGB_(0xFF, 0xFF, 0xFF),
    RGB_(0x8A, 0x8A, 0x8A), RGB_(0xB0, 0xB0, 0xB0), RGB_(0x00, 0x00, 0x00),
    RGB_(0x2B, 0x2B, 0x2B),
};
/* InPrivate keeps its grey identity in light mode (light/inprivate.png) */
static const EdgeTh TH_LIGHT_PRIV = {
    RGB_(0xE7, 0xE7, 0xE7), RGB_(0xE7, 0xE7, 0xE7), RGB_(0xE7, 0xE7, 0xE7),
    RGB_(0xDE, 0xDE, 0xDE), RGB_(0xE7, 0xE7, 0xE7), RGB_(0xCE, 0xCF, 0xCE),
    RGB_(0x26, 0x26, 0x26), RGB_(0x6E, 0x70, 0x6E), RGB_(0x4A, 0x49, 0x4A),
    RGB_(0xA5, 0xA6, 0xA5), RGB_(0x00, 0x78, 0xD7), RGB_(0x00, 0x62, 0xB1),
    RGB_(0x7F, 0x7F, 0x7F), RGB_(0x06, 0x45, 0xAD), RGB_(0xE7, 0xE7, 0xE7),
    RGB_(0x4A, 0x49, 0x4A), RGB_(0x6E, 0x70, 0x6E), RGB_(0xE7, 0xE7, 0xE7),
    RGB_(0xE7, 0xE7, 0xE7),
};
static int g_light = 1; /* system default: light */
static const EdgeTh *g_th = &TH_LIGHT;
static void theme_update(void);
/* defined after the renderer/model: flips both the chrome palette and
 * the UIX_TH table shared with fingerkb/shellchrome/uixctl */
static void theme_update(void);
#define C_BAR (g_th->bar)
#define C_SHEET (g_th->sheet)
#define C_PAGE (g_th->page)
#define C_CARD (g_th->card)
#define C_BTN (g_th->btn)
#define C_SEP (g_th->sep)
#define C_INK (g_th->ink)
#define C_MUT (g_th->mut)
#define C_DIM (g_th->dim)
#define C_DIS (g_th->dis)
#define C_BLUE (g_th->blue)
#define C_LINK (g_th->link)
#define C_SCRIM (g_th->scrim)
#define C_WLINK (g_th->wlink)
#define C_PAGEBG (g_th->pagebg)

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
static int xstr_contains(const char *hay, const char *needle)
{
    int nl = xstrlen(needle);
    if (!nl)
        return 0;
    for (const char *p = hay; *p; ++p) {
        int i = 0;
        while (i < nl && p[i] && p[i] == needle[i])
            ++i;
        if (i == nl)
            return 1;
    }
    return 0;
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

/* ---- shared-component render context ----
 * Serves uixctl.c (controls), svf.c (icons) and fingerkb.dll (keyboard):
 * the same vtable model as uixrender.dll. The UIX_TH table follows the
 * app theme (light system default, 't' toggles). */
static const U32 edge_theme_dark[] = {
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
    RGB_(0xBD, 0xBE, 0xBD), /* 12 NAV_INK   nav bar glyphs (grey, settings_1.png) */
};
static const U32 edge_theme_light[] = {
    RGB_(0xF7, 0xF3, 0xF7), /* 0 SURFACE   keyboard backdrop        */
    RGB_(0xFF, 0xFF, 0xFF), /* 1 KEY        normal key             */
    RGB_(0xE7, 0xE7, 0xE7), /* 2 KEY_ALT    shift/&123             */
    RGB_(0x00, 0x00, 0x00), /* 3 INK        key text               */
    RGB_(0x00, 0x78, 0xD7), /* 4 KEY_GO     enter/search key fill  */
    RGB_(0xFF, 0xFF, 0xFF), /* 5 GO_INK     glyph on the go key    */
    RGB_(0x00, 0x78, 0xD7), /* 6 ACCENT     checked/caret/links    */
    RGB_(0xFF, 0xFF, 0xFF), /* 7 CTRL_BG    button fill            */
    RGB_(0x00, 0x00, 0x00), /* 8 CTRL_INK   button label           */
    RGB_(0x76, 0x76, 0x76), /* 9 CTRL_EDGE  unchecked border       */
    RGB_(0x76, 0x76, 0x76), /* 10 FIELD_EDGE input border/hint     */
    RGB_(0xCE, 0xCF, 0xCE), /* 11 NAV_BG    nav bar strip          */
    RGB_(0x31, 0x30, 0x31), /* 12 NAV_INK   nav bar glyphs         */
};
static const U32 *edge_theme = edge_theme_light;

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
static EdgeRender edge_renderer = {
    0x55495831u, r_clip,  r_fill,     r_text, r_line,
    r_ellipse,   r_polygon, r_surface, edge_theme_light,
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

/* ---- MARKDOWN.DLL host hooks ----
 * The engine calls back through this table; every painter funnels into
 * the same primitives the chrome uses (fill / draw_text / SVF / NT APIs). */
int mdh_read_file(const char *name, unsigned char *buf, int cap)
{
    W16 path[40];
    int n = 0;
    path[n++] = '\\'; path[n++] = 'f'; path[n++] = 'l'; path[n++] = 's';
    path[n++] = '0'; path[n++] = '\\';
    for (int i = 0; name[i] && n < 38; ++i)
        path[n++] = (W16)(unsigned char)name[i];
    path[n] = 0;
    void *h = NT_CALL(CreateFileW, (path, 0x80000000u, 1, 0, 3, 0, 0));
    if (!h || h == (void *)(intptr_t)-1)
        return 0;
    U32 got = 0;
    int ok = NT_CALL(ReadFile, (h, buf, (U32)cap, &got, 0));
    NT_CALL(CloseHandle, (h));
    return ok && got ? (int)got : 0;
}
__attribute__((dllexport)) int mdh_read_file_exp(const char *n, unsigned char *b, int c)
{
    return mdh_read_file(n, b, c);
}
static void mdh_fill(void *dc, int l, int t, int r, int b, unsigned c)
{
    fill(dc, l, t, r, b, c);
}
static void mdh_text(void *dc, int x, int y, const char *s, int len,
                     unsigned c, int h, int flags)
{
    char tmp[110];
    if (len > 105)
        len = 105;
    for (int i = 0; i < len; ++i)
        tmp[i] = s[i];
    tmp[len] = 0;
    draw_text(dc, x, y, tmp, c, h, 0, flags & MDTF_BOLD);
    if (flags & MDTF_STRIKE)
        fill(dc, x, y + h / 2, x + text_w(tmp, h), y + h / 2 + 1, c);
}
static int mdh_text_w(const char *s, int len, int h)
{
    char tmp[110];
    if (len > 105)
        len = 105;
    for (int i = 0; i < len; ++i)
        tmp[i] = s[i];
    tmp[len] = 0;
    return text_w(tmp, h);
}
static void mdh_svf(void *dc, const unsigned char *data, unsigned len,
                    int x, int y, int w, int h)
{
    /* SVF recolouring is pigment-aware: neutral (near-white) art —
     * VEC assets authored for dark tiles, like logo.svf — takes the
     * black tint so it stays visible on the white page, while real
     * colour illustrations keep their palette (svf.c ink()). */
    SVFDrawTint(dc, data, len, x, y, w, h, RGB_(0x00, 0x00, 0x00));
}
static void mdh_ellipse(void *dc, int l, int t, int r, int b, unsigned c,
                        int filled)
{
    int cx = (l + r) / 2, cy = (t + b) / 2;
    int rx = (r - l) / 2, ry = (b - t) / 2;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    r_ellipse(dc, cx, cy, rx, ry, c, filled ? (rx < ry ? rx : ry) : 1);
}
static int mdh_chakra(const char *script)
{
    /* <script> bodies go to CHAKRA.DLL (JsRunScript). The stub evaluates
     * a tiny expression subset and ignores the rest - the POC's whole
     * scripting story in one hop. */
    static W16 wbuf[512];
    int n = 0;
    while (script[n] && n < 500) {
        wbuf[n] = (W16)(unsigned char)script[n];
        ++n;
    }
    wbuf[n] = 0;
    return NT_CALL(JsRunScript, (wbuf, 0, 0, 0));
}
static MdHost g_mdhost = {
    0, mdh_fill, mdh_text, mdh_text_w, mdh_svf, mdh_ellipse,
    mdh_read_file, mdh_chakra,
};

/* web view state (MARKDOWN.DLL engine pages; kept theme/model-free) */
static int web_kind;
static int web_scroll_x, web_scroll_y;
static int web_reader_on;
static char web_loaded_url[96]; /* engine page currently loaded */

/* ---- find-in-page helpers ---- */
static void find_run(void)
{
    find_nmatch = 0;
    find_active = 0;
    if (find_len && web_kind)
        find_nmatch = NT_CALL(md_find, (find_query, fm_x, fm_y, fm_w, 24));
    if (find_active >= find_nmatch)
        find_active = 0;
}
static void paint_find_highlights(void *dc, int clip_bottom)
{
    /* no alpha blending on this display: draw the highlight box, then
     * repaint the matched text on top in ITS OWN run metrics — the engine
     * returns the op's font height + style flags per match (md_find_style),
     * so a bold 22px heading repaints as bold 22px and a code line as it
     * was laid out; the box grows to the full line box (fh + 4). */
    for (int i = 0; i < find_nmatch; ++i) {
        int fh = 14, ff = 0;
        NT_CALL(md_find_style, (i, &fh, &ff));
        int y = fm_y[i] - web_scroll_y;
        int x = fm_x[i] - web_scroll_x;
        if (y + fh + 4 > clip_bottom || y < 0)
            continue;
        U32 c = i == find_active ? RGB_(0xFF, 0x8C, 0x00) : RGB_(0xFF, 0xD7, 0x00);
        fill(dc, x - 2, y, x + fm_w[i] + 2, y + fh + 4, c);
        const char *mt = NT_CALL(md_find_text, (i, 0));
        if (!mt)
            mt = find_query;
        draw_text(dc, x, y + 2, mt, RGB_(0x00, 0x00, 0x00), fh, 0,
                  (ff & 1) /*MDTF_BOLD*/);
    }
}

/* ---- model ---- */
#define MAX_TABS 8
#define MAX_URL 96
typedef struct {
    char title[40];
    char url[MAX_URL];
    char host[40];
    char file[MAX_URL]; /* \fls0-relative path for file:// pages */
    int is_file;        /* file:// activation (opened from File Explorer) */
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

static UxFile ux_dark, ux_light_file;
static UxFile *g_uxp = &ux_light_file; /* light is the system default */
#define ux (*g_uxp)
static Tab tabs[MAX_TABS];
static Tab closed_tabs[4];
static int closed_count;
static int tab_count = 1, tab_cur;

static void theme_update(void)
{
    if (!g_light)
        g_th = &TH_DARK;
    else
        g_th = tabs[tab_cur].priv ? &TH_LIGHT_PRIV : &TH_LIGHT;
    edge_theme = g_light ? edge_theme_light : edge_theme_dark;
    edge_renderer.theme = edge_theme;
    g_uxp = g_light ? &ux_light_file : &ux_dark;
}

static int mode; /* see enum below */
enum {
    M_BROWSER = 0, M_MENU, M_TABS, M_HUB, M_ADDRESS, M_SETTINGS, M_CLEAR,
    M_DLG_READLIST, M_DLG_FAV, M_FIND, M_TABSMENU, M_ADVANCED, M_FEEDBACK,
};
static int hub_pivot; /* 0 favorites 1 reading 2 history 3 downloads */
static char addr_buf[MAX_URL];
static int addr_len;
static char toast_text[80];
static void *g_wnd;
static const EdgeKb *g_kb;
static const EdgeChrome *g_chrome;

/* settings pages: scroll + dropdown state (settings/advanced references) */
#define SET_VIEW_H 455
static int set_scroll;
static int dd_open; /* 0 closed 1 website preference 2 cookies */
static int website_sel; /* 0 Mobile version 1 Desktop version */
static int cookies_sel = 2; /* default value: Don't block cookies */
static const char *const WEBSITE_OPTS[2] = {"Mobile version", "Desktop version"};
static const char *const COOKIE_OPTS[3] = {"Block all cookies",
                                           "Block only third party cookies",
                                           "Don't block cookies"};
static int dragging, drag_y, drag_x, drag_moved;

static int settings_mode(void)
{
    return mode == M_SETTINGS || mode == M_ADVANCED || mode == M_FEEDBACK;
}

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
    if (xeq(name, "FindText"))
        return find_query;
    /* dropdown closed-box values follow the live selection */
    if (xeq(name, "WebsitePrefValue"))
        return WEBSITE_OPTS[website_sel & 1];
    if (xeq(name, "CookiesValue"))
        return COOKIE_OPTS[cookies_sel % 3];
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
    /* open-flyout rows: the selected option takes the accent fill */
    if (xeq(name, "DDRow0") || xeq(name, "DDRow1"))
        return (dd_open == 1 && website_sel == name[5] - '0') ? C_BLUE
                                                             : RGB_(0x1F, 0x1F, 0x1F);
    if (xeq(name, "DDRow2") || xeq(name, "DDRow3") || xeq(name, "DDRow4"))
        return (dd_open == 2 && cookies_sel == name[5] - '2') ? C_BLUE
                                                             : RGB_(0x1F, 0x1F, 0x1F);
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
            U32 c = dyn_color(name, ux_clr(&ux, n, UXA_COLOR, C_MUT));
            ic_rect(ifill, dc, r.x, r.y, r.w, r.h, 1, c);
        } else {
            U32 c = dyn_color(name, ux_clr(&ux, n, UXA_BG, C_INK));
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
            int und = ux_int(&ux, n, UXA_UNDERLINE, 0);
            if (und && align != 1) {
                /* draw first so the rule never clips the glyphs */
                int w = text_w(t, size);
                int ux0 = align == 2 ? r.x + r.w - w : r.x;
                fill(dc, ux0, ty + size - 1, ux0 + w, ty + size, col);
            }
            draw_text(dc, tx, ty, t, col, size, align, bold);
            if (und && align == 1) {
                int w = text_w(t, size);
                fill(dc, r.x + (r.w - w) / 2, ty + size - 1,
                     r.x + (r.w + w) / 2, ty + size, col);
            }
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
        int dim = ux_int(&ux, n, UXA_DISABLED, 0);
        ctl_toggle_ex(dc, &edge_renderer, r.x, r.y, on, dim);
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
#define LNK RGB_(0x06, 0x45, 0xAD) /* link blue on white pages (any theme) */

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

/* light chrome geometry: the browser toolbar strip sits at y432-479 with
 * the system nav bar below (light refs); dark keeps it at y480-527 */
static int chrome_yoff(void)
{
    return g_light ? -48 : 0;
}

static void paint_page(void *dc)
{
    Tab *t = &tabs[tab_cur];
    if (!t->url[0]) {
        if (t->priv) {
            if (g_light) {
                /* light InPrivate (light/inprivate.png): grey surfaces,
                 * dark copy, blue link */
                fill(dc, 0, 0, 320, 480, C_PAGEBG);
                draw_text(dc, 36, 130, "Browsing InPrivate", C_INK, 22, 0, 1);
                static const char *const PL[] = {
                    "When you use InPrivate tabs, your",
                    "browsing data (like cookies, history,",
                    "or temporary files) isn't saved on",
                    "your device after you're done.",
                    "Microsoft Edge deletes temporary",
                    "data from your device after all of",
                    "your InPrivate tabs are closed.",
                };
                for (unsigned i = 0; i < sizeof(PL) / sizeof(PL[0]); ++i)
                    draw_text(dc, 36, 183 + i * 21, PL[i], C_INK, 14, 0, 0);
                draw_text(dc, 36, 344, "Read the Microsoft privacy statement",
                          C_LINK, 14, 0, 0);
                fill(dc, 36, 358, 36 + text_w("Read the Microsoft privacy statement", 14), 359, C_LINK);
            } else {
                /* dark InPrivate new tab: uXAML page over the content host */
                const UxDoc *d = ux_find_doc(&ux, "InPrivatePage");
                if (d) {
                    layout_doc(d, 0, 0, 320, 528);
                    paint_node(dc, d->root);
                }
            }
        } else {
            /* home state per home.png / home_empty light: plain page */
            fill(dc, 0, 0, 320, 480, C_PAGEBG);
        }
        return;
    }
    /* engine pages: MARKDOWN.DLL paints the scrolled viewport. File
     * activations from File Explorer (file://) route through the same
     * engine as mock-http pages. */
    if (web_kind) {
        int vh = 480 + chrome_yoff();
        clip_set(0, 0, 320, vh);
        g_mdhost.dc = dc;
        NT_CALL(md_paint, (&g_mdhost, web_scroll_x, web_scroll_y, 320, vh));
        clip_set(0, 0, 320, 528);
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
    if (t->is_file) {
        /* local file: single-tone path (no subdomain/domain split) */
        draw_text_clamp(dc, r.x, r.y, t->file, C_INK, 12, 126);
        return;
    }
    /* Two-tone URL like the reference bar: the registrable domain (last
     * two labels) is the strong ink; everything else is dimmed — a
     * leading subdomain ("en." of en.wikipedia.org) AND the path/query
     * ("/wiki/Page") render in C_MUT. A bare 2-label host stays all-ink.
     * host = t->host (up to the first '/'); the tail of t->url past it
     * is the path. */
    const char *h = t->host;
    int hl = 0;
    while (h[hl])
        ++hl;
    int ld = -1; /* last dot */
    for (int i = 0; i < hl; ++i)
        if (h[i] == '.')
            ld = i;
    int pd = -1; /* dot before it, if any */
    for (int i = 0; i < ld; ++i)
        if (h[i] == '.')
            pd = i;
    int sub_len = (ld > 0 && pd >= 0) ? pd + 1 : 0; /* "en." incl. dot */
    int dom_len = hl - sub_len;
    int x_limit = r.x + 150; /* address field right edge (markup) */
    int x = r.x;
    if (sub_len) {
        char pre[24];
        int n = sub_len > 23 ? 23 : sub_len;
        xcopy(pre, h, n);
        pre[n] = 0;
        draw_text_clamp(dc, x, r.y, pre, C_MUT, 12, 40);
        x += text_w(pre, 12);
    }
    {
        char dom[40];
        int n = dom_len > 39 ? 39 : dom_len;
        xcopy(dom, h + sub_len, n);
        dom[n] = 0;
        draw_text_clamp(dc, x, r.y, dom, C_INK, 12, 110);
        x += text_w(dom, 12);
    }
    /* path + query: everything in t->url past the host, dimmed like the
     * subdomain (reference: en.wikipedia.org/some-page greys both the
     * "en." and the "/some-page", the domain stays bright) */
    if (t->url[hl] == '/' && x_limit - x > 8) {
        const char *path = t->url + hl;
        draw_text_clamp(dc, x, r.y, path, C_MUT, 12, x_limit - x);
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

#define C_TABTITLE (g_th->tabtitle)
#define C_CROSS   (g_th->cross)

static void paint_tab_cards(void *dc)
{
    for (int i = 0; i < tab_count; ++i) {
        int col = i % 2, row = i / 2;
        int cx = card_col_x(col), cy = 44 + row * 148;
        if (cy > 420)
            break;
        Tab *t = &tabs[i];
        /* header strip: #1F1F1F, 30px tall. Reference tabs.png card0:
         * favicon starts 12px from the card edge (card x~10, icon 22-34),
         * title 6px after the icon, X ~4px from the right edge. */
        fill(dc, cx, cy, cx + 142, cy + 30, C_BAR);
        if (t->priv)
            ic_draw(ifill, dc, cx + 11, cy + 9, 13, C_MUT, IC_INPRIVATE);
        else
            ic_draw(ifill, dc, cx + 11, cy + 9, 13, C_MUT, IC_TABS);
        draw_text_clamp(dc, cx + 31, cy + 10, t->title, C_TABTITLE, 11, 92);
        ic_draw(ifill, dc, cx + 124, cy + 9, 12, C_CROSS, IC_CLOSE);
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

/* ---- settings pages on uXAML (settings/advanced/feedback refs) ----
 * Content taller than the viewport scrolls (drag); the system nav bar
 * comes from the imported SHELLCHROME chrome and paints last at y488. */
static const char *settings_docname(void)
{
    return mode == M_ADVANCED  ? "AdvancedPage"
         : mode == M_FEEDBACK ? "FeedbackPage"
                              : "SettingsPage";
}

static int settings_content_h(void)
{
    return mode == M_ADVANCED ? 1130 : mode == M_FEEDBACK ? 470 : 1010;
}

static void paint_settings_page(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, settings_docname());
    if (!d)
        return;
    int total = settings_content_h();
    int max_scroll = total - SET_VIEW_H;
    if (max_scroll < 0)
        max_scroll = 0;
    if (set_scroll > max_scroll)
        set_scroll = max_scroll;
    clip_set(0, 0, 320, 480);
    layout_doc(d, 0, -set_scroll, 320, 528);
    paint_node(dc, d->root);
    clip_set(0, 0, 320, 528);
    /* scrollbar per settings_2_advanced_scolled.png: 2px track at the
     * right edge, proportional thumb, chevrons at both ends; the bar
     * stops at y477 — the nav bar owns everything from y480 down */
    if (max_scroll > 0 && set_scroll > 0) { /* W10M fades the bar at rest */
        fill(dc, 316, 4, 318, 477, RGB_(0x1A, 0x1A, 0x1A));
        int track_h = 446; /* y16..462, between the two chevrons */
        int thumb_h = SET_VIEW_H * track_h / total;
        if (thumb_h < 24)
            thumb_h = 24;
        if (thumb_h > track_h)
            thumb_h = track_h;
        int ty = 16 + set_scroll * (track_h - thumb_h) / max_scroll;
        fill(dc, 316, ty, 318, ty + thumb_h, RGB_(0x76, 0x76, 0x76));
        ic_draw(ifill, dc, 310, 2, 10, C_MUT, IC_CHEVD);
        ic_draw(ifill, dc, 310, 464, 10, C_MUT, IC_CHEVU);
    }
    /* open flyout over the dropdown box (selected row aligned on the box) */
    if (dd_open) {
        const UxDoc *f = ux_find_doc(&ux, dd_open == 1 ? "WebsitePrefFlyout"
                                                     : "CookiesFlyout");
        int bi = find_node(dd_open == 1 ? "WebsitePrefDD" : "CookiesDD");
        if (f && bi >= 0) {
            int sel = dd_open == 1 ? website_sel : cookies_sel;
            int fy = nrect[bi].y - 12 - sel * 33;
            layout_doc(f, 14, fy, 284, 528);
            paint_node(dc, f->root);
        }
    }
    /* system navigation bar: imported SHELLCHROME chrome, dark theme */
    if (g_chrome)
        g_chrome->paint(dc);
}
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
    theme_update();
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

/* ---- web view: the MARKDOWN.DLL engine serves pages ----
 * The POC is offline: the router pretends to be online by serving embedded
 * content for known sites (mock DNS) and reading fls0 files for file://
 * and .md/.html URLs. web_kind: 0 = procedural mock, 1 = engine page. */
#include "mockweb.h"
static int web_serve(const char *host, const char *url)
{
    /* mock DNS table: host substring -> embedded source */
    int dialect = MD_SRC_MD;
    const char *body = 0;
    if (xstr_contains(url, "test5526c") || xstr_contains(host, "acid1")) {
        dialect = MD_SRC_HTML;
        body = MW_ACID1;
    } else if (xstr_contains(host, "classpad") &&
               !xstr_contains(host, "classpaddev")) {
        body = MW_WIKI_CP300; /* wikipedia Casio ClassPad 300 */
    } else if (xstr_contains(host, "classpaddev") || xstr_contains(host, "hh3") ||
               xstr_contains(host, "hollyhock")) {
        body = MW_CWIKI_FAQ;
    } else {
        return 0;
    }
    return NT_CALL(md_load, (dialect, body, xstrlen(body))) ? 1 : 0;
}

static int web_open_file(const char *name)
{
    static unsigned char fbuf[20480];
    int n = mdh_read_file(name, fbuf, sizeof fbuf - 1);
    if (n <= 0)
        return 0;
    fbuf[n] = 0;
    int dialect = MD_SRC_MD;
    int l = xstrlen(name);
    if (l > 5 && (xeq(name + l - 5, ".html") || xeq(name + l - 4, ".htm")))
        dialect = MD_SRC_HTML;
    else if (l > 4 && xeq(name + l - 4, ".txt"))
        dialect = MD_SRC_TEXT;
    return NT_CALL(md_load, (dialect, (const char *)fbuf, n)) ? 1 : 0;
}

/* load a url into the engine; returns web_kind for the tab */
static int web_load(const char *url)
{
    const char *p = url;
    if (p[0] == 'f' && p[1] == 'i' && p[2] == 'l' && p[3] == 'e' &&
        p[4] == ':' && p[5] == '/' && p[6] == '/') {
        p += 7;
        if (p[0] == '/')
            ++p;
        if (web_open_file(p)) {
            xcopy(web_loaded_url, p, xstrlen(p) + 1);
            NT_CALL(md_layout, (320));
            return 1;
        }
        return 0;
    }
    if (web_serve("", url)) {
        xcopy(web_loaded_url, url, xstrlen(url) + 1);
        NT_CALL(md_layout, (320));
        return 1;
    }
    /* host-part classification */
    char host[48];
    int hl = 0;
    while (url[hl] && url[hl] != '/' && hl < 47)
        ++hl;
    xcopy(host, url, hl);
    host[hl] = 0;
    if (web_serve(host, url)) {
        xcopy(web_loaded_url, url, xstrlen(url) + 1);
        NT_CALL(md_layout, (320));
        return 1;
    }
    /* local file by name (.md/.html/.txt from fls0) */
    int l = xstrlen(url);
    if (l > 3 && (xeq(url + l - 3, ".md") || xeq(url + l - 5, ".html") ||
                  xeq(url + l - 4, ".htm") || xeq(url + l - 4, ".txt"))) {
        if (web_open_file(url)) {
            xcopy(web_loaded_url, url, xstrlen(url) + 1);
            NT_CALL(md_layout, (320));
            return 1;
        }
    }
    return 0;
}

static void web_view_reset(void)
{
    web_scroll_x = web_scroll_y = 0;
}

static void nav_url(const char *url)
{
    Tab *t = &tabs[tab_cur];
    const char *p = url;
    t->is_file = 0;
    t->file[0] = 0;
    /* file:// activation (File Explorer "open in Edge"): the address bar
     * keeps the full URL, the md engine gets the \fls0-relative path. */
    if (p[0] == 'f' && p[1] == 'i' && p[2] == 'l' && p[3] == 'e' && p[4] == ':' &&
        p[5] == '/' && p[6] == '/') {
        const char *path = p + 7;
        int n = xstrlen(p);
        if (n > MAX_URL - 1)
            n = MAX_URL - 1;
        xcopy(t->url, p, n);
        t->url[n] = 0;
        /* \fls0-relative name for md_read_file (it prepends \fls0\) */
        const char *rel = path;
        if (rel[0] == '\\' && rel[1] == 'f' && rel[2] == 'l' && rel[3] == 's' &&
            rel[4] == '0' && rel[5] == '\\')
            rel += 6;
        else if (rel[0] == '/' && rel[1] == 'f' && rel[2] == 'l' && rel[3] == 's' &&
                 rel[4] == '0' && rel[5] == '/')
            rel += 6;
        int m = xstrlen(rel);
        if (m > MAX_URL - 1)
            m = MAX_URL - 1;
        xcopy(t->file, rel, m);
        t->file[m] = 0;
        t->is_file = 1;
        /* title + display host: the file name (last path segment) */
        const char *name = rel + xstrlen(rel);
        while (name > rel && name[-1] != '\\' && name[-1] != '/')
            --name;
        int tl = xstrlen(name);
        if (tl > 39)
            tl = 39;
        xcopy(t->title, name, tl);
        t->title[tl] = 0;
        xcopy(t->host, name, tl);
        t->host[tl] = 0;
        t->hash = xhash(t->url);
        if (!t->title[0]) {
            xcopy(t->title, "File", 5);
            t->title[4] = 0;
        }
        hist_add(t->file, t->title);
        /* load through the engine now that the tab is set up */
        web_view_reset();
        web_kind = web_open_file(t->file) ? 1 : 0;
        if (web_kind) {
            int tl2 = 0;
            const char *dt = NT_CALL(md_title, (&tl2));
            if (dt && tl2 > 0) {
                if (tl2 > 39)
                    tl2 = 39;
                xcopy(t->title, dt, tl2);
                t->title[tl2] = 0;
            }
        }
        set_lock_flag();
        return;
    }
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
    web_view_reset();
    web_kind = web_load(t->url);
    const char *title = t->host;
    for (unsigned s = 0; s < 6; ++s)
        if (xeq(t->host, SITES[s].host))
            title = SITES[s].label;
    if (web_kind) {
        int tl2 = 0;
        const char *dt = NT_CALL(md_title, (&tl2));
        if (dt && tl2 > 0 && !xstr_contains(t->url, "wikipedia.org"))
            title = dt; /* <title>/h1 wins for engine pages (not the wiki mock) */
    }
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
    int yo = chrome_yoff();
    layout_doc(d, 0, yo, 320, 528);
    paint_node(dc, d->root); /* chrome incl. PageHost background */
    clip_set(0, 0, 320, 480 + yo);
    paint_page(dc);
    clip_set(0, 0, 320, 528);
    paint_address_url(dc);
    if (g_light) {
        /* nav strip visible below the raised toolbar */
        if (g_chrome)
            g_chrome->paint(dc);
    }
}

static void paint_all(void *dc)
{
    const UxDoc *d;
    if (mode == M_BROWSER || mode == M_FIND) {
        /* reading-view affordance: the chrome book icon lights up when the
         * engine can serve the current page in reader mode (smallscreen
         * refs keep the greyed look for non-reader pages) */
        if (web_kind) {
            U32 rc = web_reader_on ? RGB_(0x00, 0x78, 0xD7) : RGB_(0x00, 0x00, 0x00);
            ic_draw(ifill, dc, 236, 495 + chrome_yoff(), 22, rc, IC_READER);
        }
        if (mode == M_FIND && find_sip) {
            /* with the SIP up the find bar rides above the keyboard */
            clip_set(0, 0, 320, 223);
            paint_browser(dc);
            paint_find_highlights(dc, 223);
            clip_set(0, 0, 320, 528);
            if ((d = ux_find_doc(&ux, "FindOnPageBar"))) {
                layout_doc(d, 0, 223 - 480, 320, 528);
                paint_node(dc, d->root);
                /* typed query + caret in the field: theme ink (white on
                 * the dark bar, black on the light one) like the markup's
                 * FindText node (#FFFFFF -> #000000 in the light build) */
                if (find_len) {
                    draw_text(dc, 12, 240, find_query, C_INK, 14, 0, 0);
                    int cx = 12 + text_w(find_query, 14);
                    if (cx < 150)
                        fill(dc, cx + 2, 231, cx + 4, 259, C_BLUE);
                }
            }
            if (g_kb)
                g_kb->paint(dc); /* search-scope go key set on open */
        } else {
            paint_browser(dc);
            if (mode == M_FIND) {
                paint_find_highlights(dc, 480);
                if ((d = ux_find_doc(&ux, "FindOnPageBar"))) {
                    layout_doc(d, 0, 0, 320, 528);
                    paint_node(dc, d->root);
                    if (find_len)
                        draw_text(dc, 12, 497, find_query, C_INK, 14, 0, 0);
                }
            }
        }
    } else if (mode == M_MENU) {
        paint_browser(dc);
        if ((d = ux_find_doc(&ux, "ActionsMenu"))) {
            layout_doc(d, 0, chrome_yoff(), 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_TABS || mode == M_TABSMENU) {
        if ((d = ux_find_doc(&ux, "TabCenterPage"))) {
            layout_doc(d, 0, chrome_yoff(), 320, 528);
            /* doc first (page, "Tabs" title, toolbar strip), then the
             * cards clipped to the content area: a 6th tab's row 2 stops at
             * the toolbar like on the device (manytabs.png). Theme split is
             * reference-true: dark tabs.png = the tab toolbar owns the full
             * strip; light tabs_menu.png = toolbar raised 48px with the
             * shared SHELLCHROME nav bar below it. */
            paint_node(dc, d->root);
            clip_set(0, 0, 320, 480 + chrome_yoff());
            paint_tab_cards(dc);
            clip_set(0, 0, 320, 528);
            if (g_light && g_chrome)
                g_chrome->paint(dc);
        }
        if (mode == M_TABSMENU && (d = ux_find_doc(&ux, "TabsContextMenu"))) {
            layout_doc(d, 0, chrome_yoff(), 320, 528);
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
    } else if (mode == M_SETTINGS || mode == M_ADVANCED || mode == M_FEEDBACK) {
        paint_settings_page(dc);
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
        if (g_kb) {
            g_kb->dock(0);
            g_kb->context(0); /* go key: arrow (address entry) */
        }
    } else if (xeq(cmd, "reload"))
        toast("Refreshing...");
    else if (xeq(cmd, "reader")) {
        if (web_kind) {
            web_reader_on = !web_reader_on;
            NT_CALL(md_reader, (web_reader_on));
            NT_CALL(md_layout, (320));
            web_view_reset();
        } else {
            web_reader_on = 0;
            toast("This page can't be shown in reading view");
        }
    }
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
    else if (xeq(cmd, "advanced")) {
        mode = M_ADVANCED;
        set_scroll = 0;
    } else if (xeq(cmd, "feedback")) {
        mode = M_FEEDBACK;
        set_scroll = 0;
    } else if (xeq(cmd, "dd-websitepref"))
        dd_open = dd_open == 1 ? 0 : 1;
    else if (xeq(cmd, "dd-cookies"))
        dd_open = dd_open == 2 ? 0 : 2;
    else if (xeq(cmd, "ddsel-0")) { website_sel = 0; dd_open = 0; }
    else if (xeq(cmd, "ddsel-1")) { website_sel = 1; dd_open = 0; }
    else if (xeq(cmd, "ddsel-2")) { cookies_sel = 0; dd_open = 0; }
    else if (xeq(cmd, "ddsel-3")) { cookies_sel = 1; dd_open = 0; }
    else if (xeq(cmd, "ddsel-4")) { cookies_sel = 2; dd_open = 0; }
    else if (xeq(cmd, "find-type")) {
        find_sip = 1;
        if (g_kb)
            g_kb->context(1); /* go key: search magnifier (find field) */
    }
    else if (xeq(cmd, "find-prev")) {
        if (find_nmatch)
            find_active = (find_active + find_nmatch - 1) % find_nmatch;
    }
    else if (xeq(cmd, "find-next")) {
        if (find_nmatch)
            find_active = (find_active + 1) % find_nmatch;
    }
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
               xeq(cmd, "helpabout") || xeq(cmd, "priv-link") ||
               xeq(cmd, "fav-folder") || xeq(cmd, "find-type") ||
               xeq(cmd, "find-prev") || xeq(cmd, "find-next") ||
               xeq(cmd, "link-account") || xeq(cmd, "link-devsync") ||
               xeq(cmd, "link-learnmore") || xeq(cmd, "link-savedpwd") ||
               xeq(cmd, "link-changeengine") || xeq(cmd, "link-binghist") ||
               xeq(cmd, "link-terms") || xeq(cmd, "link-privacy") ||
               xeq(cmd, "link-report") || xeq(cmd, "link-openfb") ||
               xeq(cmd, "dd-style") || xeq(cmd, "dd-font") ||
               xeq(cmd, "find-close"))
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
         * 142x146 cards at x12/x166, top y44, pitch 148. The light
         * chrome raises the toolbar, so the grid stops 48px higher. */
        int grid_bottom = 470 + chrome_yoff();
        if (x >= 12 && x < 308 && y >= 44 && y < grid_bottom) {
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
    if (mode == M_FIND && find_sip) {
        /* find bar rides above the SIP at y223..271; keys below */
        if (y >= 317 && g_kb) {
            U32 lp2 = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int c = g_kb->input(WM_DOWN_, 1, lp2);
            if (c == 13) { /* go: keep the bar, drop the keyboard */
                find_sip = 0;
                find_run();
            } else if (c == 8) {
                if (find_len)
                    find_query[--find_len] = 0;
                find_run();
            } else if (c >= 32 && c < 127 && find_len < 38) {
                find_query[find_len++] = (char)c;
                find_query[find_len] = 0;
                find_run();
            }
            return;
        }
        if (y >= 223 && y < 317) { /* the bar strip (223..271) + kb backdrop */
            if (y < 271 && (d = ux_find_doc(&ux, "FindOnPageBar")) &&
                (b = hit_test(d, x, y))) {
                do_command(ux_str(&ux, b, UXA_COMMAND, ""));
                return;
            }
            return; /* dead backdrop: swallow */
        }
        mode = M_BROWSER; /* tap the page: dismiss the find bar */
        find_sip = 0;
        return;
    }
    if (settings_mode()) {
        /* settings-family pages: flyout taps, then page buttons */
        if (dd_open) {
            const char *fd = dd_open == 1 ? "WebsitePrefFlyout" : "CookiesFlyout";
            if ((d = ux_find_doc(&ux, fd)) && (b = hit_test(d, x, y))) {
                do_command(ux_str(&ux, b, UXA_COMMAND, ""));
                return;
            }
            dd_open = 0; /* light dismiss */
        }
        const char *sdoc = settings_docname();
        if ((d = ux_find_doc(&ux, sdoc)) && (b = hit_test(d, x, y))) {
            if (b->kind != UX_CHECK && b->kind != UX_TOGGLE) {
                const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
                if (cmd && *cmd)
                    do_command(cmd);
            }
        }
        return;
    }
    const char *docn = mode == M_MENU ? "ActionsMenu"
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
    } else if (mode == M_BROWSER && web_kind && y > 45 && y < 480) {
        /* engine page: follow links (page coords include the scroll) */
        const char *href;
        int hlen;
        if (NT_CALL(md_link_at, (x + web_scroll_x, y + web_scroll_y, &href, &hlen))) {
            char target[MAX_URL];
            if (hlen > MAX_URL - 1)
                hlen = MAX_URL - 1;
            xcopy(target, href, hlen);
            target[hlen] = 0;
            /* relative hrefs resolve against the current page's "site" */
            if (target[0] == '/' || !(target[0] == 'h' || target[0] == 'f')) {
                /* same-site mock: keep the host, swap the path */
                char host[40];
                int hl = 0;
                while (web_loaded_url[hl] && web_loaded_url[hl] != '/' &&
                       hl < 39)
                    ++hl;
                xcopy(host, web_loaded_url, hl);
                host[hl] = 0;
                char abs[MAX_URL];
                int hn = xstrlen(host);
                xcopy(abs, host, hn);
                abs[hn] = '/';
                int tn = xstrlen(target);
                if (hn + 1 + tn < MAX_URL - 1) {
                    xcopy(abs + hn + 1, target, tn + 1);
                    xcopy(target, abs, hn + 1 + tn + 1);
                }
            }
            if (web_load(target)) {
                xcopy(web_loaded_url, target, xstrlen(target) + 1);
                web_view_reset();
                /* visible url + title follow the navigation */
                Tab *t = &tabs[tab_cur];
                int un = xstrlen(target);
                if (un > MAX_URL - 1)
                    un = MAX_URL - 1;
                xcopy(t->url, target, un);
                t->url[un] = 0;
                int thl = 0;
                while (target[thl] && target[thl] != '/' && thl < 39)
                    ++thl;
                xcopy(t->host, target, thl);
                t->host[thl] = 0;
                int tl2 = 0;
                const char *dt = NT_CALL(md_title, (&tl2));
                if (dt && tl2 > 0) {
                    if (tl2 > 39)
                        tl2 = 39;
                    xcopy(t->title, dt, tl2);
                    t->title[tl2] = 0;
                }
                set_lock_flag();
            } else {
                toast("This page can't be opened right now.");
            }
        }
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

static void nav_bar_action(int key)
{
    /* 4 = Back, 5 = Home, 6 = Search (SHELLCHROME hosted input codes) */
    if (key == 4) {
        if (mode == M_ADVANCED || mode == M_FEEDBACK)
            mode = M_SETTINGS;
        else if (mode == M_SETTINGS)
            mode = M_BROWSER;
        set_scroll = 0;
        dd_open = 0;
    } else if (key == 5) {
        mode = M_BROWSER;
        set_scroll = 0;
        dd_open = 0;
    } else if (key == 6) {
        toast("Search isn't available in this POC");
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
            else if (mode == M_ADVANCED || mode == M_FEEDBACK)
                mode = M_SETTINGS;
            else if (mode == M_TABSMENU)
                mode = M_TABS;
            else if (mode == M_SETTINGS)
                mode = M_BROWSER;
            else if (mode == M_FIND)
                find_sip = 0; /* hardware back: drop the keyboard first */
            else
                mode = M_BROWSER;
            set_scroll = 0;
            dd_open = 0;
        } else if (msg == WM_KEY_ && wp == 0x74) { /* 't' key: theme toggle */
            g_light = !g_light;
            theme_update();
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
        } else if (mode == M_ADDRESS) {
            addr_key(wp);
        } else if (mode == M_FIND && (wp >= 32 && wp < 127)) {
            /* find-on-page: live query through WM_CHAR (SIP typing) */
            if (find_len < 38) {
                find_query[find_len++] = (char)wp;
                find_query[find_len] = 0;
            }
            find_run();
        } else if (mode == M_BROWSER && (wp == 's' || wp == 'S')) {
            /* POC shortcut into the settings page (e2e reachability) */
            mode = M_SETTINGS;
            set_scroll = 0;
        } else if (mode == M_BROWSER && (wp == 'f' || wp == 'F')) {
            /* POC shortcut into the feedback page (reference feedback.png) */
            mode = M_FEEDBACK;
            set_scroll = 0;
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_ || msg == WM_MOVE_ || msg == WM_UP_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        if (msg == WM_DOWN_)
            toast_text[0] = 0; /* any input dismisses the toast */
        if (settings_mode()) {
            /* nav bar owns the y488..528 strip; content drags scroll */
            int r = g_chrome ? g_chrome->input(msg, wp, lp) : 0;
            if (r >= 4 && r <= 6) {
                nav_bar_action(r);
                NT_CALL(InvalidateRect, (hwnd, 0, 0));
                NT_CALL(UpdateWindow, (hwnd));
                return 0;
            }
            if (r == 1) {
                NT_CALL(InvalidateRect, (hwnd, 0, 0));
                NT_CALL(UpdateWindow, (hwnd));
                return 0; /* consumed by the bar */
            }
            if (msg == WM_DOWN_) {
                dragging = 1;
                drag_y = y;
                process_tap(x, y);
            } else if (msg == WM_MOVE_ && dragging) {
                int max_scroll = settings_content_h() - SET_VIEW_H;
                if (max_scroll < 0)
                    max_scroll = 0;
                set_scroll += drag_y - y;
                if (set_scroll > max_scroll)
                    set_scroll = max_scroll;
                if (set_scroll < 0)
                    set_scroll = 0;
                drag_y = y;
                dd_open = 0;
            } else if (msg == WM_UP_) {
                if (dragging) { /* final delta at lift-off */
                    int max_scroll = settings_content_h() - SET_VIEW_H;
                    if (max_scroll < 0)
                        max_scroll = 0;
                    set_scroll += drag_y - y;
                    if (set_scroll > max_scroll)
                        set_scroll = max_scroll;
                    if (set_scroll < 0)
                        set_scroll = 0;
                }
                dragging = 0;
            }
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
            return 0;
        }
        /* browser mode: engine pages drag-scroll (vertical always,
         * horizontal when the page is wider than the viewport) */
        if (msg == WM_DOWN_) {
            dragging = 1;
            drag_y = y;
            drag_x = x;
            drag_moved = 0;
        } else if (msg == WM_MOVE_ && dragging) {
            int dy = drag_y - y;
            int dx = drag_x - x;
            if (dy < -3 || dy > 3 || dx < -3 || dx > 3)
                drag_moved = 1;
            if (web_kind) {
                int pw = 320, ph = 0;
                NT_CALL(md_size, (&pw, &ph));
                web_scroll_y += dy;
                { int vh = 440 + chrome_yoff();
                  if (web_scroll_y > ph - vh) web_scroll_y = ph - vh; }
                if (web_scroll_y < 0)
                    web_scroll_y = 0;
                if (pw > 320) {
                    web_scroll_x += dx;
                    if (web_scroll_x > pw - 320)
                        web_scroll_x = pw - 320;
                    if (web_scroll_x < 0)
                        web_scroll_x = 0;
                }
                NT_CALL(InvalidateRect, (hwnd, 0, 0));
                NT_CALL(UpdateWindow, (hwnd));
            }
            drag_y = y;
            drag_x = x;
        } else if (msg == WM_UP_) {
            if (dragging && !drag_moved)
                process_tap(x, y);
            dragging = 0;
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
        }
        return 0;
    }
    return 0;
}

/* ---- markup load: \fls0\edge.uxaml (dark) + edgelight.uxaml ----
 * The markup is authored dark; uxaml_gen.py --light emits a light-palette
 * twin. Both load at boot and the active one follows the theme. */
#define UX_FILE_MAX (48 * 1024)
static unsigned char ux_blob[UX_FILE_MAX];
static unsigned char ux_blob_light[UX_FILE_MAX];

static int read_ux_file(const W16 *path, unsigned char *buf, U32 *out_size)
{
    void *h = NT_CALL(CreateFileW, (path, 0x80000000u, 0, 0, 3 /*OPEN_EXISTING*/, 0, 0));
    if (!h || h == (void *)(intptr_t)-1)
        return -1;
    U32 size = NT_CALL(GetFileSize, (h, 0));
    if (size == 0xFFFFFFFFu || size > UX_FILE_MAX) {
        NT_CALL(CloseHandle, (h));
        return -1;
    }
    U32 got = 0;
    int ok = NT_CALL(ReadFile, (h, buf, size, &got, 0));
    NT_CALL(CloseHandle, (h));
    if (!ok || got != size)
        return -1;
    *out_size = size;
    return 0;
}

static int load_markup(void)
{
    static const W16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 'e', 'd', 'g', 'e',
                               '.', 'u', 'x', 'a', 'm', 'l', 0};
    static const W16 pathl[] = {'\\', 'f', 'l', 's', '0', '\\', 'e', 'd', 'g', 'e',
                                'l', 'i', 'g', 'h', 't', '.', 'u', 'x', 'a', 'm', 'l', 0};
    U32 size = 0;
    const char *err = 0;
    if (read_ux_file(path, ux_blob, &size))
        return -1;
    if (ux_parse(ux_blob, size, &ux_dark, &err))
        return -1;
    U32 sizel = 0;
    if (!read_ux_file(pathl, ux_blob_light, &sizel)) {
        err = 0;
        ux_parse(ux_blob_light, sizel, &ux_light_file, &err);
    } else {
        ux_light_file = ux_dark; /* fallback: reuse the dark blob */
    }
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
    /* activation argument (device: \fls0\intent.dat via GetCommandLineW;
     * host: the CreateProcessW command line). A file:// argument opens
     * the document straight into the first tab, like tapping it in File
     * Explorer on the real phone. */
    {
        const W16 *cl = NT_CALL(GetCommandLineW, ());
        if (cl && cl[0] == 'f' && cl[1] == 'i' && cl[2] == 'l' && cl[3] == 'e') {
            char url[MAX_URL];
            int i = 0;
            while (cl[i] && i < MAX_URL - 1) {
                url[i] = (char)cl[i];
                ++i;
            }
            url[i] = 0;
            if (i > 7)
                nav_url(url);
        }
    }
    /* host-side IAT snap: uixctl.c + svf.c (linked into this DLL) share
     * the render-context slot the device loader would snap per module */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
    /* shared keyboard DLL (FINGERKB.DLL), snapped by the host loader */
    g_kb = NT_CALL(UIXGetSchemaFactory, ())
    , NT_CALL(md_set_host, (&g_mdhost));
    if (!g_kb || g_kb->profile != 0x55495831u)
        g_kb = 0;
    /* nav-bar chrome from SHELLCHROME.DLL (hosted-app surface) */
    g_chrome = NT_CALL(UIXChromeFactory, ());
    if (!g_chrome || g_chrome->profile != 0x55495831u)
        g_chrome = 0;
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
