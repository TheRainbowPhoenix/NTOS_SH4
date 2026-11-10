/* calc_icons.c — vector operator glyphs for the WindowsCalculator port.
 *
 * Same scaled-painting model as settings_icons.c: a 100x100 virtual box,
 * PC-relative scaling (k = 64 at 1:1) on top of the files_icons.c stroke
 * helpers. Glyph shapes follow the Segoe MDL2 calculator set (E94x) as
 * described in calc/analysis/CALCULATOR_UI_REFERENCE.md.
 */
#include "calc_icons.h"

/* ---- scaled painting context ---- */
typedef struct {
    IconFillFn f;
    void *dc;
    int ox, oy;
    int k;        /* host = virt * k >> 6 (64 = 1:1) */
    unsigned c;
} CPC;

static int CX(CPC *p, int v) { return p->ox + (v * p->k >> 6); }
static int CY(CPC *p, int v) { return p->oy + (v * p->k >> 6); }
static int CS(CPC *p, int v) { int s = v * p->k >> 6; return s < 1 ? 1 : s; }

static void L(CPC *p, int x0, int y0, int x1, int y1, int t)
{
    fi_line(p->f, p->dc, CX(p, x0), CY(p, y0), CX(p, x1), CY(p, y1),
            CS(p, t), p->c);
}
static void B(CPC *p, int x, int y, int w, int h)
{
    fi_box(p->f, p->dc, CX(p, x), CY(p, y), CS(p, w), CS(p, h), p->c);
}
static void D_(CPC *p, int cx, int cy, int r)
{
    fi_dot(p->f, p->dc, CX(p, cx), CY(p, cy), CS(p, r), p->c);
}

/* ---- glyphs ---- */

static void g_divide(CPC *p)
{
    B(p, 12, 47, 76, 8);      /* bar */
    D_(p, 50, 22, 8);         /* upper dot */
    D_(p, 50, 78, 8);         /* lower dot */
}

static void g_times(CPC *p)
{
    L(p, 18, 18, 82, 82, 10);
    L(p, 82, 18, 18, 82, 10);
}

static void g_plus(CPC *p)
{
    B(p, 12, 46, 76, 9);
    B(p, 45, 13, 10, 76);
}

static void g_minus(CPC *p)
{
    B(p, 12, 46, 76, 9);
}

static void g_equal(CPC *p)
{
    B(p, 12, 30, 76, 10);
    B(p, 12, 60, 76, 10);
}

static void g_negate(CPC *p)
{
    /* plus above, minus below (the MDL2 +/-) */
    B(p, 18, 20, 64, 8);
    B(p, 46, 10, 8, 30);
    B(p, 18, 74, 64, 8);
}

static void g_percent(CPC *p)
{
    D_(p, 26, 76, 13);
    D_(p, 74, 26, 13);
    L(p, 66, 8, 30, 92, 9);
}

static void g_sqrt(CPC *p)
{
    /* radical: short tail, long rise, vinculum */
    L(p, 8, 52, 24, 52, 8);
    L(p, 24, 52, 38, 92, 9);
    L(p, 38, 92, 58, 8, 10);
    B(p, 58, 6, 36, 9);       /* vinculum */
}

static void g_backspace(CPC *p)
{
    /* MDL2 E94F: arrow-eraser body with an X */
    L(p, 8, 26, 40, 26, 9);
    L(p, 40, 26, 92, 26, 9);
    L(p, 92, 26, 92, 76, 9);
    L(p, 92, 76, 40, 76, 9);
    L(p, 40, 76, 8, 51, 9);
    L(p, 8, 51, 8, 26, 9);
    L(p, 8, 26, 40, 26, 9);   /* close the nose */
    L(p, 50, 38, 82, 66, 9);  /* X */
    L(p, 82, 38, 50, 66, 9);
}

