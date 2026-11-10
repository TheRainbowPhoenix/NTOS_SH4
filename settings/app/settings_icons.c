/* settings_icons.c — stroke glyphs for the SystemSettings port.
 *
 * Reuses the files_icons.c stroke helpers (fi_line/fi_rect/fi_box/
 * fi_circle/fi_dot) and its 100x100 virtual box + PC scaling model.
 * Shapes per refs/settings-screens captures (see settings/analysis).
 */
#include "settings_icons.h"

/* ---- scaled painting context (same contract as files_icons.c) ---- */
typedef struct {
    IconFillFn f;
    void *dc;
    int ox, oy;
    int k;        /* host = virt * k >> 6 (64 = 1:1) */
    unsigned c;
} SPC;

static int SSX(SPC *p, int v) { return p->ox + (v * p->k >> 6); }
static int SSY(SPC *p, int v) { return p->oy + (v * p->k >> 6); }
static int SSS(SPC *p, int v) { int s = v * p->k >> 6; return s < 1 ? 1 : s; }

static void SL(SPC *p, int x0, int y0, int x1, int y1, int t)
{
    fi_line(p->f, p->dc, SSX(p, x0), SSY(p, y0), SSX(p, x1), SSY(p, y1),
            SSS(p, t), p->c);
}
static void SB(SPC *p, int x, int y, int w, int h)
{
    fi_box(p->f, p->dc, SSX(p, x), SSY(p, y), SSS(p, w), SSS(p, h), p->c);
}
static void SR(SPC *p, int x, int y, int w, int h, int t)
{
    fi_rect(p->f, p->dc, SSX(p, x), SSY(p, y), SSS(p, w), SSS(p, h),
            SSS(p, t), p->c);
}
static void SC_(SPC *p, int cx, int cy, int r, int t)
{
    fi_circle(p->f, p->dc, SSX(p, cx), SSY(p, cy), SSS(p, r), SSS(p, t), p->c);
}
static void SD(SPC *p, int cx, int cy, int r)
{
    fi_dot(p->f, p->dc, SSX(p, cx), SSY(p, cy), SSS(p, r), p->c);
}

