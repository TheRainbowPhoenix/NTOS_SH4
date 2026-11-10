/* files_app.c — "File Explorer" (W10M) POC for NTOS, driven by uXAML markup.
 *
 * Reconstruction of Microsoft.Windows.FileExplorer_cw5n1h2txyewy (RS1,
 * 10.0.14393) from its embedded XBF pages + en-US resources
 * (files/analysis/FILEEXPLORER_UI_REFERENCE.md): light theme, NavBar with
 * breadcrumbs + Up, hamburger NavPane (256px overlay), FolderView list and
 * icons modes with Sort-by header, bottom FrameAppBar (Select / New folder /
 * Search / "..."), the commands ContentDialog, Rename/NewFolder bottom
 * sheets docked above the shared FINGERKB.DLL keyboard, Properties page
 * (96px thumbnail + 13px rows + drive Used/Free bar) and a real file model
 * on \fls0\ (scan/sort/create/rename/copy/move/delete).
 *
 * Component reuse (the W10M shared-DLL model, mirrors edge/app):
 *   - keyboard: FINGERKB.DLL module; host loader snaps UIXGetSchemaFactory
 *   - controls + SVF: payload/phone/uixctl.c + svf.c compiled in (light theme)
 *   - uXAML engine: the exact edge/app/uxaml.c loader
 */
#include "nt_app.h"
#include "uxaml.h"
#include "files_icons.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

typedef uint64_t U64;

/* ---- shared phone-framework ABI (mirror of payload/phone/phone.h) ---- */
typedef struct FilesRender {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme; /* UIX_TH_* table (see phone.h) */
} FilesRender;

typedef struct FilesKb {
    U32 profile; /* 0x55495831 "UIX1" */
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
} FilesKb;

/* shell chrome (SHELLCHROME.DLL) mirror of phone.h Chrome: the system
 * navigation bar (Back / Home / Search) every hosted app shows at the
 * bottom — the same module edge and settings import */
typedef struct FilesChrome {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*expanded)(void);
    int (*navheight)(void);
    int (*navigation)(int);
} FilesChrome;

/* shared control painters (payload/phone/uixctl.c, linked into this DLL) */
void ctl_button(void *dc, const FilesRender *r, int x, int y, int w, int h,
                const char *label, int pressed);
void ctl_checkbox(void *dc, const FilesRender *r, int x, int y, int checked,
                  const char *label);
void ctl_toggle(void *dc, const FilesRender *r, int x, int y, int on);
void ctl_field(void *dc, const FilesRender *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused);
int SVFDrawTint(void *dc, const void *data, unsigned length, int x, int y,
                int w, int h, U32 tint);

NT_DEFINE(render_SpGetThreadContext)
NT_IMPORT(const FilesKb *, UIXGetSchemaFactory, (void))
NT_DEFINE(UIXGetSchemaFactory)
/* nav-bar chrome: imported from SHELLCHROME.DLL (UIXChromeFactory is the
 * hosted-app surface: nav bar only, themed through files_renderer) */
NT_IMPORT(const FilesChrome *, UIXChromeFactory, (void))
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
NT_IMPORT(int, WriteFile, (void *, const void *, U32, U32 *, void *))
NT_DEFINE(WriteFile)
NT_IMPORT(int, CloseHandle, (void *))
NT_DEFINE(CloseHandle)
NT_IMPORT(U32, GetFileSize, (void *, U32 *))
NT_DEFINE(GetFileSize)
NT_IMPORT(int, SipShowIM, (U32))
NT_DEFINE(SipShowIM)

/* file APIs (host builtins / device COREDLL) */
typedef struct { /* WinCE FIND_DATA, 560 bytes (host fills exactly this) */
    U32 attrs, times[6], size_hi, size_lo, oid;
    W16 name[260];
} FindData;
NT_IMPORT(void *, FindFirstFileW, (const W16 *, FindData *))
NT_DEFINE(FindFirstFileW)
NT_IMPORT(int, FindNextFileW, (void *, FindData *))
NT_DEFINE(FindNextFileW)
NT_IMPORT(int, FindClose, (void *))
NT_DEFINE(FindClose)
NT_IMPORT(int, DeleteFileW, (const W16 *))
NT_DEFINE(DeleteFileW)
NT_IMPORT(int, CreateDirectoryW, (const W16 *, void *))
NT_DEFINE(CreateDirectoryW)
NT_IMPORT(int, RemoveDirectoryW, (const W16 *))
NT_DEFINE(RemoveDirectoryW)
NT_IMPORT(int, MoveFileW, (const W16 *, const W16 *))
NT_DEFINE(MoveFileW)
NT_IMPORT(int, CopyFileW, (const W16 *, const W16 *, int))
NT_DEFINE(CopyFileW)
NT_IMPORT(int, GetDiskFreeSpaceExW, (const W16 *, U64 *, U64 *, U64 *))
NT_DEFINE(GetDiskFreeSpaceExW)
NT_IMPORT(U32, CreateProcessW, (const W16 *, const W16 *, void *, void *, int, U32, void *, const W16 *, void *, void *))
NT_DEFINE(CreateProcessW)

/* ---- palette: W10M light theme (FILEEXPLORER_UI_REFERENCE.md) ---- */
#include "files_vecicons.h"

#define C_PAGE   RGB_(0xFF, 0xFF, 0xFF) /* PageBackgroundAltHigh      */
#define C_NAV    RGB_(0xE6, 0xE6, 0xE6) /* NavBar  = ChromeMedium     */
#define C_PANE   RGB_(0xF2, 0xF2, 0xF2) /* NavPane = ChromeLow        */
#define C_INK    RGB_(0x00, 0x00, 0x00) /* BaseHigh                   */
#define C_MUT    RGB_(0x76, 0x76, 0x76) /* BaseMedium                 */
#define C_DIM    RGB_(0x9A, 0x9A, 0x9A) /* placeholder                */
#define C_SEP    RGB_(0xE0, 0xE0, 0xE0) /* hairline separators        */
#define C_ACC    RGB_(0x00, 0x78, 0xD7) /* SystemAccentColor          */
#define C_SEL    RGB_(0xCC, 0xE8, 0xFF) /* HighlightListAccentLow     */
#define C_SCRIM  RGB_(0x7F, 0x7F, 0x7F)
#define C_FOL_A  RGB_(0xFF, 0xB9, 0x00) /* Folder_Large body amber    */
#define C_FOL_B  RGB_(0xE8, 0xA0, 0x00) /* Folder_Large shade         */
#define C_CHK_E  RGB_(0x66, 0x66, 0x66) /* checkbox border (measured) */

/* ---- tiny utils (no CRT) ---- */
static int xstrlen(const char *s) { int n = 0; while (s && s[n]) ++n; return n; }
static void xcopy(char *d, const char *s, int n) { for (int i = 0; i < n; ++i) d[i] = s[i]; }
static int xeq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void wide_(W16 *d, const char *s, int cap)
{
    int i = 0;
    while (s && s[i] && i < cap - 1) { d[i] = (unsigned char)s[i]; ++i; }
    d[i] = 0;
}
static void narrow_(char *d, const W16 *s, int cap)
{
    int i = 0;
    while (s && s[i] && i < cap - 1) { d[i] = (char)s[i]; ++i; }
    d[i] = 0;
}
static int xicmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
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
    NT_CALL(FillRect, (dc, &v, cached_brush));
}

static void ifill(void *dc, int l, int t, int r, int b, unsigned c)
{
    fill(dc, l, t, r, b, (U32)c);
}

/* ---- font ---- */
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
    /* same integer ellipse as edge_app.c: fill (thick==0) or 1px ring */
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
    /* scanline fill (same algorithm as edge_app.c) — the SVF icon layers
     * are polygons, so this is what makes the coloured type icons work */
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
static void r_surface(int b) { (void)b; }

static const U32 files_theme[] = {
    RGB_(0xF2, 0xF2, 0xF2), /* 0 SURFACE   keyboard backdrop (light) */
    RGB_(0xFF, 0xFF, 0xFF), /* 1 KEY        normal key               */
    RGB_(0xE6, 0xE6, 0xE6), /* 2 KEY_ALT    shift/&123               */
    RGB_(0x00, 0x00, 0x00), /* 3 INK        key text                 */
    RGB_(0x00, 0x78, 0xD7), /* 4 KEY_GO     enter key fill (accent)  */
    RGB_(0xFF, 0xFF, 0xFF), /* 5 GO_INK     glyph on the go key      */
    RGB_(0x00, 0x78, 0xD7), /* 6 ACCENT     checked/caret/links      */
    RGB_(0xE6, 0xE6, 0xE6), /* 7 CTRL_BG    button fill              */
    RGB_(0x00, 0x00, 0x00), /* 8 CTRL_INK   button label             */
    RGB_(0x66, 0x66, 0x66), /* 9 CTRL_EDGE  unchecked border         */
    RGB_(0x76, 0x76, 0x76), /* 10 FIELD_EDGE input border/hint       */
    RGB_(0xCE, 0xCF, 0xCE), /* 11 NAV_BG    nav bar strip (light)    */
    RGB_(0x31, 0x30, 0x31), /* 12 NAV_INK   nav bar glyphs           */
};

static const FilesRender files_renderer = {
    0x55495831u, r_clip, r_fill, r_text, r_line,
    r_ellipse, r_polygon, r_surface, files_theme,
};

/* cross-module exports: fingerkb.dll snaps these (host loader) */
__attribute__((dllexport)) const FilesRender *render_SpGetThreadContext(void)
{
    return &files_renderer;
}
__attribute__((dllexport)) int svf_SVFDrawTint(void *dc, const void *data,
                                               unsigned length, int x, int y,
                                               int w, int h, U32 tint)
{
    return SVFDrawTint(dc, data, length, x, y, w, h, tint);
}

/* ---- uXAML engine glue (same layout/paint model as edge_app.c) ---- */
#define NS_TOGGLED 1
#define NS_HIDDEN 2
#define NS_SHOWN 4
typedef struct { short x, y, w, h; } NRect;
static UxFile ux;
static NRect nrect[UX_MAX_NODES];
static unsigned char nstate[UX_MAX_NODES];

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