static void g_invert(CPC *p)
{
    /* 1 over x with a fraction bar */
    B(p, 22, 14, 9, 40);      /* "1" stem */
    B(p, 14, 20, 24, 8);      /* serif   */
    B(p, 12, 47, 76, 8);      /* bar     */
    B(p, 66, 62, 9, 12);      /* "x" left arm */
    L(p, 62, 62, 84, 86, 9);  /* "x" body */
    L(p, 84, 62, 62, 86, 9);
}

static void g_abs(CPC *p)
{
    B(p, 16, 14, 8, 72);
    B(p, 38, 30, 24, 9);      /* "x" arms hint */
    L(p, 34, 26, 66, 74, 9);
    L(p, 66, 26, 34, 74, 9);
    B(p, 76, 14, 8, 72);
}

static void g_pi(CPC *p)
{
    B(p, 10, 26, 80, 9);      /* top bar */
    B(p, 14, 26, 9, 60);      /* left leg */
    B(p, 66, 26, 9, 46);      /* right leg */
    B(p, 60, 66, 21, 9);      /* right foot */
    B(p, 40, 42, 9, 26);      /* center tail */
}

static void g_factorial(CPC *p)
{
    B(p, 30, 12, 12, 46);    /* "!" stem */
    D_(p, 36, 76, 9);        /* dot  */
    /* the "n" is drawn as text by the caller (composite glyph) */
}

static void g_paren_l(CPC *p)
{
    L(p, 62, 8, 30, 50, 9);
    L(p, 30, 50, 62, 92, 9);
}

static void g_paren_r(CPC *p)
{
    L(p, 38, 8, 70, 50, 9);
    L(p, 70, 50, 38, 92, 9);
}

static void g_chev_l(CPC *p)
{
    L(p, 62, 18, 34, 50, 8);
    L(p, 34, 50, 62, 82, 8);
}

static void g_chev_r(CPC *p)
{
    L(p, 38, 18, 66, 50, 8);
    L(p, 66, 50, 38, 82, 8);
}

static void g_history(CPC *p)
{
    /* the Calculator.xbf HistoryButton path: clock face with a
     * counterclockwise arrowhead at the top-left */
    L(p, 46, 22, 46, 50, 8);  /* hand up   */
    L(p, 46, 50, 68, 64, 8);  /* hand right */
    /* circle (16 segments) */
    for (int i = 0; i < 16; ++i) {
        int a0 = i * 22, a1 = a0 + 24;
        /* fixed-point cos/sin table would be overkill: coarse 8-gon */
    }
    /* octagon ring, r=42 */
    static const int oct[9][2] = {
        {50, 8}, {80, 20}, {92, 50}, {80, 80}, {50, 92}, {20, 80},
        {8, 50}, {20, 20}, {50, 8}
    };
    for (int i = 0; i < 8; ++i)
        L(p, oct[i][0], oct[i][1], oct[i + 1][0], oct[i + 1][1], 9);
    /* arrowhead at 10-o'clock, pointing outward/ccw */
    L(p, 20, 20, 6, 32, 9);
    L(p, 20, 20, 30, 8, 9);
    L(p, 6, 32, 30, 8, 9);
}

static void g_mem(CPC *p)
{
    /* the Calculator.xbf MemoryButton path: "M" with a trailing caret */
    B(p, 8, 22, 9, 58);       /* left stem */
    L(p, 12, 24, 40, 62, 9);  /* diagonals */
    L(p, 40, 62, 68, 24, 9);
    B(p, 64, 22, 9, 58);      /* right stem */
    /* caret: V at bottom right */
    L(p, 76, 92, 88, 76, 8);
    L(p, 88, 76, 96, 92, 8);
}

static void g_xpow2(CPC *p)
{
    /* composite: big x + small 2 in the corner; the caller overlays text */
    B(p, 8, 52, 30, 9);       /* x crossbar (hint) */
    L(p, 10, 26, 42, 76, 11); /* x strokes */
    L(p, 42, 26, 10, 76, 11);
    B(p, 60, 22, 8, 26);      /* "2" spine */
    B(p, 60, 18, 22, 8);
    B(p, 66, 34, 16, 8);
    L(p, 82, 26, 82, 52, 8);
    B(p, 60, 46, 26, 8);
}

