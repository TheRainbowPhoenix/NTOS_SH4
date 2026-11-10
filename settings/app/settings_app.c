/* settings_app.c — "Settings" (W10M SystemSettings) POC for NTOS,
 * driven by uXAML markup.
 *
 * Reconstruction of Microsoft.Windows.SystemSettings_cw5n1h2txyewy (RS1,
 * 10.0.14393) from its XBF pages + en-US resources + the real-device
 * captures in refs/settings-screens (settings/analysis/
 * SYSTEMSETTINGS_UI_REFERENCE.md): the gear+"Settings" header, focused
 * "Find a setting" search, the L1 category list, L2 subcategory pages,
 * deep dives (Display sliders/toggles, Colors radios+swatches, Sounds
 * combobox/checkboxes, About value rows + the Edit-name flow page with
 * the shared SIP, USB toggles, Start preview + flyout, Active hours
 * with the hour picker) and the search results flow.
 *
 * Component reuse (same shared-DLL model as edge/files/cortana):
 *   - keyboard: FINGERKB.DLL module (host loader snaps UIXGetSchemaFactory)
 *   - navbar: SHELLCHROME.DLL chrome strip (Back/Home/Search)
 *   - controls + SVF: payload/phone/uixctl.c + svf.c compiled in (light)
 *   - uXAML engine: the exact edge/app/uxaml.c loader
 *   - base glyphs: files/app/files_icons.c (search/x/check/...)
 */
#include "nt_app.h"
#include "uxaml.h"
#include "files_icons.h"
#include "settings_icons.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

/* ---- shared phone-framework ABI (mirror of payload/phone/phone.h) ---- */
typedef uint64_t U64;

typedef struct SetRender {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme;
} SetRender;

typedef struct SetKb {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
} SetKb;

typedef struct SetChrome {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*expanded)(void);
    int (*navheight)(void);
    int (*navigation)(int);
} SetChrome;

void ctl_field(void *dc, const SetRender *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused);
int SVFDrawTint(void *dc, const void *data, unsigned length, int x, int y,
                int w, int h, U32 tint);

NT_DEFINE(render_SpGetThreadContext)
NT_IMPORT(const SetKb *, UIXGetSchemaFactory, (void))
NT_DEFINE(UIXGetSchemaFactory)
NT_IMPORT(const SetChrome *, UIXChromeFactory, (void))
NT_DEFINE(UIXChromeFactory)

/* ---- WinCE imports ---- */
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

#define WM_PAINT_ 0x000Fu
#define WM_KEY_ 0x0100u
#define WM_CHAR_ 0x0102u
#define WM_DOWN_ 0x0201u
#define WM_UP_ 0x0202u
#define WM_MOVE_ 0x0200u

/* =====================================================================
 * palette: W10M light theme, measured on refs/settings-screens
 * ===================================================================== */
#define C_PAGE  RGB_(0xF7, 0xF3, 0xF7) /* header + home background    */
#define C_WHITE RGB_(0xFF, 0xFF, 0xFF) /* deep content + fields       */
#define C_ACC   RGB_(0x39, 0x65, 0xFF) /* accent (reference device)   */
#define C_INK   RGB_(0x00, 0x00, 0x00)
#define C_MUT   RGB_(0x63, 0x61, 0x63) /* secondary text, headings    */
#define C_BTN   RGB_(0xCE, 0xCF, 0xCE) /* action buttons, toggle off  */
#define C_EDGE  RGB_(0x9C, 0x9A, 0x9C) /* combobox borders            */
#define C_PLACE RGB_(0x76, 0x76, 0x76) /* placeholder text            */
#define C_BAND  RGB_(0xAD, 0xB6, 0xF7) /* picker/combo selected band  */
#define C_TRACK RGB_(0x46, 0x45, 0x46) /* slider unfilled track       */
#define C_SCROLL RGB_(0xC6, 0xC3, 0xC6)
#define C_SCRIM RGB_(0x7F, 0x7F, 0x7F)

/* geometry */
#define HD_H 48
#define SEARCH_Y 62
#define HOME_TOP 104
#define HOME_PITCH 64
#define L2_HEAD_Y 100
#define L2_TOP 128
#define L2_PITCH 48
#define DEEP_TOP 72
#define LIST_TOP 96
#define LIST_BOT 488

/* ---- tiny utils ---- */
static int xstrlen(const char *s) { int n = 0; while (s && s[n]) ++n; return n; }
static int xeq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void xcopy(char *d, const char *s, int n) { for (int i = 0; i < n; ++i) d[i] = s[i]; }

/* ---- drawing (GDI FillRect backend, brush cache — files_app.c model) ---- */
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
    NT_CALL(FillRect, (dc, &v, cached_brush));
}

static void ifill(void *dc, int l, int t, int r, int b, unsigned c)
{
    fill(dc, l, t, r, b, (U32)c);
}

/* ---- font (bitmap glyphs) ---- */
static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[0];
    int bd = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        int d = f->height - h;
        if (d < 0) d = -d;
        if (d < bd) { best = f; bd = d; }
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

