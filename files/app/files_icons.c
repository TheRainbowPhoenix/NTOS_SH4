/* files_icons.c — integer-only MDL2-style icons for the FileExplorer port.
 *
 * Every glyph is drawn in a virtual 100x100 box; fi_draw scales it to the
 * requested square via fixed-point math (k = size*64/100, coords scaled by
 * k then >> 6). Same stroke model as edge/app/icons.c — no TTF, no floats.
 */
#include "files_icons.h"

static int iabs_(int v) { return v < 0 ? -v : v; }

/* ---- raw strokes (host pixels) ---- */
void fi_line(IconFillFn f, void *dc, int x0, int y0, int x1, int y1,
             int t, unsigned c)
{
    int dx = iabs_(x1 - x0), dy = -iabs_(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    if (t < 1)
        t = 1;
    for (;;) {
        if (t <= 1)
            f(dc, x0, y0, x0 + 1, y0 + 1, c);
        else if (t == 2)
            f(dc, x0, y0, x0 + 2, y0 + 2, c);
        else
            f(dc, x0 - t / 2, y0 - t / 2, x0 + (t + 1) / 2, y0 + (t + 1) / 2, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fi_box(IconFillFn f, void *dc, int x, int y, int w, int h, unsigned c)
{
    if (w <= 0 || h <= 0)
        return;
    f(dc, x, y, x + w, y + h, c);
}

void fi_rect(IconFillFn f, void *dc, int x, int y, int w, int h,
             int thick, unsigned c)
{
    fi_line(f, dc, x, y, x + w - 1, y, thick, c);
    fi_line(f, dc, x, y + h - 1, x + w - 1, y + h - 1, thick, c);
    fi_line(f, dc, x, y, x, y + h - 1, thick, c);
    fi_line(f, dc, x + w - 1, y, x + w - 1, y + h - 1, thick, c);
}

void fi_circle(IconFillFn f, void *dc, int cx, int cy, int r,
               int thick, unsigned c)
{
    if (r < 1) {
        f(dc, cx, cy, cx + 1, cy + 1, c);
        return;
    }
    for (int dy = -r - thick; dy <= r + thick; ++dy) {
        int span = 0, runx0 = 0;
        for (int dx = 0; dx <= r + thick; ++dx) {
            int d2 = dx * dx + dy * dy;
            int in_lo = r - thick / 2 - 1, in_hi = r + (thick + 1) / 2;
            int inside = d2 <= in_hi * in_hi && d2 >= in_lo * in_lo;
            if (inside && !span) { span = 1; runx0 = dx; }
            else if (!inside && span) {
                span = 0;
                f(dc, cx - dx, cy + dy, cx - runx0 + 1, cy + dy + 1, c);
                f(dc, cx + runx0, cy + dy, cx + dx, cy + dy + 1, c);
            }
        }
        if (span) {
            f(dc, cx - r - thick, cy + dy, cx - runx0 + 1, cy + dy + 1, c);
            f(dc, cx + runx0, cy + dy, cx + r + thick, cy + dy + 1, c);
        }
    }
}

void fi_dot(IconFillFn f, void *dc, int cx, int cy, int r, unsigned c)
{
    for (int dy = -r; dy < r; ++dy) {
        int span = r * r - dy * dy;
        if (span < 0)
            continue;
        int dx = 0;
        while (dx * dx <= span)
            ++dx;
        f(dc, cx - dx, cy + dy, cx + dx, cy + dy + 1, c);
    }
}

/* ---- scaled painting context ---- */
typedef struct {
    IconFillFn f;
    void *dc;
    int ox, oy;   /* box origin in host pixels */
    int k;        /* scale: host = virt * k >> 6  (64 = 1:1) */
    unsigned c;
} PC;

static int SX(PC *p, int v) { return p->ox + (v * p->k >> 6); }
static int SY(PC *p, int v) { return p->oy + (v * p->k >> 6); }
static int SS(PC *p, int v) { int s = v * p->k >> 6; return s < 1 ? 1 : s; }

static void L(PC *p, int x0, int y0, int x1, int y1, int t)
{
    fi_line(p->f, p->dc, SX(p, x0), SY(p, y0), SX(p, x1), SY(p, y1), SS(p, t), p->c);
}
static void B(PC *p, int x, int y, int w, int h)
{
    fi_box(p->f, p->dc, SX(p, x), SY(p, y), SS(p, w), SS(p, h), p->c);
}
static void R(PC *p, int x, int y, int w, int h, int t)
{
    fi_rect(p->f, p->dc, SX(p, x), SY(p, y), SS(p, w), SS(p, h), SS(p, t), p->c);
}
static void C(PC *p, int cx, int cy, int r, int t)
{
    fi_circle(p->f, p->dc, SX(p, cx), SY(p, cy), SS(p, r), SS(p, t), p->c);
}
static void D(PC *p, int cx, int cy, int r)
{
    fi_dot(p->f, p->dc, SX(p, cx), SY(p, cy), SS(p, r), p->c);
}

/* ---- glyphs in a 100x100 virtual box ---- */
static void g_hamburger(PC *p)
{
    for (int i = 0; i < 3; ++i)
        B(p, 5, 24 + i * 26, 90, 14);
}
static void g_up(PC *p)
{
    B(p, 42, 22, 16, 72);
    L(p, 12, 54, 50, 14, 14);
    L(p, 50, 14, 88, 54, 14);
}
static void g_chevron(PC *p)
{
    L(p, 32, 14, 70, 50, 10);
    L(p, 70, 50, 32, 86, 10);
}
static void g_chevdown(PC *p)
{
    L(p, 30, 22, 50, 44, 10);
    L(p, 50, 44, 70, 22, 10);
}
static void g_chevup(PC *p)
{
    L(p, 30, 44, 50, 22, 10);
    L(p, 50, 22, 70, 44, 10);
}
static void g_folder(PC *p)
{
    B(p, 4, 20, 40, 14);   /* tab */
    B(p, 4, 30, 92, 54);   /* body */
}
static void g_folder_add(PC *p)
{
    g_folder(p);
}
static void g_file(PC *p)
{
    R(p, 20, 8, 60, 84, 6);
    L(p, 32, 30, 68, 30, 5);
    L(p, 32, 48, 68, 48, 5);
    L(p, 32, 66, 56, 66, 5);
}
static void g_image(PC *p)
{
    R(p, 5, 15, 90, 70, 6);
    D(p, 30, 38, 8);
    L(p, 12, 80, 45, 50, 7);
    L(p, 45, 50, 65, 68, 7);
    L(p, 65, 68, 88, 45, 7);
}
static void g_audio(PC *p)
{
    L(p, 36, 78, 36, 18, 8);
    L(p, 36, 18, 80, 8, 8);
    L(p, 80, 8, 80, 68, 8);
    D(p, 26, 78, 12);
    D(p, 70, 70, 12);
}
static void g_video(PC *p)
{
    R(p, 8, 25, 84, 50, 6);
    B(p, 0, 36, 8, 28);
    B(p, 92, 36, 8, 28);
    L(p, 40, 38, 62, 50, 6);
    L(p, 40, 62, 62, 74, 6);
}
static void g_doc(PC *p)
{
    R(p, 22, 6, 56, 88, 6);
    L(p, 33, 25, 67, 25, 5);
    L(p, 33, 40, 67, 40, 5);
    L(p, 33, 55, 67, 55, 5);
    L(p, 33, 70, 55, 70, 5);
}
static void g_search(PC *p)
{
    C(p, 42, 42, 28, 9);
    L(p, 62, 62, 90, 90, 12);
}
static void g_select(PC *p)
{
    R(p, 8, 12, 84, 76, 5);
    L(p, 20, 34, 27, 43, 6);
    L(p, 27, 43, 42, 25, 6);
    L(p, 54, 32, 80, 32, 6);
    L(p, 20, 66, 27, 75, 6);
    L(p, 27, 75, 42, 57, 6);
    L(p, 54, 64, 80, 64, 6);
}
static void g_selectall(PC *p)
{
    for (int i = 0; i < 3; ++i) {
        R(p, 8, 12 + i * 30, 16, 16, 4);
        L(p, 10, 20 + i * 30, 15, 26 + i * 30, 4);
        L(p, 15, 26 + i * 30, 24, 14 + i * 30, 4);
        L(p, 36, 20 + i * 30, 92, 20 + i * 30, 6);
    }
}
static void g_clear(PC *p)
{
    C(p, 50, 50, 34, 8);
    L(p, 34, 34, 66, 66, 10);
    L(p, 66, 34, 34, 66, 10);
}
static void g_more(PC *p)
{
    D(p, 20, 50, 9);
    D(p, 50, 50, 9);
    D(p, 80, 50, 9);
}
static void g_copy(PC *p)
{
    R(p, 8, 8, 55, 70, 6);
    R(p, 36, 26, 56, 68, 6);
}
static void g_move(PC *p)
{
    R(p, 30, 42, 62, 50, 6);
    L(p, 5, 66, 5, 20, 8);
    L(p, 5, 20, 60, 20, 8);
    L(p, 48, 8, 64, 20, 8);
    L(p, 48, 32, 64, 20, 8);
}
static void g_delete(PC *p)
{
    L(p, 16, 24, 84, 24, 8);
    B(p, 44, 8, 12, 12);
    L(p, 28, 24, 34, 90, 7);
    L(p, 72, 24, 66, 90, 7);
    L(p, 34, 90, 66, 90, 7);
    L(p, 50, 40, 50, 76, 6);
    L(p, 38, 40, 38, 76, 5);
    L(p, 62, 40, 62, 76, 5);
}
static void g_rename(PC *p)
{
    L(p, 20, 84, 12, 60, 9);
    L(p, 12, 60, 60, 12, 9);
    L(p, 60, 12, 88, 40, 9);
    L(p, 88, 40, 40, 88, 9);
    L(p, 40, 88, 20, 84, 9);
    L(p, 70, 22, 80, 32, 6);
}
static void g_properties(PC *p)
{
    C(p, 50, 50, 38, 7);
    D(p, 50, 30, 8);
    B(p, 46, 44, 10, 34);
}
static void g_share(PC *p)
{
    L(p, 50, 12, 50, 66, 9);
    L(p, 28, 32, 50, 10, 9);
    L(p, 50, 10, 72, 32, 9);
    L(p, 12, 52, 12, 88, 9);
    L(p, 12, 88, 88, 88, 9);
    L(p, 88, 52, 88, 88, 9);
}
static void g_refresh(PC *p)
{
    C(p, 50, 50, 30, 9);
    B(p, 40, 2, 26, 22);
}
static void g_list(PC *p)
{
    for (int i = 0; i < 3; ++i) {
        B(p, 8, 16 + i * 30, 12, 12);
        L(p, 32, 22 + i * 30, 92, 22 + i * 30, 8);
    }
}
static void g_icons(PC *p)
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            B(p, 10 + c * 32, 10 + r * 32, 22, 22);
}
static void g_check(PC *p)
{
    L(p, 8, 55, 35, 85, 12);
    L(p, 35, 85, 92, 15, 12);
}
static void g_x(PC *p)
{
    L(p, 18, 18, 82, 82, 12);
    L(p, 82, 18, 18, 82, 12);
}
static void g_back(PC *p)
{
    L(p, 88, 50, 20, 50, 12);
    L(p, 45, 22, 15, 52, 12);
    L(p, 15, 48, 45, 78, 12);
}
static void g_device(PC *p)
{
    R(p, 26, 6, 48, 88, 6);
    B(p, 40, 14, 20, 5);
    D(p, 50, 84, 5);
}
static void g_sdcard(PC *p)
{
    L(p, 20, 88, 20, 30, 6);
    L(p, 20, 30, 35, 12, 6);
    L(p, 35, 12, 80, 12, 6);
    L(p, 80, 12, 80, 88, 6);
    L(p, 20, 88, 80, 88, 6);
    for (int i = 0; i < 3; ++i)
        B(p, 32 + i * 14, 4, 8, 18);
}
static void g_mic(PC *p)
{
    /* classic microphone: capsule, cradle arc, stem, base */
    B(p, 40, 6, 20, 34);          /* capsule */
    L(p, 24, 40, 76, 40, 5);      /* cradle top */
    L(p, 24, 52, 76, 52, 5);      /* cradle bottom */
    L(p, 24, 40, 24, 52, 5);
    L(p, 76, 40, 76, 52, 5);
    L(p, 50, 52, 50, 70, 5);      /* stem */
    L(p, 32, 74, 68, 74, 5);      /* base */
}

static const struct {
    const char *name;
    int id;
    void (*paint)(PC *);
} ICONS[] = {
    {"hamburger", FI_HAMBURGER, g_hamburger},
    {"up", FI_UP, g_up},
    {"chevron", FI_CHEVRON, g_chevron},
    {"chevdown", FI_CHEVDOWN, g_chevdown},
    {"chevup", FI_CHEVUP, g_chevup},
    {"folder", FI_FOLDER, g_folder},
    {"newfolder", FI_FOLDER_ADD, g_folder_add},
    {"file", FI_FILE, g_file},
    {"image", FI_IMAGE, g_image},
    {"audio", FI_AUDIO, g_audio},
    {"video", FI_VIDEO, g_video},
    {"doc", FI_DOC, g_doc},
    {"search", FI_SEARCH, g_search},
    {"select", FI_SELECT, g_select},
    {"selectall", FI_SELECTALL, g_selectall},
    {"clear", FI_CLEAR, g_clear},
    {"more", FI_MORE, g_more},
    {"copy", FI_COPY, g_copy},
    {"move", FI_MOVE, g_move},
    {"delete", FI_DELETE, g_delete},
    {"rename", FI_RENAME, g_rename},
    {"properties", FI_PROPERTIES, g_properties},
    {"share", FI_SHARE, g_share},
    {"refresh", FI_REFRESH, g_refresh},
    {"list", FI_LIST, g_list},
    {"icons", FI_ICONS, g_icons},
    {"check", FI_CHECK, g_check},
    {"x", FI_X, g_x},
    {"back", FI_BACK, g_back},
    {"device", FI_DEVICE, g_device},
    {"sdcard", FI_SDCARD, g_sdcard},
    {"mic", FI_MIC, g_mic},
};

static int str_eq_(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

int fi_find(const char *name)
{
    if (!name)
        return 0;
    for (unsigned i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); ++i)
        if (str_eq_(name, ICONS[i].name))
            return ICONS[i].id;
    return 0;
}

void fi_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id)
{
    for (unsigned i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); ++i) {
        if (ICONS[i].id != id)
            continue;
        PC pc;
        pc.f = fill;
        pc.dc = dc;
        pc.ox = x;
        pc.oy = y;
        pc.k = size * 64 / 100;
        pc.c = color;
        ICONS[i].paint(&pc);
        return;
    }
}

/* amber two-tone folder (W10M Folder_Large.png body #FFB900 / #E8A000) */
void fi_folder(IconFillFn fill, void *dc, int x, int y, int size,
               unsigned body, unsigned shade)
{
    PC pc;
    pc.f = fill;
    pc.dc = dc;
    pc.ox = x;
    pc.oy = y;
    pc.k = size * 64 / 100;
    pc.c = shade;
    B(&pc, 4, 20, 40, 14);            /* tab */
    B(&pc, 4, 30, 92, 5);             /* body top edge */
    pc.c = body;
    B(&pc, 4, 34, 92, 50);            /* body */
}