/* ---- glyphs (100x100 virtual boxes) ---- */
static void g_gear(SPC *p)
{
    /* cog: 8 teeth as radial stubs + ring + hub */
    for (int i = 0; i < 8; ++i) {
        int a = i * 45;
        /* integer 8-point star stubs via small axis-aligned boxes */
        int t = a % 180 == 0 ? 1 : (a % 90 == 0 ? 0 : 2);
        if (t == 1) { /* vertical teeth */
            SB(p, 44, 2, 12, 14);
            SB(p, 44, 84, 12, 14);
        } else if (t == 0) { /* horizontal teeth */
            SB(p, 2, 44, 14, 12);
            SB(p, 84, 44, 14, 12);
        } else { /* diagonals */
            SB(p, 12, 12, 13, 13);
            SB(p, 75, 12, 13, 13);
            SB(p, 12, 75, 13, 13);
            SB(p, 75, 75, 13, 13);
        }
    }
    SC_(p, 50, 50, 32, 10);
    SC_(p, 50, 50, 10, 8);
}
static void g_phone(SPC *p)
{
    /* tall rounded phone: outline + home line */
    SR(p, 26, 4, 48, 92, 7);
    SL(p, 42, 86, 58, 86, 7);
}
static void g_keyboard(SPC *p)
{
    SR(p, 4, 16, 66, 52, 6);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 7; ++c)
            SB(p, 12 + c * 8, 26 + r * 12, 4, 6);
    /* mouse */
    SR(p, 78, 28, 20, 36, 6);
    SL(p, 88, 32, 88, 42, 4);
}
static void g_globe(SPC *p)
{
    SC_(p, 50, 50, 42, 7);
    SL(p, 8, 50, 92, 50, 5);
    SB(p, 38, 9, 24, 82); /* meridian band edges drawn as 2 arcs below */
    SC_(p, 50, 50, 22, 5);
}
static void g_brush(SPC *p)
{
    /* canvas */
    SR(p, 4, 8, 60, 48, 6);
    /* brush diagonal */
    SL(p, 34, 92, 86, 40, 12);
    SB(p, 80, 26, 18, 18); /* ferrule/handle knot */
    SL(p, 28, 74, 44, 92, 10);
}
static void g_person(SPC *p)
{
    SD(p, 50, 26, 17);
    SL(p, 16, 92, 24, 60, 14);
    SL(p, 24, 60, 50, 52, 12);
    SL(p, 50, 52, 76, 60, 12);
    SL(p, 76, 60, 84, 92, 14);
}
static void g_clockchat(SPC *p)
{
    /* clock */
    SC_(p, 36, 40, 30, 6);
    SL(p, 36, 40, 36, 24, 5);
    SL(p, 36, 40, 50, 46, 5);
    /* speech bubble */
    SR(p, 58, 56, 38, 28, 6);
    SB(p, 62, 82, 12, 12);
    for (int i = 0; i < 2; ++i)
        SD(p, 70 + i * 12, 70, 3);
}
static void g_access(SPC *p)
{
    /* accessibility: person in circle */
    SC_(p, 50, 50, 44, 6);
    SD(p, 50, 28, 8);
    SL(p, 30, 42, 70, 42, 8);
    SL(p, 50, 42, 50, 62, 8);
    SL(p, 50, 62, 32, 80, 8);
    SL(p, 50, 62, 68, 80, 8);
}
static void g_padlock(SPC *p)
{
    SC_(p, 50, 36, 26, 8);   /* shackle */
    SR(p, 18, 40, 64, 52, 6); /* body */
    SD(p, 50, 62, 7);         /* keyhole */
}
static void g_update(SPC *p)
{
    /* two circular arrows */
    SC_(p, 50, 50, 38, 7);
    SB(p, 12, 30, 22, 22);   /* arrowheads (axis-aligned approximations) */
    SL(p, 76, 56, 96, 56, 10);
    SL(p, 86, 44, 96, 56, 10);
    SL(p, 86, 68, 96, 56, 10);
    SL(p, 4, 44, 24, 44, 10);
    SL(p, 14, 32, 4, 44, 10);
    SL(p, 14, 56, 4, 44, 10);
}
static void g_puzzle(SPC *p)
{
    SR(p, 10, 18, 62, 64, 6);
    SC_(p, 40, 18, 13, 6);   /* top tab */
    SC_(p, 72, 50, 13, 6);   /* right tab */
    SC_(p, 40, 82, 13, 6);   /* bottom notch outline */
}
static void g_tiles(SPC *p)
{
    SB(p, 8, 8, 34, 34);
    SB(p, 48, 8, 44, 34);
    SB(p, 8, 48, 34, 44);
    SB(p, 48, 48, 20, 20);
    SB(p, 72, 48, 20, 20);
    SB(p, 48, 72, 20, 20);
    SB(p, 72, 72, 20, 20);
}
static void g_palette(SPC *p)
{
    /* paint palette: circle with thumb hole + 3 dabs */
    SC_(p, 50, 52, 42, 7);
    SD(p, 36, 34, 7);
    SD(p, 58, 30, 7);
    SD(p, 74, 46, 7);
    SD(p, 34, 70, 11);
}
static void g_note(SPC *p)
{
    SL(p, 38, 78, 38, 14, 8);
    SL(p, 38, 14, 86, 4, 8);
    SL(p, 86, 4, 86, 64, 8);
    SD(p, 24, 78, 15);
    SD(p, 72, 66, 15);
}
static void g_screenlock(SPC *p)
{
    SR(p, 6, 12, 88, 62, 6);
    SL(p, 6, 74, 94, 74, 6); /* stand */
    SL(p, 38, 86, 62, 86, 6);
    /* padlock on screen */
    SC_(p, 50, 40, 12, 6);
    SR(p, 34, 44, 32, 24, 5);
}
static void g_navbar(SPC *p)
{
    SR(p, 6, 20, 88, 60, 6);
    SL(p, 6, 36, 94, 36, 6);
    SB(p, 18, 58, 10, 12);
    SB(p, 45, 58, 10, 12);
    SB(p, 72, 58, 10, 12);
}
static void g_monitor(SPC *p)
{
    SR(p, 6, 10, 88, 60, 6);
    SL(p, 50, 70, 50, 84, 7);
    SL(p, 30, 86, 70, 86, 7);
}
static void g_battery(SPC *p)
{
    SR(p, 10, 26, 76, 44, 6);
    SB(p, 86, 38, 8, 20);
    SB(p, 18, 34, 20, 28); /* charge */
    /* leaf */
    SL(p, 56, 66, 78, 44, 7);
    SL(p, 78, 44, 78, 66, 7);
    SL(p, 78, 66, 56, 66, 7);
}
static void g_storage(SPC *p)
{
    SR(p, 8, 16, 84, 30, 6);
    SD(p, 20, 31, 4);
    SR(p, 8, 54, 84, 30, 6);
    SD(p, 20, 69, 4);
}
static void g_info(SPC *p)
{
    SC_(p, 50, 50, 42, 7);
    SB(p, 45, 24, 10, 24);
    SB(p, 45, 58, 10, 22);
}
static void g_play(SPC *p)
{
    /* filled play triangle: apex left (28,18), base right, tip (84,50) */
    for (int i = 0; i < 32; ++i)      /* top half widens */
        SB(p, 28, 18 + i, 2 + i * 56 / 32, 1);
    for (int i = 0; i < 32; ++i)      /* bottom half narrows */
        SB(p, 28, 50 + i, 2 + 56 - i * 56 / 32, 1);
}
static void g_usb(SPC *p)
{
    /* USB trident: circle + two arms + square tip */
    SD(p, 50, 82, 12);
    SL(p, 50, 70, 50, 16, 7);
    SB(p, 44, 6, 12, 12);
    SL(p, 50, 40, 22, 26, 6);
    SL(p, 22, 26, 22, 40, 6);
    SD(p, 22, 22, 7);
    SL(p, 50, 52, 78, 34, 6);
    SR(p, 74, 20, 16, 16, 5);
}