static void draw_text(void *dc, int x, int y, const char *s, U32 c, int h,
                      int align, int bold)
{
    (void)bold;
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

/* ---- render vtable (light theme, consumed by fingerkb + uixctl) ---- */
static void r_clip(int l, int t, int r, int b) { clip_set(l, t, r, b); }
static void r_fill(void *dc, int l, int t, int r, int b, U32 c) { fill(dc, l, t, r, b, c); }
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
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
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
static void r_surface(int b) { (void)b; }

static const U32 settings_theme[] = {
    RGB_(0xF2, 0xF2, 0xF2), /* 0 SURFACE   keyboard backdrop (light) */
    RGB_(0xFF, 0xFF, 0xFF), /* 1 KEY        normal key               */
    RGB_(0xE6, 0xE6, 0xE6), /* 2 KEY_ALT    shift/&123               */
    RGB_(0x00, 0x00, 0x00), /* 3 INK        key text                 */
    RGB_(0x39, 0x65, 0xFF), /* 4 KEY_GO     enter key fill (accent)  */
    RGB_(0xFF, 0xFF, 0xFF), /* 5 GO_INK     glyph on the go key      */
    RGB_(0x39, 0x65, 0xFF), /* 6 ACCENT     checked/caret/links      */
    RGB_(0xCE, 0xCF, 0xCE), /* 7 CTRL_BG    button fill              */
    RGB_(0x00, 0x00, 0x00), /* 8 CTRL_INK   button label             */
    RGB_(0x66, 0x66, 0x66), /* 9 CTRL_EDGE  unchecked border         */
    RGB_(0x76, 0x76, 0x76), /* 10 FIELD_EDGE input border/hint       */
    RGB_(0xCE, 0xCF, 0xCE), /* 11 NAV_BG    nav bar strip (light)    */
    RGB_(0x31, 0x30, 0x31), /* 12 NAV_INK   nav bar glyphs           */
};

static const SetRender settings_renderer = {
    0x55495831u, r_clip, r_fill, r_text, r_line,
    r_ellipse, r_polygon, r_surface, settings_theme,
};

__attribute__((dllexport)) const SetRender *render_SpGetThreadContext(void)
{
    return &settings_renderer;
}
__attribute__((dllexport)) int svf_SVFDrawTint(void *dc, const void *data,
                                               unsigned length, int x, int y,
                                               int w, int h, U32 tint)
{
    return SVFDrawTint(dc, data, length, x, y, w, h, tint);
}

/* =====================================================================
 * uXAML plumbing (files_app.c contract)
 * ===================================================================== */
typedef struct { short x, y, w, h; } NRect;
static UxFile ux;
static NRect nrect[UX_MAX_NODES];
static unsigned char ux_blob[48 * 1024];

#define DYN_MAX 32
static char dyn_heap[DYN_MAX][UX_STR_MAX];

static int find_node(const char *name)
{
    for (unsigned i = 0; i < ux.node_count; ++i) {
        int a = ux_attr(&ux.nodes[i], UXA_NAME);
        if (a >= 0) {
            uint16_t idx = (uint16_t)ux.nodes[i].num[a];
            if (idx < ux.string_count && xeq(ux.strings[idx], name))
                return (int)i;
        }
    }
    return -1;
}

static void dyn_set(const char *name, const char *text)
{
    int i = find_node(name);
    if (i < 0)
        return;
    UxNode *n = &ux.nodes[i];
    int a = ux_attr(n, UXA_TEXT);
    if (a < 0 || n->type[a] != 0)
        return;
    static int slot;
    char *dst = dyn_heap[slot];
    int k = 0;
    for (; text[k] && k < UX_STR_MAX - 1; ++k)
        dst[k] = text[k];
    dst[k] = 0;
    n->num[a] = 0x8000u + (uint16_t)slot;
    slot = (slot + 1) % DYN_MAX;
}

static const char *node_text(const UxNode *n)
{
    int a = ux_attr(n, UXA_TEXT);
    if (a < 0 || n->type[a] != 0)
        return 0;
    uint16_t idx = (uint16_t)n->num[a];
    if (idx & 0x8000u)
        return dyn_heap[idx & 0x7FFFu];
    return idx < ux.string_count ? ux.strings[idx] : 0;
}

static int node_visible_idx(unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    int a = ux_attr(n, UXA_VISIBLE);
    if (a >= 0 && n->type[a] == 3 && n->num[a] == 0)
        return 0;
    unsigned p = n->parent;
    while (p != UX_NONE) {
        if (!node_visible_idx(p))
            return 0;
        p = ux.nodes[p].parent;
    }
    return 1;
}

static void layout_doc(const UxDoc *d, int bx, int by, int bw, int bh)
{
    if (!d || d->root == UX_NONE)
        return;
    unsigned lo = d->root, hi = d->root + d->node_count;
    if (hi > ux.node_count)
        hi = ux.node_count;
    for (unsigned i = lo; i < hi; ++i) {
        const UxNode *n = &ux.nodes[i];
        int x = ux_int(&ux, n, UXA_X, 0);
        int y = ux_int(&ux, n, UXA_Y, 0);
        int w = ux_int(&ux, n, UXA_W, 0);
        int h = ux_int(&ux, n, UXA_H, 0);
        if (n->parent == UX_NONE) {
            x = bx; y = by; w = bw; h = bh;
        } else {
            x += nrect[n->parent].x;
            y += nrect[n->parent].y;
        }
        nrect[i].x = (short)x;
        nrect[i].y = (short)y;
        nrect[i].w = (short)w;
        nrect[i].h = (short)h;
    }
}

static void paint_node(void *dc, unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    if (!node_visible_idx(ni))
        return;
    NRect r = nrect[ni];
    switch (n->kind) {
    case UX_RECT: {
        U32 c = ux_clr(&ux, n, UXA_BG, C_PAGE);
        fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, c);
        break;
    }
    case UX_GLYPH: {
        const char *t = node_text(n);
        U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
        int sz = r.w < r.h ? r.w : r.h;
        if (t) {
            int sid = si_find(t);
            if (sid)
                si_draw(ifill, dc, r.x, r.y, sz, col, sid);
            else {
                int fid = fi_find(t);
                if (fid)
                    fi_draw(ifill, dc, r.x, r.y, sz, col, fid);
            }
        }
        break;
    }
    case UX_TEXT:
    case UX_BUTTON: {
        const char *t = node_text(n);
        if (n->kind == UX_BUTTON) {
            U32 bg = ux_clr(&ux, n, UXA_BG, 0xFFFFFFFFu);
            if (bg != 0xFFFFFFFFu)
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg);
        }
        if (t && *t) {
            int size = ux_int(&ux, n, UXA_SIZE, 12);
            int align = ux_int(&ux, n, UXA_ALIGN, 0);
            int bold = ux_int(&ux, n, UXA_BOLD, 0);
            U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
            draw_text(dc, r.x + (align == 1 ? r.w / 2 : 0), r.y, t, col,
                      size, align, bold);
        }
        break;
    }
    default:
        break;
    }
    for (uint8_t i = 0; i < n->child_count; ++i)
        paint_node(dc, n->child[i]);
}

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
        if (n->kind != UX_BUTTON || !node_visible_idx(i))
            continue;
        NRect r = nrect[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            best = n;
    }
    return best;
}

/* =====================================================================
 * app data — categories, subcategories (page registry + references)
 * ===================================================================== */
enum {
    PG_NONE = 0, PG_DISPLAY, PG_ABOUT, PG_USB, PG_START, PG_COLORS,
    PG_SOUNDS, PG_ACTIVE
};

typedef struct { const char *name; int icon; int page; } Sub;
typedef struct {
    const char *name;
    const char *desc;
    int icon;
    const Sub *subs;
    int sub_count;
} Cat;

static const Sub SYS_SUBS[] = {
    {"Display", SI_MONITOR, PG_DISPLAY},
    {"Notifications & actions", SI_TILES, PG_NONE},
    {"Battery saver", SI_BATTERY, PG_NONE},
    {"Storage", SI_STORAGE, PG_NONE},
    {"About", SI_INFO, PG_ABOUT},
};
static const Sub DEV_SUBS[] = {
    {"Bluetooth", SI_KEYBOARD, PG_NONE},
    {"USB", SI_USB, PG_USB},
    {"Camera & photos", SI_SCREENLOCK, PG_NONE},
};
static const Sub NET_SUBS[] = {
    {"Wi-Fi", SI_GLOBE, PG_NONE},
    {"Airplane mode", SI_UPDATE, PG_NONE},
    {"Cellular & SIM", SI_PHONE, PG_NONE},
    {"VPN", SI_PADLOCK, PG_NONE},
    {"Mobile hotspot", SI_GLOBE, PG_NONE},
};
static const Sub PER_SUBS[] = {
    {"Start", SI_TILES, PG_START},
    {"Colors", SI_PALETTE, PG_COLORS},
    {"Sounds", SI_NOTE, PG_SOUNDS},
    {"Lock screen", SI_SCREENLOCK, PG_NONE},
    {"Navigation bar", SI_NAVBAR, PG_NONE},
};
static const Sub ACC_SUBS[] = {
    {"Your info", SI_PERSON, PG_NONE},
    {"Email & accounts", SI_NOTE, PG_NONE},
    {"Sync", SI_UPDATE, PG_NONE},
};
static const Sub TIM_SUBS[] = {
    {"Date & time", SI_CLOCKCHAT, PG_NONE},
    {"Speech", SI_CLOCKCHAT, PG_NONE},
    {"Region", SI_GLOBE, PG_NONE},
};
static const Sub EOA_SUBS[] = {
    {"Narrator", SI_ACCESS, PG_NONE},
    {"Magnifier", SI_INFO, PG_NONE},
    {"High contrast", SI_MONITOR, PG_NONE},
    {"Closed captioning", SI_SCREENLOCK, PG_NONE},
};
static const Sub PRI_SUBS[] = {
    {"Location", SI_GLOBE, PG_NONE},
    {"Feedback", SI_INFO, PG_NONE},
    {"Webcam", SI_MONITOR, PG_NONE},
    {"Microphone", SI_PERSON, PG_NONE},
};
static const Sub UPD_SUBS[] = {
    {"Phone update", SI_UPDATE, PG_NONE},
    {"Active hours", SI_CLOCKCHAT, PG_ACTIVE},
    {"Find My Phone", SI_PHONE, PG_NONE},
    {"Backup", SI_STORAGE, PG_NONE},
};
static const Sub EXT_SUBS[] = {
    {"Bluetooth devices", SI_KEYBOARD, PG_NONE},
};

