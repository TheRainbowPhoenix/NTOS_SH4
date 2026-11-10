/* edge_app.c — "Edge" mobile POC for NTOS, driven by uXAML markup (v2 dark).
 *
 * Reconstructs the Windows 10 Mobile Edge chrome (SmallScreenPage with the
 * bottom address bar [tabs|reload|url|reader|...], ActionsMenu sheet,
 * TabCenter 2-column grid + right-padded toolbar, HubPanel pivots,
 * Settings + Clear browsing data, Add to reading list / favorites dialogs,
 * AddressEditBox + dark SIP) from the binary uXAML file \fls0\edge.uxaml
 * loaded once at startup. Exact en-US strings from the real package
 * (edge/analysis/EDGE_UI_STRINGS.md); control names mirror the reverse-
 * engineered automation ids (TabsButton, AddressEditBox, MoreButton,
 * TabsCenter_NewTabButton, AddReadingListFlyout*...).
 *
 * The "web engine" is a deterministic mock: a wikipedia-like article for
 * wikipedia hosts, a generic page otherwise, dark NTP with top sites.
 * No network, no HTML parser, no Chakra (pure stub DLL).
 */
#include "nt_app.h"
#include "uxaml.h"
#include "icons.h"

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

/* ---- dark theme palette (Edge 1607 mobile) ---- */
#define C_BG     RGB_(0x00, 0x00, 0x00) /* app background            */
#define C_SHEET  RGB_(0x2B, 0x2B, 0x2B) /* menus, cards, flyouts     */
#define C_CARD   RGB_(0x33, 0x33, 0x33) /* tiles, keyboard keys      */
#define C_SEL    RGB_(0x3A, 0x3A, 0x3A) /* selected tab card         */
#define C_FIELD  RGB_(0x1F, 0x1F, 0x1F) /* input fields              */
#define C_SEP    RGB_(0x46, 0x46, 0x46) /* separators                */
#define C_INK    RGB_(0xFF, 0xFF, 0xFF)
#define C_MUT    RGB_(0x9A, 0x9A, 0x9A)
#define C_DIM    RGB_(0x6E, 0x6E, 0x6E)
#define C_BLUE   RGB_(0x00, 0x78, 0xD7)
#define C_SCRIM  RGB_(0x14, 0x14, 0x14)
#define C_PAGEBG RGB_(0xFF, 0xFF, 0xFF) /* mock web page background  */

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
    for (; *s; ++s) {
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

/* suggestion + title sources (subset of the real NTP/top-sites set) */
static const Site SITES[6] = {
    {"en.wikipedia.org", "Wikipedia", RGB_(0x33, 0x33, 0x33)},
    {"github.com", "GitHub", RGB_(0x24, 0x29, 0x2E)},
    {"msdn.microsoft.com", "MSDN", RGB_(0x00, 0x72, 0xC6)},
    {"news.ycombinator.com", "Hacker News", RGB_(0xFF, 0x66, 0x00)},
    {"xania.fr", "Xania", RGB_(0x8B, 0x00, 0x00)},
    {"bing.com", "Bing", RGB_(0x00, 0x83, 0x73)},
};

static const struct { const char *label; U32 color; } NTP_TILES[8] = {
    {"Wikipedia", RGB_(0x33, 0x33, 0x33)},
    {"GitHub", RGB_(0x24, 0x29, 0x2E)},
    {"YouTube", RGB_(0xFF, 0x00, 0x00)},
    {"MSN", RGB_(0x00, 0x78, 0xD7)},
    {"Bing", RGB_(0x00, 0x83, 0x73)},
    {"Outlook", RGB_(0x00, 0x72, 0xC6)},
    {"Amazon", RGB_(0xFF, 0x99, 0x00)},
    {"Reddit", RGB_(0xFF, 0x45, 0x00)},
};

typedef struct { const char *title, *host; } Item;
static Item favorites[12] = {
    {"Wikipedia", "en.wikipedia.org"},
    {"GitHub", "github.com"},
    {"Hacker News", "news.ycombinator.com"},
    {"MSN", "msn.com"},
    {"Xania", "xania.fr"},
};
static int fav_count = 5;
static Item reading_list[12] = {
    {"Stay on top of your reading", "msn.com"},
    {"Windows 10 Mobile: the best bits", "theverge.com"},
    {"Build 1607: what's new", "blogs.windows.com"},
};
static int rl_count = 3;
static Item hist_today[8] = {
    {"GitHub", "github.com"},
    {"Xania", "xania.fr"},
    {"Bing", "bing.com"},
};
static int hist_count = 3;
static const Item HIST_OLD[3] = {
    {"MSN", "msn.com"},
    {"BBC News", "bbc.com"},
    {"The Verge", "theverge.com"},
};
static const struct { const char *name, *status; int prog; } DOWNLOADS[3] = {
    {"EdgeUserGuide.pdf", "267 KB - Complete", 100},
    {"build1607.iso", "1.2 GB of 4.5 GB - Paused", 27},
    {"wallpaper.png", "1.1 MB - Complete", 100},
};

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
static int loading_ttl;
static char toast_text[80];
static void *g_wnd;

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
    if (xeq(name, "TabsCount")) {
        static char b[4];
        b[0] = (char)('0' + tab_count);
        b[1] = 0;
        return b;
    }
    if (xeq(name, "HubTitle")) {
        static const char *names[4] = {"Favorites", "Reading list", "History",
                                       "Downloads"};
        return names[hub_pivot & 3];
    }
    if (xeq(name, "RLName") || xeq(name, "FavName")) {
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
        return tabs[tab_cur].url[0] ? C_INK : C_DIM;
    if (xeq(name, "GoForwardText") || xeq(name, "GoForwardIcon"))
        return C_MUT; /* disabled: no forward stack in the POC */
    if (name[0] == 'P' && name[1] == 'i' && name[2] == 'v' && name[3] == 'o' &&
        name[4] == 't' && name[5] >= '0' && name[5] <= '3')
        return (hub_pivot == name[5] - '0') ? C_INK : C_MUT;
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
        /* pass 1: assign child rects in document order so stack/list flow
         * (auto-Y / auto-X) runs top-down / left-right */
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
        /* pass 2: descend right-to-left so the stack pops left-to-right */
        for (int ci = (int)n->child_count - 1; ci >= 0; --ci) {
            unsigned c = n->child[ci];
            if (ux.nodes[c].child_count && sp < 60)
                stack[sp++] = c;
        }
    }
}