static void g_xpow3(CPC *p)
{
    L(p, 10, 26, 42, 76, 11);
    L(p, 42, 26, 10, 76, 11);
    /* small 3 */
    B(p, 58, 18, 24, 8);
    B(p, 74, 26, 8, 10);
    B(p, 60, 38, 22, 8);
    B(p, 74, 46, 8, 10);
    B(p, 58, 58, 26, 8);
}

static void g_xpowy(CPC *p)
{
    L(p, 8, 30, 40, 80, 11);
    L(p, 40, 30, 8, 80, 11);
    /* small y */
    L(p, 56, 22, 70, 44, 8);
    L(p, 84, 22, 70, 44, 8);
    L(p, 70, 44, 70, 70, 8);
}

static void g_yroot(CPC *p)
{
    /* y-th root: small y + radical */
    L(p, 8, 16, 22, 40, 7);
    L(p, 34, 16, 22, 40, 7);
    L(p, 22, 40, 22, 54, 7);
    L(p, 22, 54, 36, 92, 8);  /* radical rise */
    L(p, 36, 92, 54, 8, 8);
    B(p, 54, 6, 34, 8);
}

static void g_tenpow(CPC *p)
{
    /* 10^x */
    B(p, 8, 34, 9, 42);       /* 1 */
    B(p, 26, 36, 8, 38);      /* 0 sides */
    B(p, 26, 36, 26, 9);      /* 0 top/bottom */
    B(p, 26, 66, 26, 9);
    /* small x */
    L(p, 64, 22, 92, 52, 8);
    L(p, 92, 22, 64, 52, 8);
}

static void g_eexp(CPC *p)
{
    /* e^x */
    B(p, 10, 42, 30, 9);      /* e mid */
    B(p, 10, 34, 9, 26);      /* e spine */
    L(p, 18, 34, 38, 38, 8);  /* e loop hint */
    B(p, 10, 58, 30, 9);
    L(p, 64, 22, 92, 52, 8);  /* small x */
    L(p, 92, 22, 64, 52, 8);
}

/* ---- registry ---- */
typedef void (*CiFn)(CPC *);

static const struct { const char *name; CiFn fn; } CI_GLYPHS[] = {
    {"divide", g_divide},   {"times", g_times},
    {"plus", g_plus},       {"minus", g_minus},
    {"equal", g_equal},     {"negate", g_negate},
    {"percent", g_percent}, {"sqrt", g_sqrt},
    {"backspace", g_backspace}, {"invert", g_invert},
    {"abs", g_abs},         {"pi", g_pi},
    {"factorial", g_factorial},
    {"paren-l", g_paren_l}, {"paren-r", g_paren_r},
    {"chev-l", g_chev_l},   {"chev-r", g_chev_r},
    {"history", g_history}, {"mem", g_mem},
    {"xpow2", g_xpow2},     {"xpow3", g_xpow3},
    {"xpowy", g_xpowy},     {"yroot", g_yroot},
    {"tenpow", g_tenpow},   {"eexp", g_eexp},
};

int ci_find(const char *name)
{
    if (!name)
        return 0;
    for (unsigned i = 0; i < sizeof(CI_GLYPHS) / sizeof(CI_GLYPHS[0]); ++i) {
        const char *a = name, *b = CI_GLYPHS[i].name;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0)
            return 1 + (int)i;
    }
    return 0;
}

void ci_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id)
{
    if (id <= 0 || id > (int)(sizeof(CI_GLYPHS) / sizeof(CI_GLYPHS[0])))
        return;
    CPC p;
    p.f = fill;
    p.dc = dc;
    p.ox = x;
    p.oy = y;
    p.k = (size << 6) / 100;
    p.c = color;
    CI_GLYPHS[id - 1].fn(&p);
}