static const Cat CATS[] = {
    {"System", "Display, notifications, battery", SI_PHONE,
     SYS_SUBS, 5},
    {"Devices", "Bluetooth, camera", SI_KEYBOARD, DEV_SUBS, 3},
    {"Network & wireless", "Wi-Fi, airplane mode, cellular", SI_GLOBE,
     NET_SUBS, 5},
    {"Personalization", "Start, lock screen, sounds", SI_BRUSH,
     PER_SUBS, 5},
    {"Accounts", "Your accounts, email, sync", SI_PERSON, ACC_SUBS, 3},
    {"Time & language", "Speech, region, keyboard", SI_CLOCKCHAT,
     TIM_SUBS, 3},
    {"Ease of Access", "Text size, narrator, high contrast", SI_ACCESS,
     EOA_SUBS, 4},
    {"Privacy", "Location, feedback", SI_PADLOCK, PRI_SUBS, 4},
    {"Update & security", "Backup, Find My Phone", SI_UPDATE,
     UPD_SUBS, 4},
    {"Extras", "Apps from your phone manufacturer", SI_PUZZLE,
     EXT_SUBS, 1},
};
#define CAT_COUNT (int)(sizeof(CATS) / sizeof(CATS[0]))

/* autosuggest dictionary (settings_search.png) */
static const char *SUGGEST[] = {
    "test", "tell", "text", "theme", "then", "the",
    "yes", "yeah", "yea", "your", "time", "today",
};

/* =====================================================================
 * state
 * ===================================================================== */
enum {
    M_HOME, M_CATEGORY, M_DISPLAY, M_COLORS, M_SOUNDS, M_ABOUT,
    M_USB, M_START, M_ACTIVE, M_SEARCH, M_EDITNAME
};
static int mode = M_HOME;
static int cur_cat;
static int list_scroll;
static int dragging, drag_y;

/* live band y-offsets (updated at paint time, read by process_tap) */
static int disp_y_bright = 106, disp_y_auto, disp_y_rot, disp_y_size, disp_y_apply;
static int usb_y_slow, usb_y_ask;
static int ah_y_start = 176, ah_y_end = 250;

static const SetKb *g_kb;
static const SetChrome *g_chrome;

static char query[64];
static int query_len;
static char edit_buf[40];
static int edit_len;

static int br_brightness = 45;
static int br_auto = 1;
static int rotation_lock = 0;
static int text_scale = 100;
static int color_mode_light = 1;
static int accent_idx = 7;
static int vibrate = 1;
static int snd_key = 1, snd_lock = 1, snd_shutter = 1;
static int usb_slow = 0, usb_ask = 0;
static int start_combo_open = 0;
static int start_combo_sel = 2; /* 0 My pictures 1 Sample images 2 None */
static int ah_start_h = 5, ah_end_h = 11;
static int ah_start_pm, ah_end_pm = 1;
static int ah_picker = -1;       /* -1 none, 0 start, 1 end */
static char device_name[40] = "Windows phone";

/* =====================================================================
 * shared control painters (W10M geometry from the captures)
 * ===================================================================== */
static void paint_search_box(void *dc, int y, const char *text, int with_clear)
{
    int x = 16, w = 288, h = 26;
    fill(dc, x, y, x + w, y + h, C_WHITE);
    fill(dc, x, y, x + w, y + 2, C_ACC);
    fill(dc, x, y + h - 2, x + w, y + h, C_ACC);
    fill(dc, x, y, x + 2, y + h, C_ACC);
    fill(dc, x + w - 2, y, x + w, y + h, C_ACC);
    const char *show = (text && *text) ? text : "Find a setting";
    U32 c = (text && *text) ? C_INK : C_PLACE;
    draw_text(dc, x + 10, y + 6, show, c, 14, 0, 0);
    if (text && *text && with_clear)
        fi_draw(ifill, dc, x + w - 28, y + 5, 16, C_PLACE, FI_X);
    else
        fi_draw(ifill, dc, x + w - 26, y + 6, 14, C_PLACE, FI_SEARCH);
}

static void paint_scrollbar(void *dc, int content_h)
{
    int view = LIST_BOT - LIST_TOP;
    if (content_h <= view)
        return;
    int track = LIST_BOT - LIST_TOP;
    int thumb = view * track / content_h;
    if (thumb < 24)
        thumb = 24;
    int t = LIST_TOP + (track - thumb) * list_scroll / (content_h - view);
    fill(dc, 312, t, 320, t + thumb, C_SCROLL);
}

static void paint_toggle(void *dc, int x, int y, int on, U32 bg)
{
    int w = 34, h = 20;
    fill(dc, x, y, x + w, y + h, on ? C_ACC : C_BTN);
    /* rounded ends: knock page corners out */
    fill(dc, x, y, x + 1, y + 1, bg);
    fill(dc, x + w - 1, y, x + w, y + 1, bg);
    fill(dc, x, y + h - 1, x + 1, y + h, bg);
    fill(dc, x + w - 1, y + h - 1, x + w, y + h, bg);
    int kx = on ? x + w - 17 : x + 3;
    fill(dc, kx, y + 3, kx + 14, y + h - 3, C_WHITE);
    draw_text(dc, x + w + 10, y + 3, on ? "On" : "Off", C_INK, 13, 0, 0);
}

static void paint_slider(void *dc, int x, int y, int w, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fill(dc, x, y, x + w, y + 2, C_TRACK);
    int tw = w * pct / 100;
    fill(dc, x, y, x + tw, y + 2, C_ACC);
    int tx = x + tw - 4;
    if (tx < x) tx = x;
    if (tx > x + w - 8) tx = x + w - 8;
    fill(dc, tx, y - 11, tx + 8, y + 13, C_ACC);
    fill(dc, tx, y - 11, tx + 1, y - 10, C_WHITE);
    fill(dc, tx + 7, y - 11, tx + 8, y - 10, C_WHITE);
    fill(dc, tx, y + 12, tx + 1, y + 13, C_WHITE);
    fill(dc, tx + 7, y + 12, tx + 8, y + 13, C_WHITE);
}

static void paint_check(void *dc, int x, int y, int checked, const char *label)
{
    if (checked) {
        fill(dc, x, y, x + 20, y + 20, C_ACC);
        fi_draw(ifill, dc, x + 2, y + 2, 16, C_WHITE, FI_CHECK);
    } else {
        fill(dc, x, y, x + 20, y + 20, C_WHITE);
        fill(dc, x, y, x + 20, y + 1, C_EDGE);
        fill(dc, x, y + 19, x + 20, y + 20, C_EDGE);
        fill(dc, x, y, x + 1, y + 20, C_EDGE);
        fill(dc, x + 19, y, x + 20, y + 20, C_EDGE);
    }
    draw_text(dc, x + 32, y + 3, label, C_INK, 15, 0, 0);
}

static void paint_radio(void *dc, int x, int y, int selected, const char *label)
{
    fi_circle(ifill, dc, x + 10, y + 10, 9, 1, selected ? C_ACC : C_MUT);
    if (selected)
        fi_dot(ifill, dc, x + 10, y + 10, 5, C_ACC);
    draw_text(dc, x + 32, y + 3, label, C_INK, 15, 0, 0);
}