/* ---- node painting ---- */
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
        int on = ux_int(&ux, n, UXA_CHECKED, 0) || (nstate[ni] & NS_TOGGLED);
        int bx = r.x + 16, by = r.y + 12;
        if (on) {
            fill_round(dc, bx, by, bx + 20, by + 20, C_BLUE, 3);
            ic_draw(ifill, dc, bx + 2, by + 2, 16, C_INK, IC_CHECK);
        } else {
            ic_rect(ifill, dc, bx, by, 20, 20, 1, C_MUT);
        }
        const char *t;
        node_text(n, &t);
        draw_text(dc, r.x + 52, r.y + 14, t, C_INK, 12, 0, 0);
        break;
    }
    case UX_TEXTBOX: {
        U32 bg = ux_clr(&ux, n, UXA_BG, C_FIELD);
        int rad = ux_int(&ux, n, UXA_RADIUS, 2);
        fill_round(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg, rad);
        if (mode == M_ADDRESS && xeq(name, "AddressInput")) {
            if (addr_len) {
                draw_text(dc, r.x + 8, r.y + (r.h - 12) / 2, addr_buf, C_INK, 12, 0, 0);
                int cx = r.x + 8 + text_w(addr_buf, 12) + 2;
                if (cx < r.x + r.w - 6)
                    fill(dc, cx, r.y + 6, cx + 2, r.y + r.h - 6, C_BLUE);
            } else {
                const char *t;
                node_text(n, &t);
                draw_text(dc, r.x + 8, r.y + (r.h - 12) / 2, t, C_MUT, 12, 0, 0);
                fill(dc, r.x + 8, r.y + 6, r.x + 10, r.y + r.h - 6, C_BLUE);
            }
        } else {
            const char *t;
            node_text(n, &t);
            int live = t && *t && (xeq(name, "RLName") || xeq(name, "FavName"));
            if (t && *t)
                draw_text(dc, r.x + 8, r.y + (r.h - 12) / 2, t,
                          live ? C_INK : C_MUT, 12, 0, 0);
            if (live) {
                int cx = r.x + 8 + text_w(t, 12) + 2;
                if (cx < r.x + r.w - 6)
                    fill(dc, cx, r.y + 6, cx + 2, r.y + r.h - 6, C_BLUE);
            }
        }
        break;
    }
    default:
        break;
    }
    for (uint8_t i = 0; i < n->child_count; ++i)
        paint_node(dc, n->child[i]);
}