static int find_node(const char *name)
{
    for (uint16_t i = 0; i < ux.node_count; ++i) {
        const char *n = ux_str(&ux, &ux.nodes[i], UXA_NAME, 0);
        if (n && xeq(n, name))
            return i;
    }
    return -1;
}

/* Show/hide override that works for BOTH markup-visible and markup-hidden
 * nodes: "on" sets NS_SHOWN (beats a visible="false" default), "off" sets
 * NS_HIDDEN (beats the default) — each clears the other bit. */
static void show_named(const char *name, int on)
{
    int i = find_node(name);
    if (i < 0)
        return;
    if (on) {
        nstate[i] |= (unsigned char)NS_SHOWN;
        nstate[i] &= (unsigned char)~NS_HIDDEN;
    } else {
        nstate[i] |= (unsigned char)NS_HIDDEN;
        nstate[i] &= (unsigned char)~NS_SHOWN;
    }
}

static void layout_doc(const UxDoc *d, int bx, int by, int bw, int bh)
{
    if (!d || d->root == UX_NONE)
        return;
    /* every page is absolutely positioned, so the layout pass runs ONCE
     * per doc: repeated paints reuse the cached nrect[] (the single
     * biggest per-frame cost on the SH4 after text rasterisation) */
    static const UxDoc *done[UX_MAX_DOCS];
    static unsigned char done_flag[UX_MAX_DOCS];
    unsigned di = (unsigned)(d - ux.docs);
    int cached = di < UX_MAX_DOCS && done_flag[di] && done[di] == d;
    if (cached && nrect[d->root].w == (short)bw)
        return;
    if (di < UX_MAX_DOCS) {
        done[di] = d;
        done_flag[di] = 1;
    }
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
                if (!cw) cw = pr.w;
                cx = 0;
            } else if (n->kind == UX_STACKH) {
                cx = acc;
                acc += cw ? cw : 40;
                if (!ch) ch = pr.h;
                cy = 0;
            } else {
                if (!cw) cw = pr.w;
                if (!ch) ch = pr.h;
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

/* dynamic text bindings (the {Binding} substitute) */
typedef struct { char name[24]; char text[96]; } DynText;
static DynText dyn_texts[24];
static int dyn_count;

static void dyn_set(const char *name, const char *text)
{
    if (!name || !text)
        return;
    int slot = -1;
    for (int i = 0; i < dyn_count; ++i)
        if (xeq(dyn_texts[i].name, name)) {
            slot = i;
            break;
        }
    if (slot < 0 && dyn_count < 24) {
        slot = dyn_count++;
        int n = xstrlen(name);
        if (n > 23)
            n = 23;
        xcopy(dyn_texts[slot].name, name, n);
        dyn_texts[slot].name[n] = 0;
    }
    if (slot < 0)
        return;
    int n = xstrlen(text);
    if (n > 95)
        n = 95;
    xcopy(dyn_texts[slot].text, text, n);
    dyn_texts[slot].text[n] = 0;
}

static const char *dyn_get(const char *name)
{
    if (!name)
        return 0;
    for (int i = 0; i < dyn_count; ++i)
        if (xeq(dyn_texts[i].name, name))
            return dyn_texts[i].text;
    return 0;
}

static void node_text(const UxNode *n, const char **out)
{
    const char *t = ux_str(&ux, n, UXA_TEXT, "");
    const char *d = dyn_get(ux_str(&ux, n, UXA_NAME, 0));
    *out = d ? d : t;
}

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
            fi_rect(ifill, dc, r.x, r.y, r.w, r.h, 1, c);
        } else {
            U32 c = ux_clr(&ux, n, UXA_BG, C_PAGE);
            fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, c);
        }
        break;
    }
    case UX_PROGRESS: {
        int val = ux_int(&ux, n, UXA_VALUE, 0);
        int max = ux_int(&ux, n, UXA_MAX, 100);
        fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, ux_clr(&ux, n, UXA_BG, C_SEP));
        if (max > 0 && val > 0) {
            int w = r.w * (val > max ? max : val) / max;
            fill(dc, r.x, r.y, r.x + w, r.y + r.h, C_ACC);
        }
        break;
    }
    case UX_GLYPH: {
        const char *t;
        node_text(n, &t);
        int id = fi_find(t);
        U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
        int sz = r.w < r.h ? r.w : r.h;
        fi_draw(ifill, dc, r.x, r.y, sz, col, id);
        break;
    }
    case UX_TEXT:
    case UX_BUTTON: {
        const char *t;
        node_text(n, &t);
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
            int tx = r.x, ty = r.y;
            if (align == 1)
                tx += r.w / 2;
            else if (align == 2)
                tx += r.w;
            draw_text(dc, tx, ty, t, col, size, align, bold);
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
 * File model
 * ===================================================================== */
#define MAX_ITEMS 128
#define MAX_CRUMBS 8
#define NAME_MAX_ 40

typedef struct {
    char name[NAME_MAX_];
    U32 size_lo;
    U32 mtime;          /* unix seconds */
    unsigned char folder, selected;
    char icon;          /* FI_* id as char */
} Item;

static Item items[MAX_ITEMS];
static int item_count;
static char cur_path[120];      /* device path, e.g. \fls0\Documents */
static int select_mode, any_selected;
static int view_list = 1;       /* 1 = List (tile), 0 = Icons (thumbnail) */
static int sort_key;            /* 0 Name 1 Date 2 Size (folders first) */
static int sort_desc;           /* 1 = Z on top / newest / largest first */
static int sort_open;           /* sort-by flyout visible */
static int menu_open;           /* "..." overflow open (labels expand)  */
static int list_scroll;         /* px scrolled inside the item area */
static int drag_y, dragging;    /* touch scroll state */

/* nav pane */
typedef struct {
    const char *label;
    const char *path;   /* 0 = header row */
    int icon;
    int indent;
} NavLoc;
static NavLoc nav_locs[12];
static int nav_count;

/* modes */
enum {
    M_FOLDER = 0, M_NAVPANE, M_RENAME, M_NEWFOLDER,
    M_CONFIRM, M_PROPERTIES, M_SEARCH, M_DEST
};

static int mode = M_FOLDER;
static int dest_op;             /* 0 copy, 1 move (M_DEST) */
static char toast_text[80];
static char edit_buf[NAME_MAX_ * 2]; /* shared text field (rename/newfolder/search) */
static int edit_len;
static const FilesKb *g_kb;
static const FilesChrome *g_chrome;

/* ops context */
static char ops_src[MAX_ITEMS][NAME_MAX_];  /* selected names at op start */
static int ops_count;
static char ops_dir[120];

/* search model (recursive, W10M searches the folder + children) */
typedef struct {
    char name[NAME_MAX_];
    char parent[120];
    char icon;
} SearchRow;
static SearchRow search_rows[64];
static int search_row_count;
static char search_base[120];

static int path_join(char *dst, int cap, const char *dir, const char *name);
static int item_icon(const char *name, int folder);

static void search_scan(const char *path, int depth)
{
    if (depth > 3)
        return;
    char pattern[160];
    path_join(pattern, sizeof(pattern), path, "*");
    W16 w[160];
    wide_(w, pattern, 160);
    FindData data;
    void *f = NT_CALL(FindFirstFileW, (w, &data));
    if (!f || f == (void *)(uintptr_t)0xFFFFFFFFu)
        return;
    do {
        if (data.name[0] == '.')
            continue;
        char name[NAME_MAX_];
        narrow_(name, data.name, NAME_MAX_);
        if (!name[0])
            continue;
        int folder = (data.attrs & 16u) != 0;
        if (search_row_count < 64) {
            SearchRow *r = &search_rows[search_row_count++];
            xcopy(r->name, name, xstrlen(name) + 1);
            xcopy(r->parent, path, xstrlen(path) + 1);
            r->icon = (char)item_icon(name, folder);
        }
        if (folder) {
            char sub[160];
            path_join(sub, sizeof(sub), path, name);
            search_scan(sub, depth + 1);
        }
    } while (NT_CALL(FindNextFileW, (f, &data)));
    NT_CALL(FindClose, (f));
}

static void search_begin(void)
{
    xcopy(search_base, cur_path, xstrlen(cur_path) + 1);
    search_row_count = 0;
    search_scan(search_base, 0);
}

/* properties cache */
static char prop_name[NAME_MAX_ * 2];
static U32 prop_size, prop_files, prop_dirs;
static U32 prop_mtime;
static int prop_is_folder, prop_is_root;

/* ---- path helpers ---- */
static U32 prop_total, prop_free; /* drive space (properties) */

static const char *path_tail(const char *path)
{
    int n = xstrlen(path);
    while (n > 0 && path[n - 1] != '\\')
        --n;
    return path + n;
}

static int path_join(char *dst, int cap, const char *dir, const char *name)
{
    int n = xstrlen(dir);
    if (n + 1 + xstrlen(name) + 1 >= cap)
        return -1;
    xcopy(dst, dir, n);
    if (n && dst[n - 1] != '\\')
        dst[n++] = '\\';
    int m = xstrlen(name);
    xcopy(dst + n, name, m);
    dst[n + m] = 0;
    return n + m;
}

static int path_parent(char *dst, const char *src, int cap)
{
    int n = xstrlen(src);
    while (n > 0 && src[n - 1] == '\\')
        --n;
    while (n > 0 && src[n - 1] != '\\')
        --n;
    while (n > 1 && src[n - 1] == '\\')
        --n;
    if (n < 1)
        n = 1;
    if (n >= cap)
        n = cap - 1;
    xcopy(dst, src, n);
    dst[n] = 0;
    return n;
}

/* file-kind icon by extension (System.ItemTypeText proxy) — the
 * multi-layer coloured VEC icons (files_vecicons.h) */
static int item_icon(const char *name, int folder)
{
    if (folder)
        return VI_FOLDER;
    int n = xstrlen(name);
    int dot = -1;
    for (int i = n - 1; i >= 0; --i)
        if (name[i] == '.') { dot = i; break; }
    if (dot < 0)
        return VI_FILE;
    const char *e = name + dot + 1;
    if (!xicmp(e, "png") || !xicmp(e, "jpg") || !xicmp(e, "jpeg") ||
        !xicmp(e, "gif") || !xicmp(e, "bmp") || !xicmp(e, "svg"))
        return VI_IMAGE;
    if (!xicmp(e, "mp3") || !xicmp(e, "wav") || !xicmp(e, "wma") ||
        !xicmp(e, "m4a") || !xicmp(e, "mid"))
        return VI_AUDIO;
    if (!xicmp(e, "mp4") || !xicmp(e, "avi") || !xicmp(e, "wmv") ||
        !xicmp(e, "mov") || !xicmp(e, "m4v"))
        return VI_VIDEO;
    if (!xicmp(e, "html") || !xicmp(e, "htm") || !xicmp(e, "svgz"))
        return VI_HTML;
    if (!xicmp(e, "txt") || !xicmp(e, "doc") || !xicmp(e, "pdf") ||
        !xicmp(e, "ini") || !xicmp(e, "c") || !xicmp(e, "h") ||
        !xicmp(e, "xml") || !xicmp(e, "md"))
        return VI_DOC;
    return VI_FILE;
}

/* can Edge open this file type (file:// activation)? */
static int item_opens_in_edge(const char *name)
{
    int n = xstrlen(name);
    int dot = -1;
    for (int i = n - 1; i >= 0; --i)
        if (name[i] == '.') { dot = i; break; }
    if (dot < 0)
        return 0;
    const char *e = name + dot + 1;
    return !xicmp(e, "html") || !xicmp(e, "htm") || !xicmp(e, "md") ||
           !xicmp(e, "txt");
}

/* draw a coloured VEC type icon through the shared SVF engine */
static void vec_icon(void *dc, int id, int x, int y, int size)
{
    if (id < 0 || (unsigned)id >= sizeof(vec_icons) / sizeof(vec_icons[0]))
        id = VI_FILE;
    SVFDrawTint(dc, vec_icons[id].data, vec_icons[id].length, x, y, size, size,
                0xFFFFFFFFu);
}

/* ---- tiny number writer (no CRT) ---- */
static void auto_num(char *out, int *p, int v)
{
    if (v < 0) {
        out[(*p)++] = '-';
        v = -v;
    }
    char tmp[12];
    int n = 0;
    if (!v)
        tmp[n++] = '0';
    while (v && n < 11)
        tmp[n++] = (char)('0' + v % 10), v /= 10;
    while (n)
        out[(*p)++] = tmp[--n];
}

/* FILETIME lo/hi -> unix seconds */
static U32 ft_to_unix(U32 lo, U32 hi)
{
    U64 ft = (U64)hi << 32 | lo;
    U64 unix_s = ft / 10000000ull;
    if (unix_s > 11644473600ull)
        unix_s -= 11644473600ull;
    else
        unix_s = 0;
    return (U32)(unix_s & 0xFFFFFFFFu);
}

/* unix seconds -> "M/D/YYYY" (W10M short format, reference rows show
 * date-only: "11/8/2026", properties "Date modified: 11/8/2026") */
static void fmt_date(U32 t, char *out, int cap)
{
    /* days since epoch -> civil (Howard Hinnant's algorithm, integer) */
    long long days = (long long)t / 86400;
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        ++y;
    /* naive digits */
    char tmp[40];
    int p = 0;
    auto_num(tmp, &p, (int)m); tmp[p++] = '/';
    auto_num(tmp, &p, (int)d); tmp[p++] = '/';
    for (int i3 = 1000; i3 >= 1; i3 /= 10) tmp[p++] = (char)('0' + (y / i3) % 10);
    tmp[p] = 0;
    int n = p < cap - 1 ? p : cap - 1;
    xcopy(out, tmp, n);
    out[n] = 0;
}

/* file size -> "N bytes" / "N.N KB" / "N.N MB" / "N.N GB" */
static void fmt_size(U32 bytes, char *out, int cap)
{
    const char *unit = "bytes";
    U32 whole = bytes, tenth = 0;
    if (bytes >= 1024u * 1024u * 1024u) {
        whole = bytes / (1024u * 1024u * 1024u);
        tenth = (bytes % (1024u * 1024u * 1024u)) * 10 / (1024u * 1024u * 1024u);
        unit = "GB";
    } else if (bytes >= 1024u * 1024u) {
        whole = bytes / (1024u * 1024u);
        tenth = (bytes % (1024u * 1024u)) * 10 / (1024u * 1024u);
        unit = "MB";
    } else if (bytes >= 1024u) {
        whole = bytes / 1024u;
        tenth = (bytes % 1024u) * 10 / 1024u;
        unit = "KB";
    }
    char tmp[40];
    int p = 0;
    auto_num(tmp, &p, (int)whole);
    if (unit[0] != 'b') {
        tmp[p++] = '.';
        tmp[p++] = (char)('0' + tenth);
    }
    tmp[p++] = ' ';
    for (int i = 0; unit[i]; ++i)
        tmp[p++] = unit[i];
    tmp[p] = 0;
    int n = p < cap - 1 ? p : cap - 1;
    xcopy(out, tmp, n);
    out[n] = 0;
}

static void u64_to_dec(U64 v, char *out, int cap)
{
    char tmp[24];
    int p = 0;
    U64 x = v;
    if (!x)
        tmp[p++] = '0';
    while (x && p < 22) {
        tmp[p++] = (char)('0' + (int)(x % 10));
        x /= 10;
    }
    int n = p < cap - 1 ? p : cap - 1;
    for (int i = 0; i < n; ++i)
        out[i] = tmp[p - 1 - i];
    out[n] = 0;
}

/* ---- scan + sort ---- */
static void scan_folder(void)
{
    item_count = 0;
    any_selected = 0;
    list_scroll = 0;
    char pattern[140];
    int n = xstrlen(cur_path);
    xcopy(pattern, cur_path, n);
    pattern[n] = '\\';
    pattern[n + 1] = '*';
    pattern[n + 2] = 0;
    W16 w[140];
    wide_(w, pattern, 140);
    FindData data;
    void *f = NT_CALL(FindFirstFileW, (w, &data));
    if (!f || f == (void *)(uintptr_t)0xFFFFFFFFu || (intptr_t)f == -1)
        goto sort;
    do {
        if (data.name[0] == '.')
            continue;
        if (item_count >= MAX_ITEMS)
            break;
        Item *it = &items[item_count];
        narrow_(it->name, data.name, NAME_MAX_);
        if (!it->name[0] || xstrlen(it->name) >= NAME_MAX_ - 1)
            continue;
        it->folder = (data.attrs & 16u) != 0;
        it->size_lo = data.size_lo;
        it->mtime = ft_to_unix(data.times[4], data.times[5]);
        it->selected = 0;
        it->icon = (char)item_icon(it->name, it->folder);
        ++item_count;
    } while (NT_CALL(FindNextFileW, (f, &data)));
    NT_CALL(FindClose, (f));
sort:;
    /* insertion sort: folders first, then key asc/desc, name tiebreak */
    for (int i = 1; i < item_count; ++i) {
        Item key = items[i];
        int j = i - 1;
        while (j >= 0) {
            Item *a = &items[j];
            int gt;
            if (a->folder != key.folder) {
                gt = !a->folder; /* folders sort first */
            } else {
                int r = 0;
                if (sort_key == 1) { /* Date */
                    if (a->mtime != key.mtime)
                        r = a->mtime > key.mtime ? 1 : -1;
                } else if (sort_key == 2) { /* Size */
                    if (a->size_lo != key.size_lo)
                        r = a->size_lo > key.size_lo ? 1 : -1;
                }
                if (!r)
                    r = xicmp(a->name, key.name);
                if (sort_desc)
                    r = -r;
                gt = r > 0;
            }
            if (!gt)
                break;
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = key;
    }
}

static void count_selected(void)
{
    any_selected = 0;
    for (int i = 0; i < item_count; ++i)
        if (items[i].selected)
            any_selected = 1;
}

static int selected_count(void)
{
    int n = 0;
    for (int i = 0; i < item_count; ++i)
        n += items[i].selected != 0;
    return n;
}

/* nav pane: This device + known folders + SD card (when present) */
static int dir_exists(const char *path)
{
    W16 w[140];
    wide_(w, path, 140);
    FindData data;
    void *f = NT_CALL(FindFirstFileW, (w, &data));
    if (!f || f == (void *)(uintptr_t)0xFFFFFFFFu ||
        (intptr_t)f == -1)
        return 0;
    NT_CALL(FindClose, (f));
    return 1;
}

static void build_navpane(void)
{
    nav_count = 0;
    struct { const char *label, *rel; } known[] = {
        {"Downloads", "\\Downloads"}, {"Documents", "\\Documents"},
        {"Pictures", "\\Pictures"}, {"Music", "\\Music"}, {"Videos", "\\Videos"},
    };
    nav_locs[nav_count].label = "This device";
    nav_locs[nav_count].path = "\\fls0";
    nav_locs[nav_count].icon = VI_DEVICE;
    nav_locs[nav_count].indent = 0;
    ++nav_count;
    for (unsigned k = 0; k < sizeof(known) / sizeof(known[0]); ++k) {
        char p[80];
        path_join(p, sizeof(p), "\\fls0", known[k].rel + 1);
        if (!dir_exists(p))
            continue;
        nav_locs[nav_count].label = known[k].label;
        nav_locs[nav_count].path = 0; /* filled dynamically (static storage) */
        nav_locs[nav_count].icon = VI_FOLDER;
        nav_locs[nav_count].indent = 1;
        /* stash the path in prop_name-style static slots */
        static char slots[5][80];
        xcopy(slots[nav_count - 1], p, xstrlen(p) + 1);
        nav_locs[nav_count].path = slots[nav_count - 1];
        ++nav_count;
    }
    if (dir_exists("\\fls0\\SD Card")) {
        nav_locs[nav_count].label = "SD card";
        nav_locs[nav_count].path = "\\fls0\\SD Card";
        nav_locs[nav_count].icon = VI_SDCARD;
        nav_locs[nav_count].indent = 0;
        ++nav_count;
    }
}

static void nav_to(const char *path)
{
    xcopy(cur_path, path, xstrlen(path) + 1);
    select_mode = 0;
    scan_folder();
}

/* =====================================================================
 * Painters (procedural parts on top of the markup chrome)
 * ===================================================================== */

/* breadcrumbs: AddressBar buttons with chevrons; "..." when overflowing */
typedef struct { short x, w; int depth; } Crumb;
static Crumb crumbs[MAX_CRUMBS];
static int crumb_count;
static int crumb_overflow_base;

static void build_crumbs(void)
{
    crumb_count = 0;
    /* depth 0 = "This device", then each path segment */
    int x = 55; /* 48 (hamburger) + 7 (AddressBarButton left pad) */
    int right = 268;
    const char *seg = cur_path + xstrlen("\\fls0");
    int depths = 1;
    while (*seg == '\\')
        ++seg;
    if (*seg)
        ++depths;
    while (*seg) {
        while (*seg == '\\')
            ++seg;
        if (!*seg)
            break;
        ++depths;
        while (*seg && *seg != '\\')
            ++seg;
    }
    /* measure segments */
    struct { char name[NAME_MAX_]; int w; } segs[MAX_CRUMBS];
    int nsegs = 0;
    xcopy(segs[0].name, "This device", 12);
    segs[0].w = text_w(segs[0].name, 14) + 26; /* + chevron zone */
    ++nsegs;
    const char *p = cur_path + 7;
    while (*p == '\\')
        ++p;
    while (*p && nsegs < MAX_CRUMBS) {
        int n = 0;
        while (p[n] && p[n] != '\\' && n < NAME_MAX_ - 1)
            ++n;
        xcopy(segs[nsegs].name, p, n);
        segs[nsegs].name[n] = 0;
        segs[nsegs].w = text_w(segs[nsegs].name, 14) + 26;
        ++nsegs;
        p += n;
        while (*p == '\\')
            ++p;
    }
    /* overflow: keep leading "..." + tail that fits */
    int start = 0;
    int total = 0;
    for (int i = 0; i < nsegs; ++i)
        total += segs[i].w;
    if (total > right - x) {
        start = nsegs - 1;
        int acc = 34; /* "..." segment */
        while (start > 1 && acc + segs[start].w < right - x) {
            acc += segs[start].w;
            --start;
        }
        if (start > 1)
            --start; /* room for one more; keep the ellipsis row */
        else
            start = 1;
    }
    for (int i = start; i < nsegs && crumb_count < MAX_CRUMBS; ++i) {
        crumbs[crumb_count].x = (short)x;
        crumbs[crumb_count].w = (short)(segs[i].w);
        crumbs[crumb_count].depth = i;
        x += segs[i].w;
        ++crumb_count;
    }
    crumb_overflow_base = start;
}

static void paint_crumbs(void *dc)
{
    int i = 0;
    if (crumb_overflow_base > 1) {
        draw_text(dc, crumbs[0].x, 17, "...", C_INK, 14, 0, 0);
        fi_draw(ifill, dc, crumbs[0].x + 22, 19, 10, C_MUT, FI_CHEVRON);
        i = 1;
    }
    for (; i < crumb_count; ++i) {
        char name[NAME_MAX_];
        if (crumbs[i].depth == 0)
            xcopy(name, "This device", 12);
        else {
            const char *p = cur_path + 7;
            while (*p == '\\') ++p;
            int d = 1;
            while (*p && d < crumbs[i].depth) {
                while (*p && *p != '\\') ++p;
                while (*p == '\\') ++p;
                ++d;
            }
            int n = 0;
            while (p[n] && p[n] != '\\' && n < NAME_MAX_ - 1) ++n;
            xcopy(name, p, n);
            name[n] = 0;
        }
        int last = i == crumb_count - 1;
        draw_text(dc, crumbs[i].x, 17, name, last ? C_INK : C_MUT, 14, 0, 0);
        if (!last)
            fi_draw(ifill, dc, crumbs[i].x + crumbs[i].w - 22, 19, 10, C_MUT, FI_CHEVRON);
    }
}

/* item rows: list mode 56px, icons mode 2x88px grid */
#define ROW_H 56
#define TILE_H 88

/* content area: y84 .. (menu_open ? 420 : 440) — the app bar grows from
 * 48px (icon-only) to 68px (labels expanded) while the "..." menu is open,
 * and the system navbar owns y488..528 below it */
static NRect items_host(void)
{
    NRect r;
    r.x = 0;
    r.y = 84;
    r.w = 320;
    r.h = (short)((menu_open ? 420 : 440) - 84);
    return r;
}

static int items_content_h(void)
{
    return view_list ? item_count * ROW_H : ((item_count + 1) / 2) * TILE_H;
}

static void clamp_scroll(void)
{
    NRect host = items_host();
    int max = items_content_h() - host.h;
    if (max < 0)
        max = 0;
    if (list_scroll > max)
        list_scroll = max;
    if (list_scroll < 0)
        list_scroll = 0;
}

static void paint_scrollbar(void *dc, NRect host)
{
    int content = items_content_h();
    if (content <= host.h)
        return;
    int thumb = host.h * host.h / content;
    if (thumb < 24)
        thumb = 24;
    int track = host.h - thumb;
    int max_scroll = content - host.h;
    int y = host.y + (max_scroll ? list_scroll * track / max_scroll : 0);
    fill(dc, 316, y, 320, y + thumb, RGB_(0xC8, 0xC8, 0xC8));
}

static void paint_items(void *dc)
{
    NRect host = items_host();
    clip_set(host.x, host.y, host.x + host.w, host.y + host.h);
    if (view_list) {
        for (int i = 0; i < item_count; ++i) {
            int y = host.y + i * ROW_H - list_scroll;
            if (y + ROW_H < host.y || y > host.y + host.h)
                continue;
            Item *it = &items[i];
            if (it->selected)
                fill(dc, 0, y, 320, y + ROW_H, C_SEL);
            /* row metrics off the references: browse icon x20, title x72;
             * select mode slides both right by the 20px checkbox + gap
             * (multi_select.png: checkbox x12, icon x52, title x104) */
            int xicon = select_mode ? 52 : 20;
            vec_icon(dc, it->icon, xicon, y + 10, 36);
            /* line 2: folders show the date, files "size date"
             * (subfolder_downloads_content_icons.png) */
            char line2[80];
            if (it->folder) {
                fmt_date(it->mtime, line2, 60);
            } else {
                char sz[40], dt[40];
                fmt_size(it->size_lo, sz, 40);
                fmt_date(it->mtime, dt, 40);
                xcopy(line2, sz, xstrlen(sz));
                int p = xstrlen(sz);
                line2[p++] = ' ';
                xcopy(line2 + p, dt, xstrlen(dt) + 1);
            }
            draw_text(dc, select_mode ? 104 : 72, y + 10, it->name, C_INK, 18, 0, 0);
            draw_text(dc, select_mode ? 104 : 72, y + 34, line2, C_MUT, 12, 0, 0);
            if (select_mode) {
                /* inline checkbox (Tile CheckMode) */
                int bx = 10, by = y + (ROW_H - 20) / 2;
                if (it->selected) {
                    fill(dc, bx, by, bx + 20, by + 20, C_ACC);
                    /* white check */
                    fill(dc, bx + 4, by + 10, bx + 6, by + 12, RGB_(255, 255, 255));
                    fill(dc, bx + 6, by + 12, bx + 8, by + 14, RGB_(255, 255, 255));
                    fill(dc, bx + 8, by + 8, bx + 10, by + 14, RGB_(255, 255, 255));
                    fill(dc, bx + 10, by + 6, bx + 12, by + 12, RGB_(255, 255, 255));
                    fill(dc, bx + 12, by + 4, bx + 14, by + 10, RGB_(255, 255, 255));
                    fill(dc, bx + 14, by + 2, bx + 16, by + 8, RGB_(255, 255, 255));
                } else {
                    fill(dc, bx, by, bx + 20, by + 2, C_CHK_E);
                    fill(dc, bx, by + 18, bx + 20, by + 20, C_CHK_E);
                    fill(dc, bx, by, bx + 2, by + 20, C_CHK_E);
                    fill(dc, bx + 18, by, bx + 20, by + 20, C_CHK_E);
                }
            }
        }
    } else {
        for (int i = 0; i < item_count; ++i) {
            int col = i & 1, row = i >> 1;
            int x = col * 160, y = host.y + row * TILE_H - list_scroll;
            if (y + TILE_H < host.y || y > host.y + host.h)
                continue;
            Item *it = &items[i];
            if (it->selected)
                fill(dc, x, y, x + 160, y + TILE_H, C_SEL);
            vec_icon(dc, it->icon, x + 56, y + 6, 48);
            draw_text(dc, x + 80, y + 58, it->name, C_INK, 11, 1, 0);
            if (select_mode) { /* overlay checkbox top-left */
                int bx = x + 6, by = y + 6;
                if (it->selected) {
                    fill(dc, bx, by, bx + 20, by + 20, C_ACC);
                    fill(dc, bx + 4, by + 10, bx + 6, by + 12, RGB_(255, 255, 255));
                    fill(dc, bx + 6, by + 12, bx + 8, by + 14, RGB_(255, 255, 255));
                    fill(dc, bx + 8, by + 8, bx + 10, by + 14, RGB_(255, 255, 255));
                    fill(dc, bx + 10, by + 6, bx + 12, by + 12, RGB_(255, 255, 255));
                    fill(dc, bx + 12, by + 4, bx + 14, by + 10, RGB_(255, 255, 255));
                    fill(dc, bx + 14, by + 2, bx + 16, by + 8, RGB_(255, 255, 255));
                } else {
                    fill(dc, bx, by, bx + 20, by + 2, C_CHK_E);
                    fill(dc, bx, by + 18, bx + 20, by + 20, C_CHK_E);
                    fill(dc, bx, by, bx + 2, by + 20, C_CHK_E);
                    fill(dc, bx + 18, by, bx + 20, by + 20, C_CHK_E);
                }
            }
        }
    }
    clip_set(0, 0, 320, 528);
    paint_scrollbar(dc, host);
}

/* nav pane rows */
static void paint_navpane_rows(void *dc)
{
    for (int i = 0; i < nav_count; ++i) {
        int y = 49 + i * 48;
        int active = xeq(cur_path, nav_locs[i].path);
        if (active)
            fill(dc, 0, y, 256, y + 48, C_NAV);
        int ix = 12 + nav_locs[i].indent * 24;
        vec_icon(dc, nav_locs[i].icon, ix, y + 16, 16);
        draw_text(dc, ix + 20, y + 15, nav_locs[i].label, C_INK, 18, 0, 0);
    }
}

/* sort-by flyout (this_device__sort_by.png): 6 rows, selected one in
 * accent blue; the active row is what re-sorts the view when tapped */
static const char *SORT_OPTS[6] = {
    "Name (A on top)", "Name (Z on top)",
    "Size (Smallest on top)", "Size (Largest on top)",
    "Date (Oldest on top)", "Date (Newest on top)",
};
static int sort_selected(void)
{
    if (sort_key == 0)
        return sort_desc ? 1 : 0;
    if (sort_key == 2)
        return sort_desc ? 3 : 2;
    return sort_desc ? 5 : 4;
}
static void paint_sort_flyout(void *dc)
{
    /* panel + rows painted over the list (z-order above the items).
     * The dropdown panel carries a 1px outline in the same grey as the
     * row separators (this_device__sort_by.png) so the white sheet reads
     * as a raised popup on the white page. */
    fill(dc, 8, 84, 216, 300, C_PAGE);
    fill(dc, 8, 84, 216, 85, C_SEP);
    fill(dc, 8, 299, 216, 300, C_SEP);
    fill(dc, 8, 84, 9, 300, C_SEP);
    fill(dc, 215, 84, 216, 300, C_SEP);
    for (int i = 0; i < 6; ++i) {
        int y = 84 + i * 36;
        int sel = i == sort_selected();
        draw_text(dc, 20, y + 9, SORT_OPTS[i], sel ? C_ACC : C_INK, 15, 0, 0);
        if (i < 5)
            fill(dc, 8, y + 35, 216, y + 36, C_SEP);
    }
}

/* "..." overflow MenuFlyout (bottom_menu_open.png / folder_selected_listview_
 * menu_open.png): browse = Refresh/Select all/Clear selection/Properties,
 * select mode swaps Refresh for Rename. Disabled rows dim (#9A9A9A). */
static const char *OVF_BROWSE[4] = {
    "Refresh", "Select all", "Clear selection", "Properties",
};
static const char *OVF_SELECT[4] = {
    "Select all", "Clear selection", "Rename", "Properties",
};
static int overflow_disabled(int row)
{
    if (mode == M_DEST)
        return 1;
    if (select_mode) {
        if (row == 3) /* Properties: needs exactly one selection */
            return selected_count() != 1;
        if (row == 2) /* Rename: needs exactly one selection */
            return selected_count() != 1;
        return 0;
    }
    return row == 2; /* Clear selection with nothing selected */
}
static int overflow_top(void)
{
    return (menu_open ? 420 : 440) - 144;
}
static void paint_overflow(void *dc)
{
    const char **rows = select_mode ? OVF_SELECT : OVF_BROWSE;
    int top = overflow_top();
    /* scrim dims the content below the navbar, then the menu panel +
     * rows sit above everything (the W10M MenuFlyout z-order) */
    fill(dc, 0, 48, 320, top, C_SCRIM);
    fill(dc, 0, top, 320, top + 144, C_PAGE);
    for (int i = 0; i < 4; ++i) {
        int y = top + i * 36;
        int dis = overflow_disabled(i);
        draw_text(dc, 12, y + 9, rows[i], dis ? C_DIM : C_INK, 15, 0, 0);
        if (i < 3)
            fill(dc, 0, y + 35, 320, y + 36, C_SEP);
    }
}

/* properties rows (13px CaptionAlt, 2 columns) */
static void paint_props_rows(void *dc)
{
    char line[80], val[60];
    int y = 196;
    int row = 0;
    /* Date modified */
    fmt_date(prop_mtime, val, 60);
    draw_text(dc, 12, y + row * 28, "Date modified:", C_MUT, 12, 0, 0);
    draw_text(dc, 110, y + row * 28, val, C_INK, 12, 0, 0);
    ++row;
    /* Type */
    if (prop_is_root)
        xcopy(val, "System folder", 14);
    else if (prop_is_folder)
        xcopy(val, "File folder", 12);
    else {
        int dot = -1, n = xstrlen(prop_name);
        for (int i = n - 1; i >= 0; --i)
            if (prop_name[i] == '.') { dot = i; break; }
        if (dot >= 0) {
            int m = xstrlen(prop_name) - dot - 1;
            xcopy(val, prop_name + dot + 1, m);
            val[m] = 0;
            for (int i = 0; val[i]; ++i)
                if (val[i] >= 'a' && val[i] <= 'z')
                    val[i] = (char)(val[i] - 32);
            char tmp[60];
            int p2 = xstrlen(val);
            xcopy(tmp, val, p2);
            xcopy(tmp + p2, " file", 6);
            xcopy(val, tmp, xstrlen(tmp) + 1);
        } else
            xcopy(val, "File", 5);
    }
    draw_text(dc, 12, y + row * 28, "File type:", C_MUT, 12, 0, 0);
    draw_text(dc, 110, y + row * 28, val, C_INK, 12, 0, 0);
    ++row;
    if (prop_is_folder) {
        /* Contains: N files, M folders */
        char nf[16], nd[16];
        u64_to_dec(prop_files, nf, 16);
        u64_to_dec(prop_dirs, nd, 16);
        int p2 = 0;
        xcopy(line + p2, nf, xstrlen(nf)); p2 += xstrlen(nf);
        xcopy(line + p2, " files, ", 8); p2 += 8;
        xcopy(line + p2, nd, xstrlen(nd)); p2 += xstrlen(nd);
        xcopy(line + p2, " folders", 9);
        draw_text(dc, 12, y + row * 28, "Contains:", C_MUT, 12, 0, 0);
        draw_text(dc, 110, y + row * 28, line, C_INK, 12, 0, 0);
        ++row;
        fmt_size(prop_size, val, 60);
        draw_text(dc, 12, y + row * 28, "Folder size:", C_MUT, 12, 0, 0);
        draw_text(dc, 110, y + row * 28, val, C_INK, 12, 0, 0);
        ++row;
    } else if (!prop_is_root) {
        fmt_size(prop_size, val, 60);
        draw_text(dc, 12, y + row * 28, "File size:", C_MUT, 12, 0, 0);
        draw_text(dc, 110, y + row * 28, val, C_INK, 12, 0, 0);
        ++row;
    }
    /* Description section (file_property.png): Title / Authors / Comments /
     * Tags rows, values empty on this build — the layout matches the
     * reference even though no property handler is registered. */
    if (!prop_is_root) {
        ++row;
        draw_text(dc, 12, y + row * 28, "Description", C_INK, 12, 0, 0);
        fill(dc, 12, y + row * 28 + 15, 72, y + row * 28 + 16, C_MUT);
        ++row;
        static const char *DSCR[4] = {"Title", "Authors", "Comments", "Tags"};
        for (int d = 0; d < 4; ++d) {
            draw_text(dc, 12, y + row * 28, DSCR[d], C_MUT, 12, 0, 0);
            ++row;
        }
    }
}

/* =====================================================================
 * Properties computation
 * ===================================================================== */
static void props_scan_dir(const char *path, U32 *files, U32 *dirs, U32 *bytes)
{
    char pattern[140];
    int n = path_join(pattern, sizeof(pattern), path, "*");
    (void)n;
    W16 w[140];
    wide_(w, pattern, 140);
    FindData data;
    void *f = NT_CALL(FindFirstFileW, (w, &data));
    if (!f || f == (void *)(uintptr_t)0xFFFFFFFFu)
        return;
    do {
        if (data.name[0] == '.')
            continue;
        char name[NAME_MAX_];
        narrow_(name, data.name, NAME_MAX_);
        if (!name[0])
            continue;
        char sub[140];
        path_join(sub, sizeof(sub), path, name);
        if (data.attrs & 16u) {
            ++*dirs;
            props_scan_dir(sub, files, dirs, bytes);
        } else {
            ++*files;
            *bytes += data.size_lo;
        }
    } while (NT_CALL(FindNextFileW, (f, &data)));
    NT_CALL(FindClose, (f));
}

static void open_properties(const char *path, int folder)
{
    prop_files = prop_dirs = prop_size = 0;
    prop_is_root = xeq(path, "\\fls0");
    prop_is_folder = folder || prop_is_root;
    if (prop_is_root)
        xcopy(prop_name, "This device", 12);
    else
        xcopy(prop_name, path_tail(path), xstrlen(path_tail(path)) + 1);
    if (prop_is_root) {
        W16 w[8];
        wide_(w, "\\fls0", 8);
        U64 free_a = 0, total = 0, free_t = 0;
        if (NT_CALL(GetDiskFreeSpaceExW, (w, &free_a, &total, &free_t))) {
            prop_files = 0;
            prop_dirs = 0;
            props_scan_dir("\\fls0", &prop_files, &prop_dirs, &prop_size);
            prop_total = (U32)(total & 0xFFFFFFFFu);
            prop_free = (U32)(free_a & 0xFFFFFFFFu);
        } else {
            prop_total = prop_free = 0;
        }
        prop_mtime = 0;
    } else if (folder) {
        props_scan_dir(path, &prop_files, &prop_dirs, &prop_size);
        prop_mtime = 0;
        /* own mtime from a find on the parent */
        char parent[120], pattern[140];
        path_parent(parent, path, sizeof(parent));
        path_join(pattern, sizeof(pattern), parent, "*");
        W16 w[140];
        wide_(w, pattern, 140);
        FindData data;
        void *f = NT_CALL(FindFirstFileW, (w, &data));
        if (f && f != (void *)(uintptr_t)0xFFFFFFFFu) {
            do {
                char name[NAME_MAX_];
                narrow_(name, data.name, NAME_MAX_);
                if (xeq(name, prop_name)) {
                    prop_mtime = ft_to_unix(data.times[4], data.times[5]);
                    break;
                }
            } while (NT_CALL(FindNextFileW, (f, &data)));
            NT_CALL(FindClose, (f));
        }
    } else {
        prop_size = 0;
        prop_mtime = 0;
        char parent[120], pattern[140];
        path_parent(parent, path, sizeof(parent));
        path_join(pattern, sizeof(pattern), parent, "*");
        W16 w[140];
        wide_(w, pattern, 140);
        FindData data;
        void *f = NT_CALL(FindFirstFileW, (w, &data));
        if (f && f != (void *)(uintptr_t)0xFFFFFFFFu) {
            do {
                char name[NAME_MAX_];
                narrow_(name, data.name, NAME_MAX_);
                if (xeq(name, prop_name)) {
                    prop_size = data.size_lo;
                    prop_mtime = ft_to_unix(data.times[4], data.times[5]);
                    break;
                }
            } while (NT_CALL(FindNextFileW, (f, &data)));
            NT_CALL(FindClose, (f));
        }
    }
    mode = M_PROPERTIES;
}

/* =====================================================================
 * Screen composition
 * ===================================================================== */
static void toast(const char *s)
{
    xcopy(toast_text, s, xstrlen(s) + 1);
}

static void update_status_bindings(void)
{
    char buf[80];
    int nsel = selected_count();
    /* sort bar / status / empty visibility + text (multi_select.png:
     * the selection counter replaces the sort row) */
    show_named("SortBar", !select_mode && mode != M_DEST && !sort_open);
    show_named("StatusText", select_mode || mode == M_DEST);
    show_named("EmptyText", item_count == 0 && !select_mode && mode != M_DEST);
    if (mode == M_DEST) {
        const char *s = dest_op ? "Choose a folder to move to"
                                : "Choose a folder to copy to";
        dyn_set("StatusText", s);
    } else if (select_mode) {
        if (nsel == 1)
            xcopy(buf, "1 item selected", 16);
        else {
            char t[16];
            u64_to_dec(nsel, t, 16);
            int p2 = xstrlen(t);
            xcopy(buf, t, p2);
            xcopy(buf + p2, " items selected", 16);
        }
        dyn_set("StatusText", buf);
    }
    if (item_count == 0 && !select_mode)
        dyn_set("EmptyText", "This folder is empty.");
}

static void paint_all(void *dc)
{
    const UxDoc *d;
    if (mode == M_FOLDER || mode == M_DEST || mode == M_SEARCH ||
        mode == M_NAVPANE) {
        if (mode == M_SEARCH) {
            if ((d = ux_find_doc(&ux, "SearchPage"))) {
                layout_doc(d, 0, 0, 320, 528);
                paint_node(dc, d->root);
                /* live field content over the markup field rect */
                int fi = find_node("SearchField");
                if (fi >= 0) {
                    NRect r = nrect[fi];
                    ctl_field(dc, &files_renderer, r.x, r.y, r.w, r.h,
                              edit_buf, "Search", 1);
                }
                /* live filter results (recursive search set) */
                int shown = 0;
                if (edit_len) {
                    for (int i = 0; i < search_row_count && shown < 3; ++i) {
                        const char *n = search_rows[i].name;
                        int hit = 0;
                        for (int j = 0; n[j] && !hit; ++j) {
                            int k = 0;
                            while (n[j + k] && edit_buf[k] &&
                                   (n[j + k] | 32) == (edit_buf[k] | 32))
                                ++k;
                            if (k == edit_len)
                                hit = 1;
                        }
                        if (!hit)
                            continue;
                        int y = 84 + shown * ROW_H;
                        vec_icon(dc, search_rows[i].icon, 20, y + 10, 36);
                        draw_text(dc, 72, y + 10, n, C_INK, 18, 0, 0);
                        /* parent folder as the secondary line */
                        const char *pp = search_rows[i].parent;
                        if (pp[0] && !xeq(pp, search_base)) {
                            const char *tail = path_tail(pp);
                            if (!tail[0])
                                tail = "This device";
                            draw_text(dc, 72, y + 34, tail, C_MUT, 12, 0, 0);
                        }
                        ++shown;
                    }
                }
                if (g_kb) {
                    /* SIP docks above the system navbar (y231..488),
                     * like rename_folder_focused_input.png */
                    g_kb->dock(40);
                    g_kb->paint(dc);
                }
            }
        } else {
            update_status_bindings();
            if ((d = ux_find_doc(&ux, "FolderPage"))) {
                layout_doc(d, 0, 0, 320, 528);
                /* ---- app bar: browse / select / dest rows swap wholesale,
                 * and the "..." state expands the bar to show labels ---- */
                int dest = mode == M_DEST;
                int selm = select_mode && !dest;
                int exp = menu_open;
                show_named("AppBarCollapsed", !exp);
                show_named("AppBarExpanded", exp);
                show_named("AbSelect", !exp && !dest && !select_mode);
                show_named("AbNewFolder", !exp && !dest && !select_mode);
                show_named("AbView", !exp && !dest && !select_mode);
                show_named("AbSearch", !exp && !dest && !select_mode);
                show_named("AbDelete", !exp && selm);
                show_named("AbMove", !exp && selm);
                show_named("AbCopy", !exp && selm);
                show_named("AbShare", !exp && selm);
                show_named("AbDone", !exp && dest);
                show_named("AbCancel", !exp && dest);
                show_named("AbMore", !exp);
                show_named("XbSelect", exp && !dest && !select_mode);
                show_named("XbNewFolder", exp && !dest && !select_mode);
                show_named("XbView", exp && !dest && !select_mode);
                show_named("XbSearch", exp && !dest && !select_mode);
                show_named("XbDelete", exp && selm);
                show_named("XbMove", exp && selm);
                show_named("XbCopy", exp && selm);
                show_named("XbShare", exp && selm);
                show_named("XbDone", exp && dest);
                show_named("XbCancel", exp && dest);
                show_named("XbMore", exp);
                /* the view button shows the mode it switches TO */
                dyn_set("ViewGlyph", view_list ? "icons" : "list");
                dyn_set("XViewGlyph", view_list ? "icons" : "list");
                dyn_set("XViewLabel", view_list ? "Icons" : "List");
                dyn_set("SortChevron", sort_open ? "chevup" : "chevdown");
                {
                    const char *keys[3] = {"Name", "Date", "Size"};
                    dyn_set("SortByKey", keys[sort_key]);
                }
                show_named("SortBar", !dest && !select_mode);
                show_named("ChooseHeading", dest);
                paint_node(dc, d->root);
                build_crumbs();
                paint_crumbs(dc);
                paint_items(dc);
                if (sort_open)
                    paint_sort_flyout(dc);
                if (menu_open)
                    paint_overflow(dc);
            }
        }
        if (mode == M_NAVPANE) {
            if ((d = ux_find_doc(&ux, "NavPane"))) {
                layout_doc(d, 0, 0, 320, 528);
                paint_node(dc, d->root);
                paint_navpane_rows(dc);
            }
        }
    } else if (mode == M_RENAME || mode == M_NEWFOLDER) {
        const char *docn = mode == M_RENAME ? "RenameDialog" : "NewFolderDialog";
        if ((d = ux_find_doc(&ux, "FolderPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_items(dc);
        }
        if ((d = ux_find_doc(&ux, docn))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            const char *field = mode == M_RENAME ? "RenameField" : "NewFolderField";
            int fi = find_node(field);
            if (fi >= 0) {
                NRect r = nrect[fi];
                const char *hint = mode == M_RENAME ? "File name" : "Folder name";
                ctl_field(dc, &files_renderer, r.x, r.y, r.w, r.h, edit_buf, hint, 1);
            }
            if (g_kb) {
                /* the shared SIP rises above the navbar: 231..488 */
                g_kb->dock(40);
                g_kb->paint(dc);
            }
        }
    } else if (mode == M_CONFIRM) {
        if ((d = ux_find_doc(&ux, "FolderPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_items(dc);
        }
        if ((d = ux_find_doc(&ux, "ConfirmDialog"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_PROPERTIES) {
        if ((d = ux_find_doc(&ux, "PropertiesPage"))) {
            /* bindings + real drive percentage BEFORE the single paint */
            if (prop_is_root && prop_total) {
                U32 used = prop_total - prop_free;
                int pct = (int)((unsigned long long)used * 100 / prop_total);
                char v[60];
                fmt_size(used, v, 60);
                xcopy(v + xstrlen(v), " used", 6);
                dyn_set("UsedSpaceText", v);
                fmt_size(prop_free, v, 60);
                xcopy(v + xstrlen(v), " free", 6);
                dyn_set("FreeSpaceText", v);
                char pctb[16];
                u64_to_dec(pct, pctb, 16);
                xcopy(pctb + xstrlen(pctb), "% used", 8);
                dyn_set("UsedSpaceLabel", pctb);
                int b = find_node("SpaceBar");
                if (b >= 0) {
                    for (int ai = 0; ai < ux.nodes[b].attr_count; ++ai)
                        if (ux.nodes[b].attr[ai] == UXA_VALUE)
                            ux.nodes[b].num[ai] = pct;
                }
            } else {
                dyn_set("UsedSpaceText", "");
                dyn_set("FreeSpaceText", "");
                dyn_set("UsedSpaceLabel", "");
            }
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* header + rows painted over the markup chrome; the 96px
             * thumbnail renders the same multi-layer VEC type icon */
            if (prop_is_root)
                vec_icon(dc, VI_DEVICE, 12, 72, 96);
            else if (prop_is_folder)
                vec_icon(dc, VI_FOLDER, 12, 72, 96);
            else
                vec_icon(dc, item_icon(prop_name, 0), 12, 72, 96);
            /* name may overflow the 200px column: clamp paint */
            clip_set(120, 0, 316, 528);
            draw_text(dc, 120, 76, prop_name, C_INK, 18, 0, 0);
            clip_set(0, 0, 320, 528);
            paint_props_rows(dc);
        }
    }
    if (toast_text[0]) {
        fill(dc, 0, 388, 320, 440, C_NAV);
        fill(dc, 0, 388, 320, 389, C_MUT);
        draw_text(dc, 16, 409, toast_text, C_INK, 12, 0, 0);
    }
    /* system navigation bar (SHELLCHROME.DLL): always the topmost strip,
     * over content, navpane and dialogs alike */
    if (g_chrome)
        g_chrome->paint(dc);
}

/* =====================================================================
 * Commands / input
 * ===================================================================== */
static void begin_op_capture(void)
{
    ops_count = 0;
    for (int i = 0; i < item_count && ops_count < MAX_ITEMS; ++i)
        if (items[i].selected) {
            xcopy(ops_src[ops_count], items[i].name, xstrlen(items[i].name) + 1);
            ++ops_count;
        }
}

static void do_command(const char *cmd)
{
    if (xeq(cmd, "navpane")) {
        mode = M_NAVPANE;
    } else if (xeq(cmd, "navclose")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "up")) {
        if (xstrlen(cur_path) > 7) { /* keep \fls0 root */
            char parent[120];
            path_parent(parent, cur_path, sizeof(parent));
            nav_to(parent);
        }
        mode = M_FOLDER;
    } else if (xeq(cmd, "noop")) {
        /* crumbs handled in tap processing */
    } else if (xeq(cmd, "select")) {
        select_mode = 1;
        for (int i = 0; i < item_count; ++i)
            items[i].selected = 0;
    } else if (xeq(cmd, "selectall")) {
        for (int i = 0; i < item_count; ++i)
            items[i].selected = 1;
        count_selected();
    } else if (xeq(cmd, "clearsel")) {
        select_mode = 0;
        for (int i = 0; i < item_count; ++i)
            items[i].selected = 0;
        count_selected();
    } else if (xeq(cmd, "newfolder")) {
        edit_len = 0;
        edit_buf[0] = 0;
        mode = M_NEWFOLDER;
    } else if (xeq(cmd, "newfolder-ok")) {
        if (edit_len) {
            char path[160];
            path_join(path, sizeof(path), cur_path, edit_buf);
            W16 w[160];
            wide_(w, path, 160);
            NT_CALL(CreateDirectoryW, (w, 0));
            scan_folder();
        }
        mode = M_FOLDER;
    } else if (xeq(cmd, "newfolder-cancel")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "search")) {
        edit_len = 0;
        edit_buf[0] = 0;
        search_begin();
        mode = M_SEARCH;
    } else if (xeq(cmd, "search-back")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "search-clear")) {
        edit_len = 0;
        edit_buf[0] = 0;
    } else if (xeq(cmd, "more")) {
        /* "..." expands the app bar (labels) + opens the MenuFlyout */
        menu_open = !menu_open;
        sort_open = 0;
        clamp_scroll();
    } else if (xeq(cmd, "sortbar")) {
        sort_open = !sort_open;
        menu_open = 0;
        clamp_scroll();
    } else if (xeq(cmd, "view")) {
        view_list = !view_list;
        list_scroll = 0;
        menu_open = 0;
        clamp_scroll();
    } else if (xeq(cmd, "refresh")) {
        scan_folder();
        menu_open = 0;
    } else if (xeq(cmd, "delete")) {
        begin_op_capture();
        xcopy(ops_dir, cur_path, xstrlen(cur_path) + 1);
        if (!ops_count)
            mode = M_FOLDER;
        else
            mode = M_CONFIRM;
    } else if (xeq(cmd, "confirm-ok")) {
        toast("Deleting files...");
        for (int i = 0; i < ops_count; ++i) {
            char path[160];
            path_join(path, sizeof(path), ops_dir, ops_src[i]);
            W16 w[160];
            wide_(w, path, 160);
            if (!NT_CALL(DeleteFileW, (w)))
                NT_CALL(RemoveDirectoryW, (w));
        }
        scan_folder();
        select_mode = ops_count = 0;
        mode = M_FOLDER;
    } else if (xeq(cmd, "confirm-cancel")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "copyto") || xeq(cmd, "moveto")) {
        begin_op_capture();
        xcopy(ops_dir, cur_path, xstrlen(cur_path) + 1);
        if (!ops_count) {
            mode = M_FOLDER;
            return;
        }
        dest_op = xeq(cmd, "moveto");
        xcopy(cur_path, "\\fls0", 7);
        cur_path[6] = 0;
        select_mode = 0;
        menu_open = sort_open = 0;
        scan_folder();
        mode = M_DEST;
    } else if (xeq(cmd, "dest-go")) {
        /* perform the pending copy/move into cur_path */
        for (int i = 0; i < ops_count; ++i) {
            char src[180], dst[180];
            path_join(src, sizeof(src), ops_dir, ops_src[i]);
            path_join(dst, sizeof(dst), cur_path, ops_src[i]);
            W16 ws[180], wd[180];
            wide_(ws, src, 180);
            wide_(wd, dst, 180);
            if (dest_op)
                NT_CALL(MoveFileW, (ws, wd));
            else
                NT_CALL(CopyFileW, (ws, wd, 0));
        }
        toast(dest_op ? "Moving files..." : "Copying files...");
        nav_to(cur_path);
        mode = M_FOLDER;
        ops_count = 0;
    } else if (xeq(cmd, "dest-cancel")) {
        xcopy(cur_path, ops_dir, xstrlen(ops_dir) + 1);
        scan_folder();
        select_mode = 1;
        mode = M_FOLDER;
    } else if (xeq(cmd, "rename")) {
        int sel = -1;
        for (int i = 0; i < item_count; ++i)
            if (items[i].selected)
                sel = i;
        menu_open = 0;
        if (sel < 0) {
            mode = M_FOLDER;
            return;
        }
        xcopy(edit_buf, items[sel].name, xstrlen(items[sel].name) + 1);
        edit_len = xstrlen(edit_buf);
        dyn_set("RenameTitle", items[sel].folder ? "Rename folder" : "Rename file");
        dyn_set("RenameLabel", items[sel].folder ? "Folder name" : "File name");
        mode = M_RENAME;
    } else if (xeq(cmd, "rename-ok")) {
        int sel = -1;
        for (int i = 0; i < item_count; ++i)
            if (items[i].selected)
                sel = i;
        if (sel >= 0 && edit_len) {
            char from[180], to[180];
            path_join(from, sizeof(from), cur_path, items[sel].name);
            path_join(to, sizeof(to), cur_path, edit_buf);
            W16 wf[180], wt[180];
            wide_(wf, from, 180);
            wide_(wt, to, 180);
            if (!NT_CALL(MoveFileW, (wf, wt)))
                toast("Something went wrong. Could not rename.");
        }
        scan_folder();
        select_mode = 0;
        mode = M_FOLDER;
    } else if (xeq(cmd, "rename-cancel")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "rename-clear") || xeq(cmd, "newfolder-clear")) {
        /* the X affordance inside the dialog field (reference input) */
        edit_len = 0;
        edit_buf[0] = 0;
    } else if (xeq(cmd, "properties")) {
        int sel = -1;
        for (int i = 0; i < item_count; ++i)
            if (items[i].selected)
                sel = i;
        menu_open = 0;
        if (sel < 0) {
            open_properties(cur_path, 1);
        } else {
            char path[160];
            path_join(path, sizeof(path), cur_path, items[sel].name);
            open_properties(path, items[sel].folder);
        }
    } else if (xeq(cmd, "props-back")) {
        mode = M_FOLDER;
    } else if (xeq(cmd, "share")) {
        toast("This item will be shared");
        menu_open = 0;
        mode = M_FOLDER;
    }
}

/* "..." overflow rows (mode-dependent: browse vs select) */
static void overflow_row_tap(int row)
{
    if (overflow_disabled(row))
        return;
    if (select_mode) {
        switch (row) {
        case 0: do_command("selectall"); break;
        case 1: do_command("clearsel"); break;
        case 2: do_command("rename"); break;
        case 3: do_command("properties"); break;
        }
    } else {
        switch (row) {
        case 0: do_command("refresh"); break;
        case 1: do_command("select"); do_command("selectall"); break;
        case 2: do_command("clearsel"); break;
        case 3: do_command("properties"); break;
        }
    }
}

/* sort flyout rows: key + direction, then re-sort */
static void sort_row_tap(int row)
{
    sort_key = row < 2 ? 0 : (row < 4 ? 2 : 1);
    sort_desc = (row & 1) != 0;
    sort_open = 0;
    scan_folder();
}

/* launch Edge with a file:// activation argument (the "open in Edge"
 * story): host runs the target module in-process, device writes the
 * intent file + hands off through the serial launcher. Either way the
 * files app quits right after, like the W10M foreground switch. */
static void open_in_edge(const char *dir, const char *name)
{
    static const W16 edge_exe[] = {'\\', 'f', 'l', 's', '0', '\\', 'A', 'p',
                                   'p', 's', '\\', 'e', 'd', 'g', 'e', '\\',
                                   'e', 'd', 'g', 'e', '.', 'e', 'x', 'e', 0};
    char url[200];
    xcopy(url, "file://", 7);
    path_join(url + 7, (int)sizeof(url) - 7, dir, name);
    W16 wurl[200];
    wide_(wurl, url, 200);
    if (NT_CALL(CreateProcessW, (edge_exe, wurl, 0, 0, 0, 0, 0, 0, 0, 0))) {
        NT_CALL(PostQuitMessage, (0));
        return;
    }
    toast("This file can't be opened on this device");
}

static void item_tap(int idx, int x, int y)
{
    (void)x; (void)y;
    if (idx < 0 || idx >= item_count)
        return;
    Item *it = &items[idx];
    if (mode == M_DEST) {
        if (it->folder) {
            char path[160];
            path_join(path, sizeof(path), cur_path, it->name);
            nav_to(path);
        }
        return;
    }
    if (select_mode) {
        it->selected = !it->selected;
        count_selected();
        return;
    }
    if (it->folder) {
        char path[160];
        path_join(path, sizeof(path), cur_path, it->name);
        nav_to(path);
    } else if (item_opens_in_edge(it->name)) {
        open_in_edge(cur_path, it->name);
    } else {
        toast("This file can't be opened on this device");
    }
}

static void edit_key(unsigned wp)
{
    if (wp == 8) {
        if (edit_len)
            edit_buf[--edit_len] = 0;
    } else if (wp == 13) {
        if (mode == M_RENAME)
            do_command("rename-ok");
        else if (mode == M_NEWFOLDER)
            do_command("newfolder-ok");
        else
            mode = M_FOLDER;
    } else if (wp == 27) {
        mode = M_FOLDER;
    } else if (wp >= 32 && wp < 127 && edit_len < NAME_MAX_ * 2 - 2) {
        edit_buf[edit_len++] = (char)wp;
        edit_buf[edit_len] = 0;
    }
}

static void process_tap(int x, int y)
{
    const UxDoc *d;
    const UxNode *b;
    if (mode == M_RENAME || mode == M_NEWFOLDER) {
        if (y >= 231 && g_kb) { /* docked SIP (y231..488) */
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int c = g_kb->input(WM_DOWN_, 1, lp);
            if (c > 0)
                edit_key((unsigned)c);
            return;
        }
        if (y >= 231) { /* navbar strip: handled by the chrome in wndproc */
            return;
        }
        if (y >= 228) { /* scrim below the sheet: light dismiss */
            mode = M_FOLDER;
            return;
        }
        const char *docn = mode == M_RENAME ? "RenameDialog" : "NewFolderDialog";
        if ((d = ux_find_doc(&ux, docn)) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    if (mode == M_SEARCH) {
        if (y >= 231 && g_kb) {
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int c = g_kb->input(WM_DOWN_, 1, lp);
            if (c > 0)
                edit_key((unsigned)c);
            return;
        }
        /* result rows: navigate to the parent folder and select the item */
        if (y >= 84 && y < 231 && edit_len) {
            int shown = 0;
            for (int i = 0; i < search_row_count && shown < 3; ++i) {
                const char *n = search_rows[i].name;
                int hit = 0;
                for (int j = 0; n[j] && !hit; ++j) {
                    int k = 0;
                    while (n[j + k] && edit_buf[k] &&
                           (n[j + k] | 32) == (edit_buf[k] | 32))
                        ++k;
                    if (k == edit_len)
                        hit = 1;
                }
                if (!hit)
                    continue;
                int ry = 84 + shown * ROW_H;
                if (y >= ry && y < ry + ROW_H) {
                    if (search_rows[i].icon == VI_FOLDER) {
                        /* folder result: navigate into it */
                        char path[160];
                        path_join(path, sizeof(path), search_rows[i].parent,
                                  search_rows[i].name);
                        nav_to(path);
                    } else {
                        nav_to(search_rows[i].parent);
                        select_mode = 1;
                        for (int q = 0; q < item_count; ++q)
                            items[q].selected = xeq(items[q].name,
                                                    search_rows[i].name);
                        count_selected();
                    }
                    mode = M_FOLDER;
                    return;
                }
                ++shown;
            }
        }
        if ((d = ux_find_doc(&ux, "SearchPage")) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        else if (y < 231)
            mode = M_FOLDER;
        return;
    }
    if (mode == M_NAVPANE) {
        if (x >= 256) { /* scrim */
            mode = M_FOLDER;
            return;
        }
        if (y >= 49) {
            int idx = (y - 49) / 48;
            if (idx >= 0 && idx < nav_count && nav_locs[idx].path) {
                nav_to(nav_locs[idx].path);
                mode = M_FOLDER;
                return;
            }
        }
        mode = M_FOLDER;
        return;
    }
    if (mode == M_CONFIRM) {
        if ((d = ux_find_doc(&ux, "ConfirmDialog")) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        else if (y >= 236 && y < 488)
            mode = M_FOLDER; /* scrim below the sheet: light dismiss */
        return;
    }
    if (mode == M_PROPERTIES) {
        if ((d = ux_find_doc(&ux, "PropertiesPage")) && (b = hit_test(d, x, y)))
            do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    /* folder / dest modes */
    if (sort_open) {
        /* flyout rows + light dismiss */
        if (x >= 8 && x < 216 && y >= 84 && y < 300)
            sort_row_tap((y - 84) / 36);
        else
            sort_open = 0;
        return;
    }
    if (menu_open) {
        int top = overflow_top();
        if (x >= 0 && x < 320 && y >= top && y < top + 144)
            overflow_row_tap((y - top) / 36);
        else
            menu_open = 0; /* light dismiss (also via the "..." toggle) */
        return;
    }
    if (y >= 48 && y < 440) {
        /* breadcrumb strip handled below (needs crumb rects) */
        if (y >= 84) {
            NRect host = items_host();
            if (y >= host.y && y < host.y + host.h) {
                int idx;
                if (view_list)
                    idx = (y - host.y + list_scroll) / ROW_H;
                else {
                    int col = x >= 160 ? 1 : 0;
                    int row = (y - host.y + list_scroll) / TILE_H;
                    idx = row * 2 + col;
                }
                item_tap(idx, x, y);
                return;
            }
        }
    }
    if ((d = ux_find_doc(&ux, "FolderPage")) && (b = hit_test(d, x, y))) {
        /* crumb buttons: navigate to depth */
        if (b == ux_find_name(&ux, d, "CrumbHost")) {
            for (int i = 0; i < crumb_count; ++i)
                if (x >= crumbs[i].x && x < crumbs[i].x + crumbs[i].w) {
                    int depth = crumbs[i].depth;
                    if (crumb_overflow_base > 1 && i == 0 && depth >= crumb_overflow_base)
                        depth = crumb_overflow_base;
                    if (depth == 0) {
                        nav_to("\\fls0");
                    } else {
                        char path[140];
                        int n2 = 7;
                        xcopy(path, "\\fls0", 7);
                        const char *p = cur_path + 7;
                        while (*p == '\\') ++p;
                        int dd = 1;
                        while (*p && dd < depth) {
                            while (*p && *p != '\\') { if (n2 < 138) path[n2++] = *p; ++p; }
                            while (*p == '\\') ++p;
                            ++dd;
                        }
                        while (*p && *p != '\\') { if (n2 < 138) path[n2++] = *p; ++p; }
                        path[n2] = 0;
                        nav_to(path);
                    }
                    mode = M_FOLDER;
                    return;
                }
            return;
        }
        const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
        if (cmd && *cmd)
            do_command(cmd);
        return;
    }
    if (select_mode && y >= 48 && y < 84) {
        /* status strip tap = clear selection (W10M light-dismiss) */
        do_command("clearsel");
    }
}

/* =====================================================================
 * entry
 * ===================================================================== */
#define UX_FILE_MAX (48 * 1024)
static unsigned char ux_blob[UX_FILE_MAX];

static int load_markup(void)
{
    static const W16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 'f', 'i', 'l',
                               'e', 's', '.', 'u', 'x', 'a', 'm', 'l', 0};
    void *h = NT_CALL(CreateFileW, (path, 0x80000000u, 0, 0, 3, 0, 0));
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

static void *g_wnd;

static void nav_bar_action(int key);

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
        if (wp == 0x1b) { /* hardware back */
            if (mode == M_FOLDER)
                NT_CALL(PostQuitMessage, (0));
            else if (mode == M_DEST)
                do_command("dest-cancel");
            else
                mode = M_FOLDER;
        } else if (mode == M_RENAME || mode == M_NEWFOLDER || mode == M_SEARCH) {
            if (msg == WM_CHAR_ || wp < 128)
                edit_key(wp);
        } else if (mode == M_FOLDER && (wp == 's' || wp == 'S')) {
            mode = M_SEARCH;
            edit_len = 0;
            edit_buf[0] = 0;
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_ || msg == WM_MOVE_ || msg == WM_UP_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        (void)x;
        /* system navbar (y488..528): the SHELLCHROME chrome owns the strip
         * — 4/5/6 = Back/Home/Search, 1 = consumed, 0 = not ours */
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
            return 0; /* press tracked by the bar */
        }
        if (msg == WM_UP_)
            return 0;
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        toast_text[0] = 0;
        drag_y = y;
        dragging = 0;
        process_tap(x, y);
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_MOVE_) {
        /* touch drag scrolls the item list (direct manipulation, the
         * W10M list feel) once the finger moves > 8px vertically */
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        (void)x;
        if (mode == M_FOLDER && !sort_open && !menu_open && !select_mode) {
            NRect host = items_host();
            int dy = drag_y - y;
            if (dragging || dy > 8 || dy < -8) {
                if (!dragging)
                    toast_text[0] = 0; /* the press became a scroll */
                dragging = 1;
                list_scroll += dy;
                clamp_scroll();
                drag_y = y;
                NT_CALL(InvalidateRect, (hwnd, 0, 0));
                NT_CALL(UpdateWindow, (hwnd));
            }
        }
        return 0;
    }
    return 0;
}

static const W16 cls_name[] = {'F', 'i', 'l', 'e', 'E', 'x', 'p', 'l', 'o',
                               'r', 'e', 'r', 0};

/* navbar keys (SHELLCHROME hosted input codes): Back walks out of the
 * current surface like the hardware back key, Home jumps to the device
 * root, Search opens the search row with the SIP */
static void nav_bar_action(int key)
{
    if (key == 4) { /* Back */
        if (mode == M_FOLDER) {
            if (xstrlen(cur_path) > 7) {
                char parent[120];
                path_parent(parent, cur_path, sizeof(parent));
                nav_to(parent);
            } else {
                NT_CALL(PostQuitMessage, (0));
            }
        } else if (mode == M_DEST) {
            do_command("dest-cancel");
        } else {
            mode = M_FOLDER;
        }
        sort_open = 0;
        menu_open = 0;
    } else if (key == 5) { /* Home */
        nav_to("\\fls0");
        mode = M_FOLDER;
        select_mode = 0;
        sort_open = 0;
        menu_open = 0;
    } else if (key == 6) { /* Search */
        mode = M_SEARCH;
        edit_len = 0;
        edit_buf[0] = 0;
    }
}

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u;
    xcopy(cur_path, "\\fls0", 7);
    cur_path[6] = 0;
    scan_folder();
    build_navpane();
    /* host-side IAT snap: uixctl.c + svf.c (linked into this DLL) share the
     * render-context slot the device loader would snap per module */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
    /* shared keyboard DLL (FINGERKB.DLL), snapped by the host loader */
    g_kb = NT_CALL(UIXGetSchemaFactory, ());
    if (!g_kb || g_kb->profile != 0x55495831u)
        g_kb = 0;
    /* system nav-bar chrome (SHELLCHROME.DLL): the strip edge/settings
     * shows under every hosted app — files themes it light */
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