static void paint_action_button(void *dc, int x, int y, int w, const char *label)
{
    fill(dc, x, y, x + w, y + 30, C_BTN);
    if (label && *label)
        draw_text(dc, x + w / 2, y + 8, label, C_INK, 14, 1, 0);
}

static void paint_combo(void *dc, int x, int y, int w, const char *value,
                        int with_play)
{
    int h = 34;
    fill(dc, x, y, x + w, y + h, C_WHITE);
    fill(dc, x, y, x + w, y + 1, C_EDGE);
    fill(dc, x, y + h - 1, x + w, y + h, C_EDGE);
    fill(dc, x, y, x + 1, y + h, C_EDGE);
    fill(dc, x + w - 1, y, x + w, y + h, C_EDGE);
    int tx = x + 12;
    if (with_play) {
        si_draw(ifill, dc, x + 14, y + 10, 14, C_INK, SI_PLAY);
        tx = x + 38;
    }
    draw_text(dc, tx, y + 9, value, C_INK, 15, 0, 0);
    fi_draw(ifill, dc, x + w - 26, y + 10, 14, C_INK, FI_CHEVDOWN);
}

static void paint_time_box(void *dc, int y, int hour, int pm)
{
    int x = 12, w = 296, h = 34;
    fill(dc, x, y, x + w, y + h, C_WHITE);
    fill(dc, x, y, x + w, y + 1, C_EDGE);
    fill(dc, x, y + h - 1, x + w, y + h, C_EDGE);
    fill(dc, x, y, x + 1, y + h, C_EDGE);
    fill(dc, x + w - 1, y, x + w, y + h, C_EDGE);
    fill(dc, x + 104, y + 1, x + 105, y + h - 1, C_EDGE);
    fill(dc, x + 192, y + 1, x + 193, y + h - 1, C_EDGE);
    char v[4];
    v[0] = (char)('0' + hour / 10);
    v[1] = (char)('0' + hour % 10);
    v[2] = 0;
    draw_text(dc, x + 30, y + 9, v, C_INK, 15, 0, 0);
    draw_text(dc, x + 132, y + 9, "00", C_INK, 15, 0, 0);
    draw_text(dc, x + 228, y + 9, pm ? "PM" : "AM", C_INK, 15, 0, 0);
}

/* width of the half-open range [a,b) — avoids building a temp buffer */
static int text_w_line(const char *a, const char *b, int h)
{
    const PhoneFont *f = pick_font(h);
    int w = 0;
    for (; a < b; ++a) {
        int ch = (unsigned char)*a;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}

/* word-wrap (max 3 lines, real font metrics) */
static int wrap_text(const char *s, int maxw, int size, char out[3][64])
{
    int lines = 0;
    const char *p = s;
    while (*p && lines < 3) {
        const char *start = p;
        const char *last_space = 0;
        while (*p && text_w_line(start, p + 1, size) <= maxw && p - start < 62) {
            if (*p == ' ')
                last_space = p;
            ++p;
        }
        if (!*p) {
            int n = (int)(p - start);
            xcopy(out[lines], start, n);
            out[lines][n] = 0;
            return lines + 1;
        }
        if (p - start >= 62)
            last_space = p; /* hard cut */
        if (!last_space)
            last_space = p;
        int n = (int)(last_space - start);
        xcopy(out[lines], start, n);
        out[lines][n] = 0;
        p = last_space;
        while (*p == ' ')
            ++p;
        ++lines;
    }
    return lines;
}

/* =====================================================================
 * page painters
 * ===================================================================== */
static void paint_home_rows(void *dc)
{
    clip_set(0, LIST_TOP, 312, LIST_BOT);
    for (int i = 0; i < CAT_COUNT; ++i) {
        int y = HOME_TOP + i * HOME_PITCH - list_scroll;
        if (y + HOME_PITCH < LIST_TOP || y > LIST_BOT)
            continue;
        si_draw(ifill, dc, 12, y, 32, C_ACC, CATS[i].icon);
        draw_text(dc, 56, y + 2, CATS[i].name, C_INK, 15, 0, 1);
        draw_text(dc, 56, y + 24, CATS[i].desc, C_MUT, 13, 0, 0);
    }
    clip_set(0, 0, 320, 528);
    paint_scrollbar(dc, HOME_TOP + CAT_COUNT * HOME_PITCH - 40);
}

static void paint_l2_rows(void *dc)
{
    const Cat *c = &CATS[cur_cat];
    clip_set(0, LIST_TOP, 312, LIST_BOT);
    for (int i = 0; i < c->sub_count; ++i) {
        int y = L2_TOP + i * L2_PITCH - list_scroll;
        if (y + L2_PITCH < LIST_TOP || y > LIST_BOT)
            continue;
        si_draw(ifill, dc, 14, y + 10, 24, C_ACC, c->subs[i].icon);
        draw_text(dc, 56, y + 15, c->subs[i].name, C_INK, 15, 0, 0);
    }
    clip_set(0, 0, 320, 528);
    paint_scrollbar(dc, L2_TOP + c->sub_count * L2_PITCH - 40);
}

static void paint_display(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "Adjust brightness level", C_INK, 15, 0, 0);
    paint_slider(dc, 12, 106, 296, br_brightness);
    char lines[3][64];
    int n = wrap_text("Change brightness automatically when lighting changes",
                      290, 15, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, 148 + i * 22, lines[i], C_INK, 15, 0, 0);
    paint_toggle(dc, 12, 148 + n * 22 + 12, br_auto, C_WHITE);
    int y2 = 148 + n * 22 + 64;
    draw_text(dc, 12, y2, "Rotation lock", C_INK, 15, 0, 0);
    paint_toggle(dc, 12, y2 + 30, rotation_lock, C_WHITE);
    int y3 = y2 + 84;
    n = wrap_text("Size of text, apps, and items on this display", 290, 15, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, y3 + i * 22, lines[i], C_INK, 15, 0, 0);
    paint_slider(dc, 12, y3 + n * 22 + 12, 296, text_scale);
    int yb = y3 + n * 22 + 64;
    paint_action_button(dc, 12, yb, 88, "Apply");
    paint_action_button(dc, 124, yb, 88, "Cancel");
    /* remember the live control bands for the tap handler */
    disp_y_auto = 148 + n * 22 + 12;
    disp_y_rot = y2 + 30;
    disp_y_size = y3 + n * 22 + 12;
    disp_y_apply = yb;
    disp_y_bright = 106;
}

static const U32 SWATCHES[] = {
    RGB_(0xFF, 0xB9, 0x00), RGB_(0xF7, 0x63, 0x0C), RGB_(0xE8, 0x11, 0x23),
    RGB_(0xEA, 0x00, 0x5E), RGB_(0xC2, 0x39, 0xB3), RGB_(0x8E, 0x8C, 0xD8),
    RGB_(0x6B, 0x69, 0xD6), RGB_(0x00, 0x78, 0xD7), RGB_(0x00, 0x99, 0xBC),
    RGB_(0x00, 0xB2, 0x94), RGB_(0x10, 0x7C, 0x10), RGB_(0x7A, 0x75, 0x74),
    RGB_(0x4C, 0x4A, 0x48), RGB_(0x69, 0x79, 0x7E), RGB_(0x4A, 0x54, 0x59),
    RGB_(0x00, 0x63, 0x8C), RGB_(0x00, 0x2E, 0x57), RGB_(0x00, 0x1B, 0x3A),
    RGB_(0xFF, 0x8C, 0x00), RGB_(0xE7, 0x48, 0x56), RGB_(0xCA, 0x50, 0x10),
    RGB_(0xDA, 0x3B, 0x01), RGB_(0xEF, 0x69, 0x00), RGB_(0xD1, 0x34, 0x38),
};
#define SWATCH_COUNT ((int)(sizeof(SWATCHES) / sizeof(SWATCHES[0])))
#define SW_CELL 40
#define SW_GAP 8
#define SW_COLS 6
#define SW_TOP 200

