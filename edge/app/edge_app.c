/* edge_app.c — "Edge" mobile POC for NTOS, driven by uXAML markup.
 *
 * Reconstructs the Windows 10 Mobile Edge chrome (SmallScreenPage with
 * bottom [tabs][address][...] bar, ActionsMenu sheet, TabCenterPage,
 * HubPanel pivots, AddressEditBox + SIP) from the binary uXAML file
 * \fls0\edge.uxaml — one slow device file read at startup, then everything
 * is memory-resident. The "web engine" is a deterministic mock: navigating
 * paints a generated page card; no network, no HTML parser, no Chakra.
 *
 * UI structure mirrors the reverse-engineered Microsoft.MicrosoftEdge
 * 38.14393 package (see edge/analysis/EDGE_UI_MAP.txt); automation names
 * reuse the real ones (TabsButton, AddressEditBox, MoreButton, Hub pivots,
 * ActionsMenu items, TabCenter...).
 */
#include "nt_app.h"
#include "uxaml.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

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

/* ---- drawing (GDI FillRect backend, brush cache like uixrender) ---- */
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
    fill(dc, l + rad, t, r - rad, b, c);   /* vertical core */
    fill(dc, l, t + rad, l + rad, b - rad, c);
    fill(dc, r - rad, t + rad, r, b - rad, c);
    for (int i = 0; i < rad; ++i) {
        /* quarter-disc edges: row i, dx from the circle of radius rad */
        int dx = isqrt_(rad * rad - (rad - i) * (rad - i));
        int xo = rad - dx;
        fill(dc, l + xo, t + i, l + rad, t + i + 1, c);
        fill(dc, r - rad, t + i, r - xo, t + i + 1, c);
        fill(dc, l + xo, b - i - 1, l + rad, b - i, c);
        fill(dc, r - rad, b - i - 1, r - xo, b - i, c);
    }
}

/* text with the baked 1-bpp fonts; returns pixel width */
static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[3];
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
    for (; *s; ++s) {
        int ch = (unsigned char)*s;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}

static void draw_text(void *dc, int x, int y, const char *s, U32 c, int h, int align)
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

/* ---- app state ---- */
#define MAX_TABS 8
#define MAX_URL 96
typedef struct {
    char title[40];
    char url[MAX_URL];
    unsigned hash;
} Tab;

static UxFile ux;
static int ux_loaded;
static Tab tabs[MAX_TABS];
static int tab_count = 1;
static int tab_cur;
static int mode; /* 0 browser 1 menu 2 tabs 3 hub 4 address */
static int hub_pivot; /* 0 fav 1 hist 2 reading 3 downloads */
static char addr_buf[MAX_URL];
static int addr_len;
static char status_text[48] = "NTOS";
static void *g_wnd;

/* ---- layout cache: computed rect per node index ---- */
typedef struct {
    short x, y, w, h;
} NRect;
static NRect nrect[UX_MAX_NODES];

static int node_visible(const UxNode *n)
{
    return ux_int(&ux, n, UXA_VISIBLE, 1) != 0;
}

/* dynamic text override by automation name (binding substitute) */
static const char *dyn_text(const char *name)
{
    if (!name)
        return 0;
    if (xeq(name, "TabsCount")) {
        static char b[4];
        b[0] = (char)('0' + tab_count);
        b[1] = 0;
        return b;
    }
    if (xeq(name, "AddressText")) {
        if (addr_len)
            return addr_buf;
        return tabs[tab_cur].url[0] ? tabs[tab_cur].url : "Search or enter address";
    }
    if (xeq(name, "TabTitle"))
        return tabs[tab_cur].title;
    if (xeq(name, "TabUrl"))
        return tabs[tab_cur].url;
    if (xeq(name, "TabCountLabel")) {
        static char b[24];
        char *p = b;
        const char *t = tab_count == 1 ? " open tab" : " open tabs";
        b[0] = (char)('0' + tab_count);
        p = b + 1;
        xcopy(p, t, xstrlen(t));
        p[xstrlen(t)] = 0;
        return b;
    }
    if (xeq(name, "HubTitle")) {
        static const char *names[4] = {"Favorites", "History", "Reading list", "Downloads"};
        return names[hub_pivot & 3];
    }
    if (xeq(name, "StatusText"))
        return status_text;
    return 0;
}

