/* cortana_app.c — Cortana (W10M) POC for NTOS, driven by uXAML markup.
 *
 * Reconstruction of Microsoft.Windows.Cortana_cw5n1h2txyewy (RS1) from its
 * Views/*.xbf pages + resources.pri strings (cortana/analysis/) and the
 * real-device captures in refs/cortana/: the light "How can I help?" home
 * with the layered-VEC orb, the shared-SIP query flow, the "Thinking..."
 * state and the offline error ("I can't connect at the moment."). This is
 * the demo app — no speech stack, the conversation is the UI itself.
 *
 * Component reuse (same shared-DLL model as edge/ and files/):
 *   - keyboard: FINGERKB.DLL module (host loader snaps UIXGetSchemaFactory)
 *   - controls + SVF: payload/phone/uixctl.c + svf.c compiled in (light)
 *   - uXAML engine: the exact edge/app/uxaml.c loader
 *   - glyphs: files/app/files_icons.c (hamburger/mic/music/x)
 */
#include "nt_app.h"
#include "uxaml.h"
#include "files_icons.h"
#include "cortana_vec.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

typedef uint64_t U64;

/* ---- shared phone-framework ABI (mirror of payload/phone/phone.h) ---- */
typedef struct CortRender {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme; /* UIX_TH_* table (see phone.h) */
} CortRender;

typedef struct CortKb {
    U32 profile; /* 0x55494831 "UIX1" */
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
} CortKb;

void ctl_field(void *dc, const CortRender *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused);
int SVFDrawTint(void *dc, const void *data, unsigned length, int x, int y,
                int w, int h, U32 tint);

NT_DEFINE(render_SpGetThreadContext)
NT_IMPORT(const CortKb *, UIXGetSchemaFactory, (void))
NT_DEFINE(UIXGetSchemaFactory)
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
NT_IMPORT(int, SipShowIM, (U32))
NT_DEFINE(SipShowIM)

/* ---- palette (measured from refs/cortana/*.png, light theme) ---- */
#define C_PAGE  RGB_(0xF7, 0xF3, 0xF7) /* page background              */
#define C_BAND  RGB_(0xDE, 0xDB, 0xDE) /* search bar band              */
#define C_INK   RGB_(0x00, 0x00, 0x00)
#define C_MUT   RGB_(0x5A, 0x5A, 0x5A)
#define C_BLUE  RGB_(0x3E, 0x6B, 0xE0) /* greeting / orb text blue     */
#define C_ACC   RGB_(0x00, 0x78, 0xD7) /* mic button                   */

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
                      int align)
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