static void paint_colors(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "Choose your mode", C_MUT, 15, 0, 0);
    paint_radio(dc, 16, 96, !color_mode_light, "Dark");
    paint_radio(dc, 16, 128, color_mode_light, "Light");
    draw_text(dc, 12, 172, "Choose your accent color", C_MUT, 15, 0, 0);
    for (int i = 0; i < SWATCH_COUNT; ++i) {
        int cx = 12 + (i % SW_COLS) * (SW_CELL + SW_GAP);
        int cy = SW_TOP + (i / SW_COLS) * (SW_CELL + SW_GAP);
        if (cy + SW_CELL > 472)
            break;
        fill(dc, cx, cy, cx + SW_CELL, cy + SW_CELL, SWATCHES[i]);
        if (i == accent_idx) {
            U32 ring = C_INK;
            for (int t = 0; t < 2; ++t) {
                fill(dc, cx - 2 + t, cy - 2, cx + SW_CELL + 2 - t, cy - 1 + t, ring);
                fill(dc, cx - 2 + t, cy + SW_CELL + 1 - t, cx + SW_CELL + 2 - t, cy + SW_CELL + 2 - t, ring);
                fill(dc, cx - 2, cy - 2 + t, cx - 1 + t, cy + SW_CELL + 2 - t, ring);
                fill(dc, cx + SW_CELL + 1 - t, cy - 2 + t, cx + SW_CELL + 2 - t, cy + SW_CELL + 2 - t, ring);
            }
        }
    }
}

static void paint_sounds(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "Ringtone", C_MUT, 15, 0, 0);
    paint_combo(dc, 12, 94, 296, "Pure", 1);
    draw_text(dc, 12, 140, "Learn how to get custom ringtones", C_ACC, 14, 0, 0);
    draw_text(dc, 12, 172, "Vibrate", C_MUT, 15, 0, 0);
    paint_toggle(dc, 12, 194, vibrate, C_WHITE);
    draw_text(dc, 12, 240, "Play a sound for", C_MUT, 15, 0, 0);
    paint_check(dc, 16, 264, snd_key, "Key press");
    paint_check(dc, 16, 296, snd_lock, "Lock and unlock");
    paint_check(dc, 16, 328, snd_shutter, "Camera shutter");
}

static void paint_about(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "Device name", C_INK, 15, 0, 0);
    draw_text(dc, 12, 96, device_name, C_INK, 15, 0, 0);
    paint_action_button(dc, 12, 122, 92, "Edit name");
    draw_text(dc, 12, 178, "Device information", C_INK, 15, 0, 0);
    static const char *ROWS[4][2] = {
        {"Model: ", "Microsoft Virtual"},
        {"Carrier: ", "Operator"},
        {"Software: ", "Windows 10 Mobile"},
        {"Installed RAM: ", "2 GB"},
    };
    for (int i = 0; i < 4; ++i) {
        int y = 204 + i * 28;
        draw_text(dc, 12, y, ROWS[i][0], C_MUT, 13, 0, 0);
        draw_text(dc, 12 + text_w(ROWS[i][0], 13), y, ROWS[i][1], C_INK, 13, 0, 0);
    }
    paint_action_button(dc, 12, 328, 92, "More info");
}

static void paint_usb(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "USB", C_INK, 20, 0, 1);
    char lines[3][64];
    int n = wrap_text(
        "Only chargers and USB connections to PCs are supported on your "
        "mobile device", 290, 13, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, 100 + i * 19, lines[i], C_MUT, 13, 0, 0);
    int y = 100 + n * 19 + 24;
    n = wrap_text("Notify me if my mobile device is charging slowly over USB",
                  290, 15, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, y + i * 22, lines[i], C_INK, 15, 0, 0);
    paint_toggle(dc, 12, y + n * 22 + 10, usb_slow, C_WHITE);
    usb_y_slow = y + n * 22 + 10;
    int y2 = y + n * 22 + 64;
    n = wrap_text("Ask me before allowing other PCs to connect to my mobile "
                  "device using USB", 290, 15, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, y2 + i * 22, lines[i], C_INK, 15, 0, 0);
    paint_toggle(dc, 12, y2 + n * 22 + 10, usb_ask, C_WHITE);
    usb_y_ask = y2 + n * 22 + 10;
}

static const char *START_OPTS[3] = {"My pictures", "Sample images", "None"};

static void paint_start(void *dc)
{
    draw_text(dc, 12, DEEP_TOP, "Preview", C_MUT, 15, 0, 0);
    int px = 96, py = 100, pw = 128, ph = 176;
    fill(dc, px, py, px + pw, py + ph, C_WHITE);
    for (int t = 0; t < 2; ++t) {
        fill(dc, px - 2 + t, py - 2, px + pw + 2 - t, py - 1 + t, C_INK);
        fill(dc, px - 2 + t, py + ph + 1 - t, px + pw + 2 - t, py + ph + 2 - t, C_INK);
        fill(dc, px - 2, py - 2 + t, px - 1 + t, py + ph + 2 - t, C_INK);
        fill(dc, px + pw + 1 - t, py - 2 + t, px + pw + 2 - t, py + ph + 2 - t, C_INK);
    }
    U32 tile = C_ACC;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 4; ++c)
            fill(dc, px + 6 + c * 30, py + 6 + r * 30, px + 6 + c * 30 + 26,
                 py + 6 + r * 30 + 26, tile);
    fill(dc, px + 6, py + 66, px + 92, py + 122, tile);
    fill(dc, px + 98, py + 66, px + 122, py + 94, tile);
    fill(dc, px + 6, py + 128, px + 122, py + 170, tile);
    draw_text(dc, px + 10, py + 144, "Aa", C_WHITE, 14, 0, 0);
    draw_text(dc, 12, 296, "Choose a background", C_MUT, 15, 0, 0);
    paint_combo(dc, 12, 320, 296, START_OPTS[start_combo_sel], 0);
    if (start_combo_open) {
        /* MenuFlyout shard: over everything (title bar included), like
         * the files dialogs — light-dismiss scrim + panel */
        fill(dc, 0, 0, 320, 488, C_SCRIM);
        int top = 318;
        fill(dc, 12, top, 308, top + 108, C_WHITE);
        fill(dc, 12, top, 308, top + 1, C_EDGE);
        for (int i = 0; i < 3; ++i) {
            int y = top + i * 36;
            if (i == start_combo_sel)
                fill(dc, 12, y + 1, 308, y + 35, C_BAND);
            draw_text(dc, 24, y + 9, START_OPTS[i], C_INK, 15, 0, 0);
            if (i < 2)
                fill(dc, 12, y + 35, 308, y + 36, C_EDGE);
        }
    }
}