/* measure a text node (override aware) */
static void node_text(const UxNode *n, const char **out)
{
    const char *t = ux_str(&ux, n, UXA_TEXT, "");
    const char *d = dyn_text(ux_str(&ux, n, UXA_NAME, 0));
    *out = d ? d : t;
}

/* layout: relative coords, auto flow in stacks/lists */
static void layout_doc(const UxDoc *d, int bx, int by, int bw, int bh)
{
    if (!d || d->root == UX_NONE)
        return;
    /* depth-first with explicit stack (no recursion budget worries) */
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
        /* push children right-to-left so layout order is left-to-right */
        int flow = (n->kind == UX_STACKV || n->kind == UX_STACKH || n->kind == UX_LIST);
        int acc = 0;
        for (int ci = (int)n->child_count - 1; ci >= 0; --ci) {
            unsigned c = n->child[ci];
            const UxNode *cn = &ux.nodes[c];
            int cx = ux_int(&ux, cn, UXA_X, 0);
            int cy = ux_int(&ux, cn, UXA_Y, 0);
            int cw = ux_int(&ux, cn, UXA_W, 0);
            int ch = ux_int(&ux, cn, UXA_H, 0);
            if (n->kind == UX_STACKV) {
                cy = acc;
                acc += ch ? ch : 24;
                if (!cw)
                    cw = pr.w;
                cx = 0;
            } else if (n->kind == UX_STACKH) {
                cx = acc;
                acc += cw ? cw : 40;
                if (!ch)
                    ch = pr.h;
                cy = 0;
            } else if (n->kind == UX_LIST) {
                cy = acc;
                acc += ch ? ch : 44;
                if (!cw)
                    cw = pr.w;
                cx = 0;
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
            if (cn->child_count && sp < 60)
                stack[sp++] = c;
        }
        (void)flow;
    }
}

/* paint a node subtree (document order) */
static void paint_node(void *dc, unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    if (!node_visible(n))
        return;
    NRect r = nrect[ni];
    switch (n->kind) {
    case UX_RECT: {
        U32 c = ux_clr(&ux, n, UXA_BG, WHITE);
        int rad = ux_int(&ux, n, UXA_RADIUS, 0);
        if (rad)
            fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, c, rad);
        else
            fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, c);
        break;
    }
    case UX_ELLIPSE:
        fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, ux_clr(&ux, n, UXA_BG, WHITE));
        break;
    case UX_TEXT:
    case UX_BUTTON:
    case UX_GLYPH: {
        const char *t;
        node_text(n, &t);
        if (t && *t) {
            int size = ux_int(&ux, n, UXA_SIZE, 11);
            int align = ux_int(&ux, n, UXA_ALIGN, 0);
            U32 col = ux_clr(&ux, n, UXA_COLOR, INK);
            int tx = r.x, ty = r.y;
            if (align == 1)
                tx += r.w / 2;
            else if (align == 2)
                tx += r.w;
            draw_text(dc, tx, ty, t, col, size, align);
        }
        break;
    }
    case UX_TEXTBOX: {
        fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, WHITE, 4);
        const char *t = addr_len ? addr_buf : ux_str(&ux, n, UXA_TEXT, "");
        if (t)
            draw_text(dc, r.x + 6, r.y + (r.h - 12) / 2, t, addr_len ? INK : MUTED, 11, 0);
        if (addr_len && mode == 4)
            fill(dc, r.x + 6 + text_w(addr_buf, 11) + 1, r.y + 4,
                 r.x + 8 + text_w(addr_buf, 11), r.y + r.h - 4, EDGE_BLUE);
        break;
    }
    default:
        break;
    }
    for (uint8_t i = 0; i < n->child_count; ++i)
        paint_node(dc, n->child[i]);
}

/* hit test: last drawn match wins (topmost) */
static const UxNode *hit_test(const UxDoc *d, int x, int y)
{
    const UxNode *best = 0;
    if (!d || d->root == UX_NONE)
        return 0;
    for (uint16_t i = 0; i < ux.node_count; ++i) {
        const UxNode *n = &ux.nodes[i];
        if (n->kind != UX_BUTTON || !node_visible(n))
            continue;
        NRect r = nrect[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            const char *cmd = ux_str(&ux, n, UXA_COMMAND, 0);
            if (cmd)
                best = n;
        }
    }
    return best;
}