static const struct {
    const char *name;
    int id;
    void (*paint)(SPC *);
} SICONS[] = {
    {"gear", SI_GEAR, g_gear},
    {"phone", SI_PHONE, g_phone},
    {"keyboard", SI_KEYBOARD, g_keyboard},
    {"globe", SI_GLOBE, g_globe},
    {"brush", SI_BRUSH, g_brush},
    {"person", SI_PERSON, g_person},
    {"clockchat", SI_CLOCKCHAT, g_clockchat},
    {"access", SI_ACCESS, g_access},
    {"padlock", SI_PADLOCK, g_padlock},
    {"update", SI_UPDATE, g_update},
    {"puzzle", SI_PUZZLE, g_puzzle},
    {"tiles", SI_TILES, g_tiles},
    {"palette", SI_PALETTE, g_palette},
    {"note", SI_NOTE, g_note},
    {"screenlock", SI_SCREENLOCK, g_screenlock},
    {"navbaricon", SI_NAVBAR, g_navbar},
    {"monitor", SI_MONITOR, g_monitor},
    {"battery", SI_BATTERY, g_battery},
    {"storage", SI_STORAGE, g_storage},
    {"info", SI_INFO, g_info},
    {"play", SI_PLAY, g_play},
    {"usb", SI_USB, g_usb},
};

static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

int si_find(const char *name)
{
    if (!name)
        return 0;
    for (unsigned i = 0; i < sizeof(SICONS) / sizeof(SICONS[0]); ++i)
        if (s_eq(name, SICONS[i].name))
            return SICONS[i].id;
    return 0;
}

void si_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id)
{
    if (!fill || id <= 0 || id >= SI_COUNT)
        return;
    SPC pc;
    pc.f = fill;
    pc.dc = dc;
    pc.ox = x;
    pc.oy = y;
    pc.k = size * 64 / 100;
    pc.c = color;
    SICONS[id - 1].paint(&pc);
}