static void paint_active(void *dc)
{
    if (ah_picker >= 0) {
        /* hour picker (active_hours_hourpicker.png): keeps the header,
         * 3 columns + light-blue selected band + confirm bar */
        draw_text(dc, 12, 36, "Active hours", C_INK, 20, 0, 0);
        int px = 24, pw = 272, top = 120, band = 240;
        fill(dc, px, top, px + pw, top + 240, C_PAGE);
        fill(dc, px, top, px + pw, top + 1, C_EDGE);
        fill(dc, px, top + 239, px + pw, top + 240, C_EDGE);
        fill(dc, px, top, px + 1, top + 240, C_EDGE);
        fill(dc, px + pw - 1, top, px + pw, top + 240, C_EDGE);
        fill(dc, px + 84, top + 1, px + 85, top + 239, C_EDGE);
        fill(dc, px + 172, top + 1, px + 173, top + 239, C_EDGE);
        fill(dc, px + 1, band, px + pw - 1, band + 36, C_BAND);
        int hour = ah_picker == 0 ? ah_start_h : ah_end_h;
        int pm = ah_picker == 0 ? ah_start_pm : ah_end_pm;
        char v[4];
        for (int i = -2; i <= 2; ++i) {
            int hv = ((hour - 1 + i) % 12 + 12) % 12 + 1;
            int y = band + 18 + i * 40 - 8;
            v[0] = (char)('0' + hv / 10);
            v[1] = (char)('0' + hv % 10);
            v[2] = 0;
            draw_text(dc, px + 24, y, v, i == 0 ? C_INK : C_MUT, 17, 0, 0);
        }
        draw_text(dc, px + 110, band + 10, "00", C_INK, 17, 0, 0);
        draw_text(dc, px + 110, band - 30, "01", C_MUT, 15, 0, 0);
        draw_text(dc, px + 110, band + 50, "59", C_MUT, 15, 0, 0);
        draw_text(dc, px + 202, band + 10, pm ? "PM" : "AM", C_INK, 17, 0, 0);
        draw_text(dc, px + 202, band - 30, pm ? "AM" : "PM", C_MUT, 15, 0, 0);
        fi_draw(ifill, dc, 152, top + 10, 16, C_MUT, FI_CHEVUP);
        fi_draw(ifill, dc, 152, top + 214, 16, C_MUT, FI_CHEVDOWN);
        /* confirm bar: two gray halves with check / X */
        fill(dc, 24, 424, 160, 454, C_BTN);
        fill(dc, 160, 424, 296, 454, C_BTN);
        fi_draw(ifill, dc, 80, 433, 20, C_INK, FI_CHECK);
        fi_draw(ifill, dc, 218, 433, 20, C_INK, FI_X);
        return;
    }
    draw_text(dc, 12, DEEP_TOP, "Active hours", C_INK, 20, 0, 0);
    char lines[3][64];
    int n = wrap_text(
        "Active hours lets us know when you usually use this device. When a "
        "restart is necessary to finish installing an update, we won't "
        "automatically restart your device during active hours.", 290, 13, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, 100 + i * 19, lines[i], C_MUT, 13, 0, 0);
    int y = 100 + n * 19 + 16;
    n = wrap_text("We'll check to see if you're using this device before "
                  "attempting to restart.", 290, 13, lines);
    for (int i = 0; i < n; ++i)
        draw_text(dc, 12, y + i * 19, lines[i], C_MUT, 13, 0, 0);
    y = y + n * 19 + 20;
    draw_text(dc, 12, y, "Start time", C_MUT, 13, 0, 0);
    paint_time_box(dc, y + 20, ah_start_h, ah_start_pm);
    ah_y_start = y + 20;
    int y2 = y + 74;
    draw_text(dc, 12, y2, "End time", C_MUT, 13, 0, 0);
    paint_time_box(dc, y2 + 20, ah_end_h, ah_end_pm);
    ah_y_end = y2 + 20;
    int yb = y2 + 74;
    paint_action_button(dc, 12, yb, 136, "Save");
    paint_action_button(dc, 164, yb, 132, "Cancel");
}

/* search results (prefix filter over every subcategory row) */
#define SR_MAX 16
static int search_row_count;
static char search_rows[SR_MAX][40];
static int search_row_icon[SR_MAX];
static int search_row_cat[SR_MAX];

static void search_rebuild(void)
{
    search_row_count = 0;
    if (!query_len)
        return;
    for (int c = 0; c < CAT_COUNT && search_row_count < SR_MAX; ++c)
        for (int s = 0; s < CATS[c].sub_count && search_row_count < SR_MAX; ++s) {
            const char *n = CATS[c].subs[s].name;
            /* substring match (case-insensitive), like the W10M search */
            int hit = 0;
            for (int j = 0; n[j] && !hit; ++j) {
                int k = 0;
                while (n[j + k] && query[k] &&
                       (n[j + k] | 32) == (query[k] | 32))
                    ++k;
                if (k == query_len)
                    hit = 1;
            }
            if (!hit)
                continue;
            int k;
            for (k = 0; k < 39 && n[k]; ++k)
                search_rows[search_row_count][k] = n[k];
            search_rows[search_row_count][k] = 0;
            search_row_icon[search_row_count] = CATS[c].subs[s].icon;
            search_row_cat[search_row_count] = c;
            ++search_row_count;
        }
}

static void paint_search(void *dc)
{
    paint_search_box(dc, SEARCH_Y, query, 1);
    int x = 12;
    for (unsigned i = 0; i < sizeof(SUGGEST) / sizeof(SUGGEST[0]); ++i) {
        const char *s = SUGGEST[i];
        int hit = 1;
        for (int j = 0; j < query_len; ++j)
            if (!s[j] || (s[j] | 32) != (query[j] | 32)) {
                hit = 0;
                break;
            }
        if (!hit)
            continue;
        draw_text(dc, x, 103, s, C_INK, 13, 0, 0);
        x += text_w(s, 13) + 16;
        if (x > 280)
            break;
    }
    clip_set(0, 124, 320, 231);
    for (int i = 0; i < search_row_count && i < 3; ++i) {
        int y = 132 + i * 44;
        si_draw(ifill, dc, 14, y, 20, C_ACC, search_row_icon[i]);
        draw_text(dc, 44, y + 2, search_rows[i], C_INK, 15, 0, 0);
    }
    clip_set(0, 0, 320, 528);
}

/* =====================================================================
 * frame paint
 * ===================================================================== */
static void paint_all(void *dc)
{
    const UxDoc *d;
    if (mode == M_HOME) {
        if ((d = ux_find_doc(&ux, "HomePage"))) {
            dyn_set("HeaderTitle", "Settings");
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_search_box(dc, SEARCH_Y, 0, 0);
            paint_home_rows(dc);
        }
    } else if (mode == M_CATEGORY) {
        if ((d = ux_find_doc(&ux, "CategoryPage"))) {
            dyn_set("HeaderTitle", "Settings");
            dyn_set("CategoryTitle", CATS[cur_cat].name);
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_search_box(dc, SEARCH_Y, 0, 0);
            paint_l2_rows(dc);
        }
    } else if (mode == M_SEARCH) {
        if ((d = ux_find_doc(&ux, "SearchPage"))) {
            dyn_set("HeaderTitle", "Settings");
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_search(dc);
            if (g_kb) {
                g_kb->dock(40);
                g_kb->paint(dc);
            }
        }
    } else if (mode == M_EDITNAME) {
        if ((d = ux_find_doc(&ux, "EditNamePage"))) {
            dyn_set("HeaderTitle", "Settings");
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            int fi = find_node("NameField");
            if (fi >= 0) {
                NRect r = nrect[fi];
                ctl_field(dc, (const SetRender *)&settings_renderer,
                          r.x, r.y, r.w, r.h, edit_buf, "Name", 1);
            }
            if (g_kb) {
                g_kb->dock(40);
                g_kb->paint(dc);
            }
        }
    } else {
        if ((d = ux_find_doc(&ux, "DeepPage"))) {
            const char *title = "Settings";
            switch (mode) {
            case M_DISPLAY: title = "Display"; break;
            case M_COLORS: title = "Colors"; break;
            case M_SOUNDS: title = "Sounds"; break;
            case M_ABOUT: title = "About"; break;
            case M_USB: title = "USB"; break;
            case M_START: title = "Start"; break;
            case M_ACTIVE: title = "Active hours"; break;
            }
            dyn_set("HeaderTitle", title);
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            switch (mode) {
            case M_DISPLAY: paint_display(dc); break;
            case M_COLORS: paint_colors(dc); break;
            case M_SOUNDS: paint_sounds(dc); break;
            case M_ABOUT: paint_about(dc); break;
            case M_USB: paint_usb(dc); break;
            case M_START: paint_start(dc); break;
            case M_ACTIVE: paint_active(dc); break;
            }
        }
    }
    if (g_chrome)
        g_chrome->paint(dc);
}