/* ---- mock page content (the "engine") ---- */
static void paint_page(void *dc)
{
    Tab *t = &tabs[tab_cur];
    int top = 24, bottom = 472;
    fill(dc, 0, top, 320, bottom, WHITE);
    if (!t->url[0]) {
        /* new tab page: top-sites grid */
        draw_text(dc, 160, top + 24, "New tab", INK, 16, 1);
        static const char *sites[6] = {"MSDN", "GitHub", "Wiki", "News", "Store", "Docs"};
        for (int i = 0; i < 6; ++i) {
            int gx = 24 + (i % 3) * 100, gy = top + 70 + (i / 3) * 110;
            fill_round(dc, gx, gy, gx + 72, gy + 72, SURFACE, 6);
            fill_round(dc, gx + 10, gy + 10, gx + 62, gy + 62, CARD, 4);
            draw_text(dc, gx + 36, gy + 76, sites[i], MUTED, 10, 1);
        }
        draw_text(dc, 160, bottom - 28, "Windows NTOS Edge", MUTED, 9, 1);
        return;
    }
    /* generated article page keyed by URL hash */
    draw_text(dc, 16, top + 16, t->title, INK, 14, 0);
    fill(dc, 16, top + 36, 304, top + 37, CARD);
    unsigned h = t->hash;
    const char *lorem[4] = {
        "The tiny device renders this page locally.",
        "No network stack is involved in this demo.",
        "uXAML drives the chrome; content is mocked.",
        "Edge on NTOS: wine-style reimplementation.",
    };
    for (int p = 0; p < 4; ++p) {
        int lines = 2 + (int)((h >> (unsigned)(p * 3)) & 3u);
        for (int l = 0; l < lines; ++l) {
            fill(dc, 16, top + 52 + p * 52 + l * 14, 16 + 240 - (int)((h >> (unsigned)(l * 5)) & 63u),
                 top + 60 + p * 52 + l * 14, CARD);
        }
        draw_text(dc, 16, top + 52 + p * 52 + lines * 14, lorem[p], MUTED, 9, 0);
    }
    fill_round(dc, 248, top + 12, 300, top + 64, SURFACE, 4);
    draw_text(dc, 274, top + 34, "e", EDGE_BLUE, 20, 1);
}

static void paint_tab_cards(void *dc, const UxDoc *d)
{
    const UxNode *tpl = ux_find_name(&ux, d, "TabCardTemplate");
    if (!tpl)
        return;
    unsigned ti = (unsigned)(tpl - ux.nodes);
    for (int i = 0; i < tab_count; ++i) {
        int y = 52 + i * 80;
        if (y > 440)
            break;
        /* card background */
        fill_round(dc, 16, y, 304, y + 72, WHITE, 4);
        fill_round(dc, 24, y + 8, 80, y + 64, i == tab_cur ? EDGE_BLUE_DARK : CARD, 2);
        draw_text(dc, 52, y + 30, "e", WHITE, 14, 1);
        draw_text(dc, 88, y + 12, tabs[i].title, INK, 11, 0);
        draw_text(dc, 88, y + 32, tabs[i].url[0] ? tabs[i].url : "about:start", MUTED, 9, 0);
        if (i == tab_cur)
            fill(dc, 88, y + 46, 128, y + 49, EDGE_BLUE);
        draw_text(dc, 288, y + 10, "X", MUTED, 12, 1);
        /* remember close-hit zones: reuse nrect of template close button */
        (void)ti;
    }
}