/* ---- render vtable (light theme, consumed by fingerkb + uixctl) ---- */
static void r_clip(int l, int t, int r, int b) { clip_set(l, t, r, b); }
static void r_fill(void *dc, int l, int t, int r, int b, U32 c) { fill(dc, l, t, r, b, c); }
static void r_text(void *dc, int x, int y, const char *s, U32 c, int h)
{
    draw_text(dc, x, y, s, c, h, 0);
}
static void r_line(void *dc, int x, int y, int xx, int yy, U32 c)
{
    fi_line(ifill, dc, x, y, xx, yy, 1, c);
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
    if (ymin < clip_rect.top) ymin = clip_rect.top;
    if (ymax > clip_rect.bottom) ymax = clip_rect.bottom;
    for (int y = ymin; y < ymax; ++y) {
        int xs[255];
        unsigned used = 0;
        for (unsigned i = 0, j = n - 1; i < n; j = i++)
            if ((p[i].y <= y && p[j].y > y) || (p[j].y <= y && p[i].y > y)) {
                int num = (y - p[i].y) * (p[j].x - p[i].x);
                int den = p[j].y - p[i].y;
                xs[used++] = p[i].x + num / den;
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

static const U32 cort_theme[] = {
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
};

static const CortRender cort_renderer = {
    0x55495831u, r_clip, r_fill, r_text, r_line,
    r_ellipse, r_polygon, r_surface, cort_theme,
};

__attribute__((dllexport)) const CortRender *render_SpGetThreadContext(void)
{
    return &cort_renderer;
}
__attribute__((dllexport)) int svf_SVFDrawTint(void *dc, const void *data,
                                               unsigned length, int x, int y,
                                               int w, int h, U32 tint)
{
    return SVFDrawTint(dc, data, length, x, y, w, h, tint);
}

/* ---- uXAML engine glue (same layout/paint model as files_app.c) ---- */
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
    static const UxDoc *done[UX_MAX_DOCS];
    static unsigned char done_flag[UX_MAX_DOCS];
    unsigned di = (unsigned)(d - ux.docs);
    if (di < UX_MAX_DOCS && done_flag[di] && done[di] == d &&
        nrect[d->root].w == (short)bw)
        return; /* static absolute geometry: layout once */
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
        for (int ci = 0; ci < (int)n->child_count; ++ci) {
            unsigned c = n->child[ci];
            const UxNode *cn = &ux.nodes[c];
            int cx = ux_int(&ux, cn, UXA_X, 0);
            int cy = ux_int(&ux, cn, UXA_Y, 0);
            int cw = ux_int(&ux, cn, UXA_W, 0);
            int ch = ux_int(&ux, cn, UXA_H, 0);
            if (!cw) cw = pr.w;
            if (!ch) ch = pr.h;
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

/* dynamic text bindings */
typedef struct { char name[24]; char text[96]; } DynText;
static DynText dyn_texts[8];
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
    if (slot < 0 && dyn_count < 8) {
        slot = dyn_count++;
        int n = xstrlen(name);
        if (n > 23) n = 23;
        xcopy(dyn_texts[slot].name, name, n);
        dyn_texts[slot].name[n] = 0;
    }
    if (slot < 0)
        return;
    int n = xstrlen(text);
    if (n > 95) n = 95;
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
    const char *t;
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
    case UX_GLYPH: {
        node_text(n, &t);
        int id = fi_find(t);
        U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
        int sz = r.w < r.h ? r.w : r.h;
        fi_draw(ifill, dc, r.x, r.y, sz, col, id);
        break;
    }
    case UX_TEXT:
    case UX_BUTTON: {
        node_text(n, &t);
        if (n->kind == UX_BUTTON) {
            U32 bg = ux_clr(&ux, n, UXA_BG, 0xFFFFFFFFu);
            if (bg != 0xFFFFFFFFu)
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg);
        }
        if (t && *t) {
            int size = ux_int(&ux, n, UXA_SIZE, 12);
            int align = ux_int(&ux, n, UXA_ALIGN, 0);
            U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
            int tx = r.x, ty = r.y;
            if (align == 1)
                tx += r.w / 2;
            else if (align == 2)
                tx += r.w;
            draw_text(dc, tx, ty, t, col, size, align);
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
 * App state
 * ===================================================================== */
enum { M_HOME = 0, M_INPUT, M_THINK, M_ERROR, M_MUSIC };
static int mode = M_HOME;
static char query[64];
static int query_len;
static const CortKb *g_kb;

/* orb geometry (measured from refs/cortana/cortana_light.png) */
#define ORB_CX 160
#define ORB_CY 48
#define ORB_D 48

static void paint_orb(void *dc, int thinking)
{
    const CortVec *v = &cort_vec[thinking ? CV_RING_THINK : CV_RING_HOME];
    SVFDrawTint(dc, v->data, v->length, ORB_CX - ORB_D / 2, ORB_CY - ORB_D / 2,
                ORB_D, ORB_D, 0xFFFFFFFFu);
}

static void paint_all(void *dc)
{
    const UxDoc *d;
    const char *docn = mode == M_THINK ? "ThinkingPage"
                     : mode == M_ERROR ? "ErrorPage" : "HomePage";
    if ((d = ux_find_doc(&ux, docn))) {
        layout_doc(d, 0, 0, 320, 528);
        if (mode == M_HOME || mode == M_ERROR)
            show_named("SearchHint", 1);
        paint_node(dc, d->root);
        paint_orb(dc, mode == M_THINK);
        if (mode == M_INPUT) {
            /* the SIP covers the bottom half (y271..528) like the reference
             * ask state: the query field floats directly above it */
            fill(dc, 12, 228, 308, 264, C_BAND);
            fill(dc, 16, 232, 304, 260, RGB_(0xFF, 0xFF, 0xFF));
            ctl_field(dc, &cort_renderer, 20, 236, 280, 20, query,
                      "Type here to search", 1);
        }
        if (mode == M_INPUT && g_kb)
            g_kb->paint(dc);
    }
}

/* =====================================================================
 * Input
 * ===================================================================== */
static void edit_key(unsigned wp)
{
    if (wp == 8) {
        if (query_len)
            query[--query_len] = 0;
    } else if (wp == 13) {
        mode = query_len ? M_THINK : M_HOME;
    } else if (wp == 27) {
        mode = M_HOME;
    } else if (wp >= 32 && wp < 127 && query_len < (int)sizeof(query) - 2) {
        query[query_len++] = (char)wp;
        query[query_len] = 0;
    }
}

static void do_command(const char *cmd)
{
    if (xeq(cmd, "search")) {
        mode = M_INPUT;
    } else if (xeq(cmd, "cancel")) {
        mode = M_HOME;
        query[0] = 0;
        query_len = 0;
    } else if (xeq(cmd, "music")) {
        /* MusicRecoPage: no mic hardware on this build - straight to the
         * same offline error the reference shows without a connection */
        mode = M_ERROR;
    } else if (xeq(cmd, "noop")) {
        /* hamburger: the notebook menu is out of scope for the demo */
    }
}

static void process_tap(int x, int y)
{
    const UxDoc *d;
    const UxNode *b;
    if (mode == M_INPUT) {
        if (y >= 271 && g_kb) {
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int c = g_kb->input(WM_DOWN_, 1, lp);
            if (c > 0)
                edit_key((unsigned)c);
            return;
        }
        mode = M_HOME; /* light dismiss */
        return;
    }
    if (mode == M_THINK) {
        /* any tap (or the X) resolves the query: no network on the device,
         * so the answer is the offline error - exactly the reference flow */
        mode = M_ERROR;
        return;
    }
    if ((d = ux_find_doc(&ux, "HomePage")) && (b = hit_test(d, x, y))) {
        do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    if ((d = ux_find_doc(&ux, "ErrorPage")) && (b = hit_test(d, x, y))) {
        do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
    if ((d = ux_find_doc(&ux, "ThinkingPage")) && (b = hit_test(d, x, y))) {
        do_command(ux_str(&ux, b, UXA_COMMAND, ""));
        return;
    }
}

/* =====================================================================
 * entry
 * ===================================================================== */
#define UX_FILE_MAX (48 * 1024)
static unsigned char ux_blob[UX_FILE_MAX];

static int load_markup(void)
{
    static const W16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 'c', 'o', 'r',
                               't', 'a', 'n', 'a', '.', 'u', 'x', 'a', 'm',
                               'l', 0};
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
            if (mode == M_HOME)
                NT_CALL(PostQuitMessage, (0));
            else
                mode = M_HOME;
        } else if (mode == M_INPUT) {
            if (msg == WM_CHAR_ || wp < 128)
                edit_key(wp);
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        process_tap(x, y);
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

static const W16 cls_name[] = {'C', 'o', 'r', 't', 'a', 'n', 'a', 0};

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u;
    /* host-side IAT snap: uixctl.c + svf.c (linked into this DLL) share the
     * render-context slot the device loader would snap per module */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
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