/* =====================================================================
 * input
 * ===================================================================== */
static int cat_row_at(int y)
{
    int ry = y + list_scroll;
    if (ry < HOME_TOP)
        return -1;
    int idx = (ry - HOME_TOP) / HOME_PITCH;
    if (idx >= 0 && idx < CAT_COUNT && ry < HOME_TOP + (idx + 1) * HOME_PITCH)
        return idx;
    return -1;
}

static int l2_row_at(int y)
{
    const Cat *c = &CATS[cur_cat];
    int ry = y + list_scroll;
    if (ry < L2_TOP)
        return -1;
    int idx = (ry - L2_TOP) / L2_PITCH;
    if (idx >= 0 && idx < c->sub_count && ry < L2_TOP + (idx + 1) * L2_PITCH)
        return idx;
    return -1;
}

static void go_page(int page)
{
    switch (page) {
    case PG_DISPLAY: mode = M_DISPLAY; break;
    case PG_ABOUT: mode = M_ABOUT; break;
    case PG_USB: mode = M_USB; break;
    case PG_START: mode = M_START; start_combo_open = 0; break;
    case PG_COLORS: mode = M_COLORS; break;
    case PG_SOUNDS: mode = M_SOUNDS; break;
    case PG_ACTIVE: mode = M_ACTIVE; ah_picker = -1; break;
    default: return; /* not wired in this build */
    }
}

static void edit_key(unsigned wp)
{
    if (wp == 8) {
        if (mode == M_SEARCH) {
            if (query_len) {
                --query_len;
                query[query_len] = 0;
            }
        } else if (edit_len) {
            --edit_len;
            edit_buf[edit_len] = 0;
        }
    } else if (wp == 13) {
        if (mode == M_SEARCH) {
            if (search_row_count) {
                cur_cat = search_row_cat[0];
                mode = M_CATEGORY;
                list_scroll = 0;
            }
        } else {
            xcopy(device_name, edit_buf, edit_len);
            device_name[edit_len] = 0;
            mode = M_ABOUT;
        }
    } else if (wp == 27) {
        mode = M_ABOUT;
    } else if (wp >= 32 && wp < 127) {
        if (mode == M_SEARCH) {
            if (query_len < 60) {
                query[query_len++] = (char)wp;
                query[query_len] = 0;
                search_rebuild();
            }
        } else if (edit_len < 38) {
            edit_buf[edit_len++] = (char)wp;
            edit_buf[edit_len] = 0;
        }
    }
}

static void process_tap(int x, int y)
{
    const UxDoc *d;
    const UxNode *b;
    /* docked SIP strip */
    if ((mode == M_SEARCH || mode == M_EDITNAME) && y >= 231 && g_kb) {
        U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
        int c = g_kb->input(WM_DOWN_, 1, lp);
        if (c > 0)
            edit_key((unsigned)c);
        return;
    }
    switch (mode) {
    case M_HOME: {
        if ((d = ux_find_doc(&ux, "HomePage")) && (b = hit_test(d, x, y))) {
            const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
            if (cmd && xeq(cmd, "search")) {
                mode = M_SEARCH;
                query_len = 0;
                query[0] = 0;
                search_rebuild();
                return;
            }
        }
        int row = cat_row_at(y);
        if (row >= 0) {
            cur_cat = row;
            mode = M_CATEGORY;
            list_scroll = 0;
        }
        return;
    }
    case M_CATEGORY: {
        if ((d = ux_find_doc(&ux, "CategoryPage")) && (b = hit_test(d, x, y))) {
            const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
            if (cmd && xeq(cmd, "search")) {
                mode = M_SEARCH;
                query_len = 0;
                query[0] = 0;
                search_rebuild();
                return;
            }
        }
        int row = l2_row_at(y);
        if (row >= 0)
            go_page(CATS[cur_cat].subs[row].page);
        return;
    }
    case M_DISPLAY: {
        if (y >= disp_y_bright - 12 && y <= disp_y_bright + 14) {
            int v = (x - 12) * 100 / 296;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            br_brightness = v;
            return;
        }
        if (y >= disp_y_auto && y <= disp_y_auto + 20 && x < 110)
            br_auto = !br_auto;
        else if (y >= disp_y_rot && y <= disp_y_rot + 20 && x < 110)
            rotation_lock = !rotation_lock;
        else if (y >= disp_y_size - 12 && y <= disp_y_size + 14) {
            int v = (x - 12) * 100 / 296;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            text_scale = v;
        }
        return;
    }
    case M_COLORS: {
        if (y >= 96 && y < 116 && x < 160)
            color_mode_light = 0;
        else if (y >= 128 && y < 148 && x < 160)
            color_mode_light = 1;
        else if (y >= SW_TOP) {
            int cx = (x - 12) / (SW_CELL + SW_GAP);
            int cy = (y - SW_TOP) / (SW_CELL + SW_GAP);
            if (cx >= 0 && cx < SW_COLS && cy >= 0) {
                int idx = cy * SW_COLS + cx;
                if (idx < SWATCH_COUNT) {
                    int px = 12 + cx * (SW_CELL + SW_GAP);
                    int py = SW_TOP + cy * (SW_CELL + SW_GAP);
                    if (x >= px && x < px + SW_CELL && y >= py && y < py + SW_CELL)
                        accent_idx = idx;
                }
            }
        }
        return;
    }
    case M_SOUNDS: {
        if (y >= 194 && y <= 214 && x < 110)
            vibrate = !vibrate;
        else if (y >= 264 && y < 284)
            snd_key = !snd_key;
        else if (y >= 296 && y < 316)
            snd_lock = !snd_lock;
        else if (y >= 328 && y < 348)
            snd_shutter = !snd_shutter;
        return;
    }
    case M_ABOUT: {
        if (y >= 122 && y < 152 && x >= 12 && x < 104) {
            xcopy(edit_buf, device_name, xstrlen(device_name));
            edit_len = xstrlen(device_name);
            edit_buf[edit_len] = 0;
            mode = M_EDITNAME;
        }
        return;
    }
    case M_USB: {
        if (y >= usb_y_slow && y <= usb_y_slow + 20 && x < 110)
            usb_slow = !usb_slow;
        else if (y >= usb_y_ask && y <= usb_y_ask + 20 && x < 110)
            usb_ask = !usb_ask;
        return;
    }
    case M_START: {
        if (start_combo_open) {
            int top = 318;
            if (y >= top && y < top + 108 && x >= 12 && x < 308) {
                int sel = (y - top) / 36;
                if (sel >= 0 && sel < 3)
                    start_combo_sel = sel;
            }
            start_combo_open = 0;
            return;
        }
        if (y >= 320 && y < 354 && x >= 12 && x < 308)
            start_combo_open = 1;
        return;
    }
    case M_ACTIVE: {
        if (ah_picker >= 0) {
            if (y >= 424 && y < 454) {
                ah_picker = -1; /* confirm or cancel both close */
            } else if (y >= 120 && y < 240) {
                int *h = ah_picker == 0 ? &ah_start_h : &ah_end_h;
                *h = *h % 12 + 1;
            } else if (y >= 276 && y < 360) {
                int *h = ah_picker == 0 ? &ah_start_h : &ah_end_h;
                *h = (*h + 10) % 12 + 1;
            }
            return;
        }
        if (y >= ah_y_start && y < ah_y_start + 34) {
            if (x < 116)
                ah_picker = 0;
            else if (x >= 204)
                ah_start_pm = !ah_start_pm;
        } else if (y >= ah_y_end && y < ah_y_end + 34) {
            if (x < 116)
                ah_picker = 1;
            else if (x >= 204)
                ah_end_pm = !ah_end_pm;
        }
        return;
    }
    case M_SEARCH: {
        if ((d = ux_find_doc(&ux, "SearchPage")) && (b = hit_test(d, x, y))) {
            const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
            if (cmd && xeq(cmd, "search-clear")) {
                query_len = 0;
                query[0] = 0;
                search_rebuild();
                return;
            }
        }
        if (y >= 96 && y < 124) { /* accept a suggestion word */
            int wx = 12;
            for (unsigned i = 0; i < sizeof(SUGGEST) / sizeof(SUGGEST[0]); ++i) {
                const char *s = SUGGEST[i];
                int hit = 1;
                for (int j = 0; j < query_len; ++j)
                    if (!s[j] || (s[j] | 32) != (query[j] | 32)) {
                        hit = 0;
                        break;
                    }
                if (!hit)
                    continue;
                int w = text_w(s, 13);
                if (x >= wx - 6 && x < wx + w + 8) {
                    int k;
                    for (k = 0; s[k]; ++k)
                        query[k] = s[k];
                    query[k] = 0;
                    query_len = k;
                    search_rebuild();
                    return;
                }
                wx += w + 16;
                if (wx > 280)
                    break;
            }
            return;
        }
        if (y >= 124 && y < 231) { /* result row -> its page */
            int row = (y - 132) / 44;
            if (row >= 0 && row < search_row_count && row < 3) {
                for (int s = 0; s < CATS[cur_cat].sub_count; ++s)
                    ;
                /* open the covered page when one exists, else L2 list */
                const char *n = search_rows[row];
                for (int c = 0; c < CAT_COUNT; ++c)
                    for (int s = 0; s < CATS[c].sub_count; ++s)
                        if (xeq(n, CATS[c].subs[s].name)) {
                            cur_cat = c;
                            go_page(CATS[c].subs[s].page);
                            if (mode == M_SEARCH) /* page not wired */
                                mode = M_CATEGORY;
                            list_scroll = 0;
                            return;
                        }
            }
        }
        return;
    }
    case M_EDITNAME: {
        if ((d = ux_find_doc(&ux, "EditNamePage")) && (b = hit_test(d, x, y))) {
            const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
            if (cmd && xeq(cmd, "name-clear")) {
                edit_len = 0;
                edit_buf[0] = 0;
                return;
            }
            if (cmd && xeq(cmd, "name-save")) {
                xcopy(device_name, edit_buf, edit_len);
                device_name[edit_len] = 0;
                mode = M_ABOUT;
                return;
            }
            if (cmd && xeq(cmd, "name-cancel")) {
                mode = M_ABOUT;
                return;
            }
        }
        return;
    }
    }
}