/* ---- mock web content (the "engine") ---- */
static void paint_ntp(void *dc)
{
    fill(dc, 0, 0, 320, 480, C_BG);
    /* top sites: 4 x 2 tiles */
    for (int i = 0; i < 8; ++i) {
        int gx = 8 + (i % 4) * 78, gy = 24 + (i / 4) * 104;
        fill_round(dc, gx, gy, gx + 64, gy + 64, C_SHEET, 8);
        const char *lab = NTP_TILES[i].label;
        char c2[2] = {lab[0], 0};
        draw_text(dc, gx + 32, gy + 20, c2, C_INK, 22, 1, 1);
        draw_text(dc, gx + 32, gy + 72, lab, C_MUT, 9, 1, 0);
    }
    /* news cards */
    static const char *NEWS[2][2] = {
        {"Windows 10 Mobile gets a faster Edge", "MSN News"},
        {"Best budget phones of 2016", "MSN News"},
    };
    for (int i = 0; i < 2; ++i) {
        int gy = 248 + i * 96;
        fill_round(dc, 8, gy, 312, gy + 84, RGB_(0x1F, 0x1F, 0x1F), 4);
        draw_text_clamp(dc, 24, gy + 14, NEWS[i][0], C_INK, 12, 190);
        draw_text(dc, 24, gy + 38, "Tap to read the full story on your", C_MUT, 9, 0, 0);
        draw_text(dc, 24, gy + 52, "device, even without a connection.", C_MUT, 9, 0, 0);
        draw_text(dc, 24, gy + 66, NEWS[i][1], C_DIM, 9, 0, 0);
        fill_round(dc, 240, gy + 12, 300, gy + 72, C_CARD, 4);
        draw_text(dc, 270, gy + 36, "i", C_MUT, 22, 1, 1);
    }
}

static void paint_bars_paragraph(void *dc, int y, const int *widths, int lines)
{
    for (int i = 0; i < lines; ++i)
        fill(dc, 16, y + i * 16, 16 + widths[i], y + i * 16 + 8,
             RGB_(0xEA, 0xEC, 0xF0));
}