/* ---- SIP keyboard (address mode) ---- */
static const char *KB[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
static void paint_keyboard(void *dc, int y0)
{
    fill(dc, 0, y0, 320, 528, SURFACE);
    int roww[3] = {10, 9, 7};
    for (int r = 0; r < 3; ++r) {
        int n = roww[r];
        int kw = 28, gap = 4;
        int total = n * kw + (n - 1) * gap;
        int x = (320 - total) / 2;
        for (int i = 0; i < n; ++i) {
            char c2[2] = {KB[r][i], 0};
            fill_round(dc, x, y0 + 12 + r * 44, x + kw, y0 + 44 + r * 44, WHITE, 3);
            draw_text(dc, x + kw / 2, y0 + 24 + r * 44, c2, INK, 12, 1);
            x += kw + gap;
        }
    }
    int y = y0 + 12 + 3 * 44;
    fill_round(dc, 40, y, 280, y + 36, EDGE_BLUE, 3);
    draw_text(dc, 160, y + 12, addr_len ? addr_buf : " ", WHITE, 11, 1);
    fill_round(dc, 250, y, 310, y + 36, SURFACE, 3);
    draw_text(dc, 280, y + 12, "Go", INK, 11, 1);
}

static int keyboard_tap(int px, int py, int y0)
{
    int roww[3] = {10, 9, 7};
    for (int r = 0; r < 3; ++r) {
        if (py < y0 + 12 + r * 44 || py >= y0 + 44 + r * 44)
            continue;
        int n = roww[r];
        int kw = 28, gap = 4;
        int total = n * kw + (n - 1) * gap;
        int xs = (320 - total) / 2;
        for (int i = 0; i < n; ++i)
            if (px >= xs + i * (kw + gap) && px < xs + i * (kw + gap) + kw) {
                if (addr_len < MAX_URL - 2) {
                    addr_buf[addr_len++] = KB[r][i];
                    addr_buf[addr_len] = 0;
                }
                return 1;
            }
    }
    {
        int ky = y0 + 12 + 3 * 44;
        if (py >= ky && py < ky + 36) {
            if (px >= 250)
                return 2; /* go */
            if (addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = ' ';
                addr_buf[addr_len] = 0;
            }
            return 1;
        }
    }
    return 0;
}

/* ---- navigation ---- */
static void nav_newtab(void)
{
    if (tab_count >= MAX_TABS)
        return;
    Tab *t = &tabs[tab_count++];
    t->title[0] = 'N';
    xcopy(t->title, "New tab", 7);
    t->title[7] = 0;
    t->url[0] = 0;
    t->hash = 0;
    tab_cur = tab_count - 1;
}

static void nav_url(const char *url)
{
    Tab *t = &tabs[tab_cur];
    int n = xstrlen(url);
    if (n > MAX_URL - 1)
        n = MAX_URL - 1;
    xcopy(t->url, url, n);
    t->url[n] = 0;
    t->hash = xhash(url);
    /* title from host part: after "//" up to '/' */
    const char *hp = url;
    while (*hp && *hp != '/')
        ++hp;
    if (*hp)
        hp += 2;
    int i = 0;
    while (hp[i] && hp[i] != '/' && i < 38)
        ++i;
    xcopy(t->title, hp, i);
    t->title[i] = 0;
    if (!i)
        xcopy(t->title, "Page", 4), t->title[4] = 0;
}

/* ---- repaint ---- */
static void paint_all(void *dc)
{
    const UxDoc *d;
    if (mode == 1 && (d = ux_find_doc(&ux, "ActionsMenu"))) {
        const UxDoc *bg = ux_find_doc(&ux, "SmallScreenPage");
        if (bg) {
            layout_doc(bg, 0, 0, 320, 528);
            paint_page(dc);
            paint_node(dc, bg->root);
        }
        /* scrim above the sheet (no alpha on device: solid dim) */
        fill(dc, 0, 0, 320, 180, RGB_(15, 25, 38));
        layout_doc(d, 0, 0, 320, 528);
        paint_node(dc, d->root);
        return;
    }
    if (mode == 2 && (d = ux_find_doc(&ux, "TabCenterPage"))) {
        layout_doc(d, 0, 0, 320, 528);
        paint_node(dc, d->root);
        paint_tab_cards(dc, d);
        return;
    }
    if (mode == 3 && (d = ux_find_doc(&ux, "HubPanel"))) {
        layout_doc(d, 0, 0, 320, 528);
        paint_node(dc, d->root);
        return;
    }
    if (mode == 4 && (d = ux_find_doc(&ux, "AddressEditBoxPage"))) {
        layout_doc(d, 0, 0, 320, 528);
        paint_node(dc, d->root);
        paint_keyboard(dc, 124);
        return;
    }
    d = ux_find_doc(&ux, "SmallScreenPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root); /* full chrome incl. PageHost background */
    clip_set(0, 24, 320, 472); /* content stays inside the page viewport */
    paint_page(dc);
    clip_set(0, 0, 320, 528);
}

/* ---- command dispatch ---- */
static void do_command(const char *cmd)
{
    if (xeq(cmd, "tabs")) {
        mode = 2;
    } else if (xeq(cmd, "menu")) {
        mode = 1;
    } else if (xeq(cmd, "close")) {
        mode = 0;
    } else if (xeq(cmd, "address")) {
        mode = 4;
        addr_len = 0;
        addr_buf[0] = 0;
    } else if (xeq(cmd, "newtab")) {
        nav_newtab();
        mode = 0;
    } else if (xeq(cmd, "newinprivate")) {
        nav_newtab();
        mode = 0;
    } else if (xeq(cmd, "favorites")) {
        hub_pivot = 0;
        mode = 3;
    } else if (xeq(cmd, "history")) {
        hub_pivot = 1;
        mode = 3;
    } else if (xeq(cmd, "readinglist")) {
        hub_pivot = 2;
        mode = 3;
    } else if (xeq(cmd, "downloads")) {
        hub_pivot = 3;
        mode = 3;
    } else if (xeq(cmd, "pivot-favorites")) {
        hub_pivot = 0;
    } else if (xeq(cmd, "pivot-history")) {
        hub_pivot = 1;
    } else if (xeq(cmd, "pivot-reading")) {
        hub_pivot = 2;
    } else if (xeq(cmd, "pivot-downloads")) {
        hub_pivot = 3;
    } else if (xeq(cmd, "go")) {
        nav_url(addr_len ? addr_buf : "ntos.local/start");
        mode = 0;
    } else if (xeq(cmd, "open-favorite")) {
        nav_url("https://msdn.microsoft.com");
        mode = 0;
    } else if (xeq(cmd, "closetab")) {
        if (tab_count > 1) {
            for (int i = tab_cur; i + 1 < tab_count; ++i)
                tabs[i] = tabs[i + 1];
            --tab_count;
            if (tab_cur >= tab_count)
                tab_cur = tab_count - 1;
        }
    }
}

/* ---- window proc ---- */
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
    if (msg == WM_KEY_) {
        if (wp == 0x1b) { /* hardware back */
            if (mode != 0)
                mode = 0;
            else
                NT_CALL(PostQuitMessage, (0));
        } else if (mode == 4) {
            if (wp == 8) { /* backspace */
                if (addr_len)
                    addr_buf[--addr_len] = 0;
            } else if (wp == 13) {
                do_command("go");
            } else if (wp >= 'a' && wp <= 'z' && addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = (char)wp;
                addr_buf[addr_len] = 0;
            } else if (wp >= '0' && wp <= '9' && addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = (char)wp;
                addr_buf[addr_len] = 0;
            } else if (wp == 190 /* . */ && addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = '.';
                addr_buf[addr_len] = 0;
            }
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        if (mode == 4) {
            if (y < 124) {
                const UxDoc *d = ux_find_doc(&ux, "AddressEditBoxPage");
                const UxNode *b = hit_test(d, x, y);
                if (b)
                    do_command(ux_str(&ux, b, UXA_COMMAND, ""));
                else
                    mode = 0;
            } else {
                int r = keyboard_tap(x, y, 124);
                if (r == 2)
                    do_command("go");
            }
        } else {
            const char *docn = mode == 1 ? "ActionsMenu" : mode == 2 ? "TabCenterPage"
                              : mode == 3 ? "HubPanel" : "SmallScreenPage";
            const UxDoc *d = ux_find_doc(&ux, docn);
            const UxNode *b = hit_test(d, x, y);
            if (mode == 2) {
                /* tab card hot zones (cards painted procedurally) */
                int idx = (y - 52) / 80;
                if (x >= 276 && y >= 52 && idx >= 0 && idx < tab_count) {
                    tab_cur = idx;
                    do_command("closetab");
                } else if (x >= 16 && x < 304 && y >= 52 && idx >= 0 && idx < tab_count) {
                    tab_cur = idx;
                    mode = 0;
                } else if (b) {
                    do_command(ux_str(&ux, b, UXA_COMMAND, ""));
                }
            } else if (b) {
                do_command(ux_str(&ux, b, UXA_COMMAND, ""));
            } else if (mode == 1) {
                mode = 0; /* scrim tap dismisses */
            }
        }
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
    ux_loaded = load_markup() == 0;
    if (!ux_loaded)
        return 0x80070002u; /* file missing: ERROR_FILE_NOT_FOUND */
    nav_newtab(); /* start with one New tab */
    tab_count = 1;
    tab_cur = 0;
    tabs[0].title[0] = 0;
    tabs[0].url[0] = 0;
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