/* =====================================================================
 * entry
 * ===================================================================== */
static int load_markup(void)
{
    static const W16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 's', 'e',
                               't', 't', 'i', 'n', 'g', 's', '.', 'u',
                               'x', 'a', 'm', 'l', 0};
    void *h = NT_CALL(CreateFileW, (path, 0x80000000u, 0, 0, 3, 0, 0));
    if (!h || h == (void *)(uintptr_t)0xFFFFFFFFu)
        return -1;
    U32 size = NT_CALL(GetFileSize, (h, 0));
    if (size == 0xFFFFFFFFu || size > 48 * 1024) {
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

static void *g_wnd;

/* press state: rows activate on WM_UP_ unless the press became a drag
 * (proper tap-vs-pan disambiguation, the W10M list feel) */
static int press_x, press_y, pressed;

static void nav_bar_action(int key)
{
    if (key == 4) { /* Back: walk the surface stack */
        if (mode == M_HOME) {
            NT_CALL(PostQuitMessage, (0));
        } else if (ah_picker >= 0) {
            ah_picker = -1;
        } else if (start_combo_open) {
            start_combo_open = 0;
        } else if (mode == M_CATEGORY || mode == M_SEARCH) {
            mode = M_HOME;
            list_scroll = 0;
        } else {
            mode = M_CATEGORY;
        }
    } else if (key == 5) { /* Home */
        mode = M_HOME;
        list_scroll = 0;
        ah_picker = -1;
        start_combo_open = 0;
    } else if (key == 6) { /* Search */
        mode = M_SEARCH;
        query_len = 0;
        query[0] = 0;
        search_rebuild();
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
        if (wp == 0x1b) { /* hardware back */
            nav_bar_action(4);
        } else if (mode == M_SEARCH || mode == M_EDITNAME) {
            if (msg == WM_CHAR_ || wp < 128)
                edit_key(wp);
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_ || msg == WM_MOVE_ || msg == WM_UP_) {
        int cr = g_chrome ? g_chrome->input(msg, wp, lp) : 0;
        if (cr >= 4 && cr <= 6) {
            nav_bar_action(cr);
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
            return 0;
        }
        if (cr == 1) {
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
            return 0;
        }
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        drag_y = y;
        dragging = 0;
        press_x = x;
        press_y = y;
        pressed = 1;
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_MOVE_) {
        int y = (int)(short)(lp >> 16);
        if (pressed && (mode == M_HOME || mode == M_CATEGORY)) {
            int dy = drag_y - y;
            int max = (mode == M_HOME)
                ? HOME_TOP + CAT_COUNT * HOME_PITCH - 40 - (LIST_BOT - LIST_TOP)
                : L2_TOP + CATS[cur_cat].sub_count * L2_PITCH - 40 - (LIST_BOT - LIST_TOP);
            if (dragging || dy > 8 || dy < -8) {
                dragging = 1;
                list_scroll += dy;
                if (list_scroll < 0)
                    list_scroll = 0;
                if (list_scroll > max)
                    list_scroll = max;
                drag_y = y;
                NT_CALL(InvalidateRect, (hwnd, 0, 0));
                NT_CALL(UpdateWindow, (hwnd));
            }
        }
        return 0;
    }
    if (msg == WM_UP_) {
        if (pressed && !dragging)
            process_tap(press_x, press_y);
        pressed = 0;
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

static const W16 cls_name[] = {'S', 'e', 't', 't', 'i', 'n', 'g', 's', 0};

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u;
    /* host-side IAT snap for uixctl.c + svf.c compiled into this DLL */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
    g_kb = NT_CALL(UIXGetSchemaFactory, ());
    if (!g_kb || g_kb->profile != 0x55495831u)
        g_kb = 0;
    g_chrome = NT_CALL(UIXChromeFactory, ());
    if (!g_chrome || g_chrome->profile != 0x55495831u)
        g_chrome = 0;
    search_rebuild();
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