static void paint_page(void *dc)
{
    Tab *t = &tabs[tab_cur];
    if (!t->url[0]) {
        paint_ntp(dc);
        return;
    }
    int wiki = 0;
    for (const char *p = t->host; *p; ++p)
        if (p[0] == 'w' && p[1] == 'i' && p[2] == 'k' && p[3] == 'i')
            wiki = 1;
    fill(dc, 0, 0, 320, 480, C_PAGEBG);
    if (wiki) {
        draw_text(dc, 16, 16, "Wikipedia", RGB_(0x00, 0x00, 0x00), 18, 0, 1);
        draw_text(dc, 16, 44, "From Wikipedia, the free encyclopedia",
                  RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
        fill(dc, 16, 60, 304, 61, RGB_(0xA2, 0xA9, 0xB1));
        static const int p1[4] = {288, 260, 274, 180};
        paint_bars_paragraph(dc, 74, p1, 4);
        /* table of contents */
        ic_rect(ifill, dc, 16, 150, 288, 104, 1, RGB_(0xA2, 0xA9, 0xB1));
        draw_text(dc, 28, 162, "Contents", RGB_(0x00, 0x00, 0x00), 12, 0, 1);
        static const char *TOC[4] = {"1 History", "2 Features", "3 See also",
                                     "4 References"};
        for (int i = 0; i < 4; ++i)
            draw_text(dc, 28, 184 + i * 17, TOC[i], RGB_(0x33, 0x66, 0xCC), 11, 0, 0);
        static const int p2[5] = {288, 200, 288, 274, 132};
        paint_bars_paragraph(dc, 270, p2, 5);
        /* an inline blue link inside the third line */
        fill(dc, 16, 302, 70, 310, RGB_(0x33, 0x66, 0xCC));
        draw_text(dc, 16, 386, "This page is rendered locally by the NTOS",
                  RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
        draw_text(dc, 16, 400, "mock engine. No network stack involved.",
                  RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
        fill_round(dc, 248, 20, 304, 76, RGB_(0xEA, 0xEC, 0xF0), 4);
        draw_text(dc, 276, 34, "W", RGB_(0x33, 0x33, 0x33), 22, 1, 1);
    } else {
        fill(dc, 0, 0, 320, 52, RGB_(0xF5, 0xF6, 0xF7));
        draw_text(dc, 16, 18, t->title, RGB_(0x00, 0x00, 0x00), 14, 0, 1);
        draw_text(dc, 16, 68, t->host, RGB_(0x33, 0x66, 0xCC), 12, 0, 0);
        static const int p1[4] = {288, 260, 274, 180};
        paint_bars_paragraph(dc, 92, p1, 4);
        static const int p2[4] = {288, 274, 252, 210};
        paint_bars_paragraph(dc, 170, p2, 4);
        draw_text(dc, 16, 250, "This page is rendered locally by the NTOS",
                  RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
        draw_text(dc, 16, 264, "mock engine. No network stack involved.",
                  RGB_(0x54, 0x59, 0x5D), 9, 0, 0);
    }
}

/* two-tone URL: subdomain greyed, domain white (AddressBar*TextBrush) */
static void paint_address_url(void *dc, const UxDoc *d)
{
    Tab *t = &tabs[tab_cur];
    if (!t->url[0])
        return;
    int ti = find_node("AddressText");
    if (ti < 0)
        return;
    (void)d;
    NRect r = nrect[ti];
    fill(dc, r.x - 2, r.y - 2, r.x + 130, r.y + 16, C_BG); /* erase markup text */
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
        draw_text_clamp(dc, r.x + text_w(pre, 12), r.y, dot + 1, C_INK, 12, 64);
    } else {
        draw_text_clamp(dc, r.x, r.y, h, C_INK, 12, 126);
    }
}

/* ---- tab center cards (2-column grid) ---- */
static void paint_tab_cards(void *dc)
{
    for (int i = 0; i < tab_count; ++i) {
        int col = i % 2, row = i / 2;
        int cx = 8 + col * 156, cy = 56 + row * 108;
        if (cy > 420)
            break;
        Tab *t = &tabs[i];
        int sel = (i == tab_cur);
        fill_round(dc, cx, cy, cx + 148, cy + 100, sel ? C_SEL : C_SHEET, 4);
        if (sel)
            ic_rect(ifill, dc, cx + 1, cy + 1, 146, 98, 2, C_BLUE);
        /* favicon plate */
        U32 pc = t->priv ? C_BLUE : (t->url[0] ? RGB_(0x33, 0x33, 0x33) : C_BLUE);
        fill_round(dc, cx + 10, cy + 10, cx + 40, cy + 40, pc, 6);
        if (t->priv)
            ic_draw(ifill, dc, cx + 14, cy + 14, 16, C_INK, IC_INPRIVATE);
        else if (t->url[0]) {
            char c2[2] = {t->title[0] ? t->title[0] : 'e', 0};
            draw_text(dc, cx + 25, cy + 14, c2, C_INK, 18, 1, 1);
        } else
            draw_text(dc, cx + 25, cy + 13, "e", C_INK, 22, 1, 1);
        draw_text_clamp(dc, cx + 10, cy + 50, t->title, C_INK, 11, 128);
        draw_text_clamp(dc, cx + 10, cy + 68, t->priv ? "InPrivate"
                    : (t->url[0] ? t->host : "New tab"), C_MUT, 9, 128);
        ic_draw(ifill, dc, cx + 122, cy + 6, 16, C_MUT, IC_CLOSE);
    }
}

/* ---- hub content (pivot panes) ---- */
static void paint_hub_rows(void *dc, const Item *items, int count, int icon)
{
    int y = 104;
    for (int i = 0; i < count && y < 500; ++i, y += 52) {
        ic_draw(ifill, dc, 14, y + 16, 18, C_MUT, icon);
        draw_text_clamp(dc, 44, y + 14, items[i].title, C_INK, 12, 240);
        draw_text_clamp(dc, 44, y + 32, items[i].host, C_MUT, 9, 240);
    }
}

static void paint_hub_content(void *dc)
{
    if (hub_pivot == 0) {
        int y = 104;
        for (int i = 0; i < fav_count && y < 500; ++i, y += 52) {
            U32 pc = C_SHEET;
            for (unsigned s = 0; s < 6; ++s)
                if (xeq(favorites[i].host, SITES[s].host))
                    pc = SITES[s].color;
            fill_round(dc, 10, y + 11, 38, y + 39, pc, 6);
            char c2[2] = {favorites[i].title[0], 0};
            draw_text(dc, 24, y + 16, c2, C_INK, 18, 1, 1);
            draw_text_clamp(dc, 50, y + 14, favorites[i].title, C_INK, 12, 236);
            draw_text_clamp(dc, 50, y + 32, favorites[i].host, C_MUT, 9, 236);
        }
    } else if (hub_pivot == 1) {
        paint_hub_rows(dc, reading_list, rl_count, IC_BOOK);
    } else if (hub_pivot == 2) {
        draw_text(dc, 16, 110, "Today", C_DIM, 9, 0, 1);
        int y = 126;
        for (int i = 0; i < hist_count && y < 300; ++i, y += 44) {
            ic_draw(ifill, dc, 14, y + 12, 18, C_MUT, IC_CLOCK);
            draw_text_clamp(dc, 44, y + 11, hist_today[i].title, C_INK, 12, 240);
            draw_text_clamp(dc, 44, y + 27, hist_today[i].host, C_MUT, 9, 240);
        }
        draw_text(dc, 16, y + 14, "Last month", C_DIM, 9, 0, 1);
        y += 30;
        for (int i = 0; i < 3 && y < 500; ++i, y += 44) {
            ic_draw(ifill, dc, 14, y + 12, 18, C_MUT, IC_CLOCK);
            draw_text_clamp(dc, 44, y + 11, HIST_OLD[i].title, C_INK, 12, 240);
            draw_text_clamp(dc, 44, y + 27, HIST_OLD[i].host, C_MUT, 9, 240);
        }
    } else {
        int y = 104;
        for (int i = 0; i < 3 && y < 500; ++i, y += 60) {
            ic_draw(ifill, dc, 14, y + 16, 18, C_MUT, IC_DOWNLOAD);
            draw_text_clamp(dc, 44, y + 12, DOWNLOADS[i].name, C_INK, 12, 240);
            draw_text_clamp(dc, 44, y + 30, DOWNLOADS[i].status, C_MUT, 9, 240);
            if (DOWNLOADS[i].prog < 100) {
                fill(dc, 44, y + 44, 260, y + 47, C_SHEET);
                fill(dc, 44, y + 44, 44 + 216 * DOWNLOADS[i].prog / 100, y + 47,
                     C_BLUE);
            }
        }
    }
}

/* ---- address edit: suggestions + SIP ---- */
static char sug_search_buf[64];
static const char *sug_text[8];
static unsigned char sug_is_search[8];
static int sug_count;

static int host_prefix(const char *host, const char *pre)
{
    int i = 0;
    while (pre[i]) {
        if (!host[i] || host[i] != pre[i])
            return 0;
        ++i;
    }
    return 1;
}

static void build_suggestions(void)
{
    sug_count = 0;
    if (addr_len) {
        /* "Search for <typed>" row first */
        char *p = sug_search_buf;
        const char *a = "Search for ";
        while (*a)
            *p++ = *a++;
        for (int i = 0; i < addr_len && p < sug_search_buf + 60; ++i)
            *p++ = addr_buf[i];
        *p = 0;
        sug_text[sug_count] = sug_search_buf;
        sug_is_search[sug_count] = 1;
        ++sug_count;
    }
    for (int i = 0; i < 6 && sug_count < 7; ++i) {
        if (!addr_len || host_prefix(SITES[i].host, addr_buf)) {
            sug_text[sug_count] = SITES[i].host;
            sug_is_search[sug_count] = 0;
            ++sug_count;
        }
    }
}

static void paint_suggestions(void *dc)
{
    for (int i = 0; i < sug_count; ++i) {
        int y = 56 + i * 44;
        if (y + 44 > 340)
            break;
        ic_draw(ifill, dc, 14, y + 13, 18, C_MUT,
                sug_is_search[i] ? IC_FIND : IC_CLOCK);
        draw_text_clamp(dc, 44, y + 16, sug_text[i], C_INK, 12, 260);
    }
}

static const char *KROW[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};

static void paint_keyboard(void *dc)
{
    int y0 = 340;
    fill(dc, 0, y0, 320, 528, C_BG);
    static const int rowx[3] = {2, 18, 14};
    for (int r = 0; r < 3; ++r) {
        int ky = 352 + r * 36;
        for (int i = 0; KROW[r][i]; ++i) {
            int kx = rowx[r] + i * 32;
            fill_round(dc, kx, ky, kx + 28, ky + 32, C_CARD, 4);
            char c2[2] = {KROW[r][i], 0};
            draw_text(dc, kx + 14, ky + 10, c2, C_INK, 12, 1, 0);
        }
    }
    /* backspace */
    fill_round(dc, 258, 424, 306, 456, C_CARD, 4);
    ic_draw(ifill, dc, 272, 432, 16, C_INK, IC_BACK);
    /* row 4: . / - [space] enter */
    fill_round(dc, 6, 460, 34, 492, C_CARD, 4);
    draw_text(dc, 20, 470, ".", C_INK, 12, 1, 0);
    fill_round(dc, 38, 460, 66, 492, C_CARD, 4);
    draw_text(dc, 52, 470, "/", C_INK, 12, 1, 0);
    fill_round(dc, 70, 460, 98, 492, C_CARD, 4);
    draw_text(dc, 84, 470, "-", C_INK, 12, 1, 0);
    fill_round(dc, 102, 460, 222, 492, C_CARD, 4);
    fill_round(dc, 226, 460, 314, 492, C_BLUE, 4);
    ic_draw(ifill, dc, 262, 468, 16, C_INK, IC_FORWARD);
}

static int keyboard_tap(int px, int py)
{
    static const int rowx[3] = {2, 18, 14};
    if (py >= 352 && py < 456) {
        int r = (py - 352) / 36;
        if (r > 2)
            r = 2;
        for (int i = 0; KROW[r][i]; ++i) {
            int kx = rowx[r] + i * 32;
            if (px >= kx && px < kx + 28) {
                if (addr_len < MAX_URL - 2) {
                    addr_buf[addr_len++] = KROW[r][i];
                    addr_buf[addr_len] = 0;
                }
                return 1;
            }
        }
        if (r == 2 && px >= 258 && px < 306) { /* backspace */
            if (addr_len)
                addr_buf[--addr_len] = 0;
            return 1;
        }
        return 0;
    }
    if (py >= 460 && py < 492) {
        if (px >= 6 && px < 34) {
            if (addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = '.';
                addr_buf[addr_len] = 0;
            }
            return 1;
        }
        if (px >= 38 && px < 66) {
            if (addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = '/';
                addr_buf[addr_len] = 0;
            }
            return 1;
        }
        if (px >= 70 && px < 98) {
            if (addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = '-';
                addr_buf[addr_len] = 0;
            }
            return 1;
        }
        if (px >= 102 && px < 222) {
            if (addr_len < MAX_URL - 2) {
                addr_buf[addr_len++] = ' ';
                addr_buf[addr_len] = 0;
            }
            return 1;
        }
        if (px >= 226 && px < 314)
            return 2; /* enter */
    }
    return 0;
}

/* ---- navigation ---- */
static void set_lock_flag(void)
{
    flag_named("AddressLock", NS_SHOWN, tabs[tab_cur].url[0] != 0);
}

static void hist_add(const char *host, const char *title)
{
    if (hist_count >= 8)
        hist_count = 8 - 1;
    for (int i = hist_count; i > 0; --i)
        hist_today[i] = hist_today[i - 1];
    hist_today[0].host = host;
    hist_today[0].title = title;
    ++hist_count;
}

static void nav_url(const char *url)
{
    Tab *t = &tabs[tab_cur];
    /* strip scheme */
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
    /* host = up to first '/' */
    int hl = 0;
    while (p[hl] && p[hl] != '/' && hl < 39)
        ++hl;
    xcopy(t->host, p, hl);
    t->host[hl] = 0;
    t->hash = xhash(t->url);
    /* title from the site table */
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
    loading_ttl = 8;
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
        /* Edge behaviour: closing the last tab yields a fresh New tab */
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
    paint_address_url(dc, d);
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
        fill(dc, 0, 0, 320, 136, C_SCRIM); /* sheet covers the rest */
        if ((d = ux_find_doc(&ux, "ActionsMenu"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_TABS || mode == M_TABSMENU) {
        if ((d = ux_find_doc(&ux, "TabCenterPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            paint_tab_cards(dc);
        }
        if (mode == M_TABSMENU && (d = ux_find_doc(&ux, "TabsContextMenu"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_HUB) {
        if ((d = ux_find_doc(&ux, "HubPanel"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* selected pivot underline */
            fill(dc, hub_pivot * 80, 98, hub_pivot * 80 + 80, 101, C_BLUE);
            paint_hub_content(dc);
        }
    } else if (mode == M_ADDRESS) {
        if ((d = ux_find_doc(&ux, "AddressEditBoxPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            build_suggestions();
            paint_suggestions(dc);
            paint_keyboard(dc);
        }
    } else if (mode == M_SETTINGS) {
        if ((d = ux_find_doc(&ux, "SettingsPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
            /* hairline separators + sync toggle (row 0) */
            for (int i = 1; i <= 6; ++i)
                fill(dc, 16, 56 + i * 48, 304, 57 + i * 48, RGB_(0x1F, 0x1F, 0x1F));
            fill_round(dc, 252, 70, 296, 90, C_BLUE, 10);
            ic_dot(ifill, dc, 286, 80, 8, C_INK);
        }
    } else if (mode == M_CLEAR) {
        if ((d = ux_find_doc(&ux, "ClearBrowsingDataPage"))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    } else if (mode == M_DLG_READLIST || mode == M_DLG_FAV) {
        paint_browser(dc);
        fill(dc, 0, 0, 320, 528, C_SCRIM);
        const char *doc = mode == M_DLG_READLIST ? "AddToReadingListDialog"
                                                 : "AddToFavoritesDialog";
        if ((d = ux_find_doc(&ux, doc))) {
            layout_doc(d, 0, 0, 320, 528);
            paint_node(dc, d->root);
        }
    }
    /* loading strip over content */
    if (loading_ttl > 0 && (mode == M_BROWSER || mode == M_FIND)) {
        fill(dc, 0, 0, 320, 3, C_BLUE);
        --loading_ttl;
    }
    /* notification bar (toast) sits above the address bar */
    if (toast_text[0]) {
        fill(dc, 0, 428, 320, 480, RGB_(0x1F, 0x1F, 0x1F));
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
        if ((n->kind != UX_BUTTON && n->kind != UX_CHECK) || !node_visible_idx(i))
            continue;
        NRect r = nrect[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            if (n->kind == UX_CHECK) {
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
    } else if (xeq(cmd, "reload")) {
        if (tabs[tab_cur].url[0])
            loading_ttl = 8;
    } else if (xeq(cmd, "reader"))
        toast("This page can't be shown in reading view");
    else if (xeq(cmd, "fwd"))
        ; /* disabled in the POC: no forward stack */
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
        if (hub_pivot == 2)
            hist_count = 0;
        toast("All clear!");
    } else if (xeq(cmd, "newtab"))
        tab_new(0);
    else if (xeq(cmd, "newprivate"))
        tab_new(1);
    else if (xeq(cmd, "tabsmenu"))
        mode = M_TABSMENU;
    else if (xeq(cmd, "ctx-refresh-all")) {
        loading_ttl = 8;
        mode = M_TABS;
    } else if (xeq(cmd, "ctx-duplicate")) {
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
    else if (xeq(cmd, "doclear"))
        toast("All clear!");
    else if (xeq(cmd, "rl-add")) {
        if (rl_count < 12) {
            reading_list[rl_count].title = tabs[tab_cur].title[0]
                                               ? tabs[tab_cur].title
                                               : "New tab";
            reading_list[rl_count].host = tabs[tab_cur].host[0]
                                              ? tabs[tab_cur].host
                                              : "about:start";
            ++rl_count;
        }
        toast("Added to reading list");
        mode = M_BROWSER;
    } else if (xeq(cmd, "fav-add")) {
        if (fav_count < 12) {
            favorites[fav_count].title = tabs[tab_cur].title[0]
                                             ? tabs[tab_cur].title
                                             : "New tab";
            favorites[fav_count].host = tabs[tab_cur].host[0]
                                            ? tabs[tab_cur].host
                                            : "about:start";
            ++fav_count;
        }
        toast("Added to favorites");
        mode = M_BROWSER;
    } else if (xeq(cmd, "link-cloud") || xeq(cmd, "link-bing") ||
               xeq(cmd, "advanced") || xeq(cmd, "feedback") ||
               xeq(cmd, "helpabout") || xeq(cmd, "fav-folder"))
        toast("Not available in this POC");
    /* "noop" and unknown commands: silently ignored */
}

/* ---- window proc ---- */
static void process_tap(int x, int y)
{
    const UxDoc *d;
    const UxNode *b;
    if (mode == M_ADDRESS) {
        if (y < 56) {
            if ((d = ux_find_doc(&ux, "AddressEditBoxPage")) &&
                (b = hit_test(d, x, y)))
                do_command(ux_str(&ux, b, UXA_COMMAND, ""));
            else
                mode = M_BROWSER;
        } else if (y < 340) {
            build_suggestions();
            int idx = (y - 56) / 44;
            if (idx >= 0 && idx < sug_count) {
                if (sug_is_search[idx])
                    nav_url(addr_len ? addr_buf : "bing.com");
                else
                    nav_url(sug_text[idx]);
                mode = M_BROWSER;
            }
        } else {
            int r = keyboard_tap(x, y);
            if (r == 2) {
                if (addr_len)
                    nav_url(addr_buf);
                mode = M_BROWSER;
            }
        }
        return;
    }
    if (mode == M_TABS) {
        /* tab card grid hot zones (cards painted procedurally) */
        if (x >= 8 && x < 312 && y >= 56 && y < 476) {
            int col = x >= 164 ? 1 : 0;
            int row = (y - 56) / 108;
            int idx = row * 2 + col;
            int cx = 8 + col * 156, cy = 56 + row * 108;
            if (idx < tab_count) {
                if (x >= cx + 118 && y < cy + 28) {
                    tab_close_idx(idx); /* close X */
                } else {
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
    const char *docn = mode == M_MENU ? "ActionsMenu"
                    : mode == M_HUB ? "HubPanel"
                    : mode == M_SETTINGS ? "SettingsPage"
                    : mode == M_CLEAR ? "ClearBrowsingDataPage"
                    : mode == M_DLG_READLIST ? "AddToReadingListDialog"
                    : mode == M_DLG_FAV ? "AddToFavoritesDialog"
                    : mode == M_FIND ? "FindOnPageBar"
                    : "SmallScreenPage";
    if ((d = ux_find_doc(&ux, docn)) && (b = hit_test(d, x, y))) {
        if (b->kind == UX_CHECK)
            return; /* toggled in hit_test */
        const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
        if (cmd && *cmd)
            do_command(cmd);
        else if (mode == M_MENU || mode == M_DLG_READLIST || mode == M_DLG_FAV)
            mode = M_BROWSER; /* scrim dismiss */
    } else if (mode == M_MENU || mode == M_DLG_READLIST || mode == M_DLG_FAV) {
        mode = M_BROWSER; /* scrim tap dismisses */
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
    if (msg == WM_KEY_) {
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
            if (wp == 8) {
                if (addr_len)
                    addr_buf[--addr_len] = 0;
            } else if (wp == 13) {
                if (addr_len)
                    nav_url(addr_buf);
                mode = M_BROWSER;
            } else if (addr_len < MAX_URL - 2 &&
                       ((wp >= 'a' && wp <= 'z') || (wp >= '0' && wp <= '9') ||
                        wp == '.' || wp == '/' || wp == '-' || wp == ' ')) {
                addr_buf[addr_len++] = (char)wp;
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
