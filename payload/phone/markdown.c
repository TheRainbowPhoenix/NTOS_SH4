/* markdown.c — MARKDOWN.DLL: shared web-document engine for NTOS.
 *
 * A from-scratch "Netscape without JS" core for the 4 MB / no-FPU device:
 *   - HTML4-subset parser (implied closes, attrs, entities, <style>)
 *   - CSS1-subset engine (tag/class/id/descendant selectors, cascade with
 *     specificity, the box model: margin/border/padding/width/height,
 *     floats + clear, colors, fonts, text-align, display)
 *   - Markdown parser (CommonMark-lite: emphasis, links w/ balanced
 *     brackets/parens, nested lists, tables, fences, quotes, images,
 *     autolinks, link refs, frontmatter, inline HTML passthrough)
 *   - box layout with float avoidance + word wrap -> flat paint-op list
 *   - paint / find-in-page / link hit-test / reader mode / title
 *
 * Integer-only, single-pass per phase, all static arenas, no CRT, no
 * ported library. The host app supplies every system call through an
 * MdHost callback table (markdown.h) so any app can embed the engine.
 */
#include "markdown.h"
#ifdef MD_DEBUG
#include <stdio.h>
#endif

/* =============== tiny helpers (freestanding) =============== */
static int xlen(const char *s)
{
    int n = 0;
    while (s[n])
        ++n;
    return n;
}
static int xeq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int xlow(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static int xdigit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int xisalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int xisalnum(int c)
{
    return xisalpha(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
static int xisspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
#define UNSET 0xF8000000u /* "no colour" sentinel in 0xBBGGRR space */

/* =============== arenas =============== */
#define MAX_SRC 20000
#define NBLK 200
#define NINL 800
#define NRULE 96
#define NOPS 2300
#define NFLT 24
#define NLNK 96
#define NREF 24
#define NIMG 6
#define IMG_CAP 2560
#define NNAME 24 /* interned class/id names */
#define NAME_CAP 16

static char g_src[MAX_SRC];
static int g_src_len;

/* tags we know (identity drives UA defaults + CSS matching) */
enum {
    TG_NONE = 0, TG_P, TG_H1, TG_H2, TG_H3, TG_H4, TG_H5, TG_H6,
    TG_DIV, TG_SPAN, TG_A, TG_B, TG_I, TG_U, TG_EM, TG_STRONG, TG_CODE,
    TG_PRE, TG_BR, TG_IMG, TG_INPUT, TG_FORM, TG_UL, TG_OL, TG_LI,
    TG_BLOCKQUOTE, TG_HR, TG_TABLE, TG_TR, TG_TD, TG_TH, TG_DL, TG_DT,
    TG_DD, TG_HTML, TG_BODY, TG_CENTER, TG_FONT, TG_SMALL, TG_BIG,
    TG_SUB, TG_SUP, TG_S, TG_STRIKE, TG_DEL, TG_TT, TG_MARK,
    TG_KBD, TG_SAMP, TG_CITE, TG_VAR, TG_INS,
    TG_SELECT, TG_OPTION, TG_ADDRESS, TG_SECTION, TG_ARTICLE,
    TG_HEADER, TG_FOOTER, TG_NAV, TG_MAIN, TG_HGROUP, TG_LABEL,
    TG_COUNT_
};
/* block-level by default (everything else defaults inline) */
static const unsigned char TG_IS_BLOCK[TG_COUNT_] = {
    [TG_P] = 1, [TG_H1] = 1, [TG_H2] = 1, [TG_H3] = 1, [TG_H4] = 1,
    [TG_H5] = 1, [TG_H6] = 1, [TG_DIV] = 1, [TG_PRE] = 1, [TG_FORM] = 1,
    [TG_UL] = 1, [TG_OL] = 1, [TG_LI] = 1, [TG_BLOCKQUOTE] = 1,
    [TG_HR] = 1, [TG_TABLE] = 1, [TG_TR] = 1, [TG_TD] = 1, [TG_TH] = 1,
    [TG_DL] = 1, [TG_DT] = 1, [TG_DD] = 1, [TG_HTML] = 1, [TG_BODY] = 1,
    [TG_CENTER] = 1, [TG_SECTION] = 1, [TG_ARTICLE] = 1, [TG_HEADER] = 1,
    [TG_FOOTER] = 1, [TG_NAV] = 1, [TG_MAIN] = 1, [TG_HGROUP] = 1,
    [TG_ADDRESS] = 1, [TG_LABEL] = 1, [TG_SELECT] = 1, [TG_OPTION] = 1,
};

/* display values */
enum { D_BLOCK, D_INLINE, D_NONE, D_LIST_ITEM, D_TABLE_CELL };
/* float/clear */
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { CL_NONE, CL_LEFT, CL_RIGHT, CL_BOTH };
/* text align */
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

/* set-mask bits for the CSS cascade */
enum {
    SM_MT, SM_MR, SM_MB, SM_ML,
    SM_PT, SM_PR, SM_PB, SM_PL,
    SM_BT, SM_BR, SM_BB, SM_BL,
    SM_BDC, SM_BNONE,
    SM_W, SM_H, SM_BG, SM_FG, SM_FS, SM_LH,
    SM_DISP, SM_FLT, SM_CLR, SM_ALIGN,
    SM_WEIGHT, SM_FSTYLE, SM_DECO, SM_MONO,
    SM_COUNT_
};
#define SM_M_GROUP ((1ull << SM_MT) | (1ull << SM_MR) | (1ull << SM_MB) | (1ull << SM_ML))
#define SM_P_GROUP ((1ull << SM_PT) | (1ull << SM_PR) | (1ull << SM_PB) | (1ull << SM_PL))
#define SM_B_GROUP ((1ull << SM_BT) | (1ull << SM_BR) | (1ull << SM_BB) | (1ull << SM_BL))

/* units: value x10 */
enum { U_PX, U_EM, U_PCT, U_AUTO, U_NUM };

typedef struct {
    unsigned long long set;
    short m[4], p[4], bw[4]; /* T R B L, x10 */
    unsigned char mu, pu, bu, bnone;
    short w, h; /* x10 */
    unsigned char wu, hu;
    short fs, lh; /* x10 */
    unsigned char fsu, lhu;
    unsigned fg, bg, bdc; /* UNSET = not set */
    unsigned char disp, flt, clr, align, weight, fstyle, deco, mono;
} CssSt;

/* synthetic block kinds (md constructs without an html tag) */
enum { BK_NONE, BK_HR, BK_PRE, BK_QUOTE, BK_LIST, BK_ITEM, BK_CELL };

typedef struct {
    unsigned char tag;   /* TG_* */
    unsigned char kind;  /* BK_* */
    short parent, first_child, next_sib, last_child;
    short inl, ninl;     /* inline items */
    short id;            /* interned id, -1 */
    short cls[2];        /* interned classes, -1 */
    short level;         /* heading/list level, table col count */
    short marker;        /* list marker: 0 disc 1 decimal */
    CssSt st;
} Blk;

/* inline items */
enum { T_TEXT, T_BR, T_IMG, T_INPUT };
enum { IN_RADIO, IN_CHECK, IN_TEXT, IN_SUBMIT };

typedef struct {
    unsigned char kind, flags; /* MDTF_* in flags */
    short link;                /* link idx or -1 */
    short img;                 /* image slot or -1 */
    short len, w, h;
    signed char fdelta;        /* font-size delta px */
    unsigned char inp;         /* IN_* for T_INPUT */
    unsigned color;            /* inline colour override, 0 = inherit */
    short owner;               /* owning block (parse-time bookkeeping) */
    const char *s;
} Inl;

typedef struct {
    const char *s;
    short len;
} MdLnk;

typedef struct { /* markdown link reference definitions */
    char id[NAME_CAP];
    const char *url;
    short len;
} MdRef;

typedef struct {
    unsigned char k, f;
    short x, y, w, h;
    unsigned c;
    const char *s;
    short len, link, img;
    unsigned char fh;
} POp;

enum { PO_FILL, PO_TEXT, PO_SVF, PO_ELL, PO_INPUT };

typedef struct {
    unsigned char side;
    short xl, xr, yt, yb;
} Flt;

typedef struct {
    char name[28];
    unsigned char data[IMG_CAP];
    unsigned len;
    short vw, vh; /* svf viewbox aspect, -1 unknown */
} Img;

/* resolved (layout-time) style */
typedef struct {
    short m[4], p[4], bw[4]; /* px, T R B L */
    short w, h;              /* px, -1 auto */
    unsigned fg, bg, bdc;
    short fs, lh;            /* px; lh 0 = auto */
    unsigned char disp, flt, clr, align, weight, fstyle, deco, mono;
    short fh;                /* mapped font height */
    unsigned char mark;      /* list marker once resolved */
    unsigned char nowrap;
} RSt;

static Blk blks[NBLK];
static int nblk;
static Inl inls[NINL];
static int ninl;
static MdLnk lnks[NLNK];
static int nlnk;
static MdRef refs[NREF];
static int nref;
static Img imgs[NIMG];
static int nimg;
static char names[NNAME][NAME_CAP];
static int nname;

static POp ops[NOPS];
static int nops;
static Flt flts[NFLT];
static int nflt;
static RSt rs_[NBLK];

static int g_page_w, g_page_h, g_view_w;
static int g_reader;
static char g_title[72];
static int g_title_len;
static const MdHost *g_host;
static int g_dialect;

/* css rules */
typedef struct {
    unsigned char tag[3]; /* TG_* or 0xFF = any, right-to-left */
    short cls[3], id[3];  /* -1 = n/a */
    unsigned char nsel, spec;
    CssSt st;
} CssRule;
static CssRule rules[NRULE];
static int nrule;
static char cssbuf[7000];
static int cssbuf_len;

/* =============== CSS =============== */
static unsigned pack_rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (unsigned)r | ((unsigned)g << 8) | ((unsigned)b << 16);
}

static const struct { const char *n; unsigned v; } CSS_COLORS[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0x0000FF},
    {"green", 0x008000}, {"blue", 0xFF0000}, {"yellow", 0x00FFFF},
    {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xC0C0C0},
    {"maroon", 0x000080}, {"purple", 0x800080}, {"fuchsia", 0xFF00FF},
    {"magenta", 0xFF00FF}, {"lime", 0x00FF00}, {"olive", 0x008080},
    {"navy", 0x800000}, {"teal", 0x808000}, {"aqua", 0xFFFF00},
    {"cyan", 0xFFFF00}, {"orange", 0x00A5FF}, {"pink", 0xCBC0FF},
    {"brown", 0x2A2AA5}, {"gold", 0x00D7FF}, {"darkred", 0x00008B},
    {"darkblue", 0x8B0000}, {"darkgray", 0xA9A9A9}, {"lightgray", 0xD3D3D3},
    {"lightgrey", 0xD3D3D3}, {"beige", 0xDCDCF5}, {"ivory", 0xFFFFF0},
};

/* packed length: 14-bit signed x10 value + 2-bit unit (0 px/num, 1 em, 2 %) */
#define LENC(v10, u) ((short)(((v10) & 0x3FFF) | ((u) << 14)))
static int LENV(short e)
{
    int v = e & 0x3FFF;
    if (v & 0x2000)
        v -= 0x4000;
    return v;
}
#define LENU(e) (((unsigned)(e) >> 14) & 3)
static unsigned char len_pack_unit(unsigned char u)
{
    return u == U_EM ? 1 : u == U_PCT ? 2 : 0;
}

/* skip whitespace + css comments */
static const char *cskip(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/'))
                ++p;
            if (*p)
                p += 2;
            continue;
        }
        return p;
    }
}

/* parse a length: number (+ optional decimals) + unit -> x10 value.
 * ".5em" and "41.17%" both work; unitless numbers come back as U_NUM
 * (line-height multipliers) which resolve_len treats like px. */
static int parse_len(const char **pp, short *out, unsigned char *unit)
{
    const char *p = *pp;
    int neg = 0;
    if (*p == '-') { neg = 1; ++p; }
    else if (*p == '+') ++p;
    int v = 0, tenth = 0, any = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        any = 1;
    }
    if (*p == '.') {
        ++p;
        if (*p >= '0' && *p <= '9') {
            tenth = *p++ - '0';
            any = 1;
        }
        while (*p >= '0' && *p <= '9')
            ++p; /* precision beyond x10 is dropped */
    }
    if (!any)
        return 0;
    v = v * 10 + tenth;
    if (neg)
        v = -v;
    unsigned char u = U_NUM;
    if (p[0] == 'p' && p[1] == 'x') { u = U_PX; p += 2; }
    else if (p[0] == 'p' && p[1] == 't') { u = U_PX; p += 2; v = v * 96 / 72; }
    else if (p[0] == 'e' && p[1] == 'm') { u = U_EM; p += 2; }
    else if (p[0] == '%') { u = U_PCT; ++p; }
    *out = (short)v;
    *unit = u;
    *pp = p;
    return 1;
}

static int parse_color(const char **pp, unsigned *out)
{
    const char *p = *pp;
    if (*p == '#') {
        ++p;
        int h[6], n = 0;
        while (n < 6 && xdigit((unsigned char)p[n]) >= 0) {
            h[n] = xdigit((unsigned char)p[n]);
            ++n;
        }
        if (n == 3) {
            *out = pack_rgb(h[0] * 17, h[1] * 17, h[2] * 17);
            p += 3;
            *pp = p;
            return 1;
        }
        if (n >= 6) {
            *out = pack_rgb(h[0] * 16 + h[1], h[2] * 16 + h[3], h[4] * 16 + h[5]);
            p += 6;
            *pp = p;
            return 1;
        }
        return 0;
    }
    if (p[0] == 'r' && p[1] == 'g' && p[2] == 'b' && p[3] == '(') {
        p += 4;
        int c[3], i = 0;
        while (i < 3) {
            int v = 0, any = 0, pct = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); any = 1; }
            if (!any)
                return 0;
            if (*p == '%') { pct = 1; ++p; }
            c[i++] = pct ? v * 255 / 100 : v;
            if (*p == ',')
                ++p;
        }
        while (*p && *p != ')')
            ++p;
        if (*p)
            ++p;
        *out = pack_rgb(c[0], c[1], c[2]);
        *pp = p;
        return 1;
    }
    for (unsigned k = 0; k < sizeof(CSS_COLORS) / sizeof(CSS_COLORS[0]); ++k) {
        int l = xlen(CSS_COLORS[k].n);
        if (xeq(p, CSS_COLORS[k].n, l)) {
            *out = CSS_COLORS[k].v;
            *pp = p + l;
            return 1;
        }
    }
    return 0;
}

/* side shorthands: v[4] filled T R B L from 1-4 values, each encoded with
 * its own unit so "0 0 0 1em" keeps the em on the left side only. */
static int parse_sides(const char **pp, short *v)
{
    const char *p = cskip(*pp);
    short t[4];
    unsigned char u[4];
    int n = 0;
    while (n < 4 && parse_len(&p, &t[n], &u[n])) {
        ++n;
        p = cskip(p);
        if (*p == ';' || *p == '}' || !*p)
            break;
    }
    if (!n)
        return 0;
    for (int i = 0; i < 4; ++i) {
        int idx = n == 1 ? 0 : n == 2 ? (i & 1) : n == 3 ? (i == 0 ? 0 : i - 1 > 1 ? 1 : i) : i;
        if (n == 3 && i == 3)
            idx = 1;
        v[i] = LENC(t[idx], len_pack_unit(u[idx]));
    }
    *pp = p;
    return 1;
}

static void st_set(CssSt *st, int bit)
{
    st->set |= 1ull << bit;
}

/* apply one declaration to a style */
static void css_decl(CssSt *st, const char *prop, int plen, const char *val)
{
    const char *p = cskip(val);
#define P_IS(s) (plen == xlen(s) && xeq(prop, s, plen))
    if (P_IS("margin")) {
        if (parse_sides(&p, st->m)) { st_set(st, SM_MT); st_set(st, SM_MR); st_set(st, SM_MB); st_set(st, SM_ML); }
    } else if (P_IS("margin-top")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->m[0] = LENC(v, len_pack_unit(u)); st_set(st, SM_MT); } }
    else if (P_IS("margin-right")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->m[1] = LENC(v, len_pack_unit(u)); st_set(st, SM_MR); } }
    else if (P_IS("margin-bottom")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->m[2] = LENC(v, len_pack_unit(u)); st_set(st, SM_MB); } }
    else if (P_IS("margin-left")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->m[3] = LENC(v, len_pack_unit(u)); st_set(st, SM_ML); } }
    else if (P_IS("padding")) {
        if (parse_sides(&p, st->p)) { st_set(st, SM_PT); st_set(st, SM_PR); st_set(st, SM_PB); st_set(st, SM_PL); }
    } else if (P_IS("padding-top")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->p[0] = LENC(v, len_pack_unit(u)); st_set(st, SM_PT); } }
    else if (P_IS("padding-right")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->p[1] = LENC(v, len_pack_unit(u)); st_set(st, SM_PR); } }
    else if (P_IS("padding-bottom")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->p[2] = LENC(v, len_pack_unit(u)); st_set(st, SM_PB); } }
    else if (P_IS("padding-left")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->p[3] = LENC(v, len_pack_unit(u)); st_set(st, SM_PL); } }
    else if (P_IS("border-width")) {
        if (parse_sides(&p, st->bw)) { st_set(st, SM_BT); st_set(st, SM_BR); st_set(st, SM_BB); st_set(st, SM_BL); }
    } else if (P_IS("border-top-width")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->bw[0] = LENC(v, len_pack_unit(u)); st_set(st, SM_BT); } }
    else if (P_IS("border-right-width")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->bw[1] = LENC(v, len_pack_unit(u)); st_set(st, SM_BR); } }
    else if (P_IS("border-bottom-width")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->bw[2] = LENC(v, len_pack_unit(u)); st_set(st, SM_BB); } }
    else if (P_IS("border-left-width")) { short v; unsigned char u; if (parse_len(&p, &v, &u)) { st->bw[3] = LENC(v, len_pack_unit(u)); st_set(st, SM_BL); } }
    if (P_IS("border")) {
        /* try width then style then color tokens in any order */
        for (int guard = 0; guard < 4; ++guard) {
            p = cskip(p);
            if (!*p || *p == ';')
                break;
            short v;
            unsigned char u;
            const char *q = p;
            if (parse_len(&q, &v, &u)) {
                for (int i = 0; i < 4; ++i)
                    st->bw[i] = LENC(v, len_pack_unit(u));
                st->bnone = 0;
                st_set(st, SM_BT); st_set(st, SM_BR); st_set(st, SM_BB); st_set(st, SM_BL);
                p = q;
                continue;
            }
            unsigned c;
            q = p;
            if (parse_color(&q, &c)) {
                st->bdc = c;
                st_set(st, SM_BDC);
                p = q;
                continue;
            }
            /* style keyword */
            if (xeq(p, "solid", 5) || xeq(p, "dotted", 6) || xeq(p, "dashed", 6) ||
                xeq(p, "double", 6) || xeq(p, "groove", 6) || xeq(p, "ridge", 5) ||
                xeq(p, "inset", 5) || xeq(p, "outset", 6)) {
                p += 4; /* shortest is inset/ridge=5; step safely below */
                p = cskip(p);
                while (*p && *p != ';' && *p != ' ' && *p != '}')
                    ++p;
                continue;
            }
            if (xeq(p, "none", 4) || xeq(p, "hidden", 6)) {
                for (int i = 0; i < 4; ++i)
                    st->bw[i] = 0;
                st->bnone = 1;
                st_set(st, SM_BT); st_set(st, SM_BR); st_set(st, SM_BB); st_set(st, SM_BL);
                st_set(st, SM_BNONE);
                p += xeq(p, "none", 4) ? 4 : 6;
                continue;
            }
            break; /* unknown token: stop */
        }
    } else if (P_IS("border-style")) {
        p = cskip(p);
        if (xeq(p, "none", 4)) {
            for (int i = 0; i < 4; ++i)
                st->bw[i] = 0;
            st->bnone = 1;
            st_set(st, SM_BNONE);
        } else {
            st->bnone = 0;
            st_set(st, SM_BNONE);
        }
    } else if (P_IS("border-color")) {
        unsigned c;
        if (parse_color(&p, &c)) { st->bdc = c; st_set(st, SM_BDC); }
    } else if (P_IS("width")) {
        if (xeq(p, "auto", 4)) { st->w = -10; st->wu = U_AUTO; }
        else if (parse_len(&p, &st->w, &st->wu)) st_set(st, SM_W);
    } else if (P_IS("height")) {
        if (xeq(p, "auto", 4)) { st->h = -10; st->hu = U_AUTO; }
        else if (parse_len(&p, &st->h, &st->hu)) st_set(st, SM_H);
    } else if (P_IS("background-color") || P_IS("background")) {
        unsigned c;
        if (xeq(p, "transparent", 11)) { st->bg = UNSET; st_set(st, SM_BG); }
        else if (parse_color(&p, &c)) { st->bg = c; st_set(st, SM_BG); }
    } else if (P_IS("color")) {
        unsigned c;
        if (parse_color(&p, &c)) { st->fg = c; st_set(st, SM_FG); }
    } else if (P_IS("font-size")) {
        if (xeq(p, "small", 5)) { st->fs = 100; st->fsu = U_PCT; }
        else if (xeq(p, "x-small", 7)) { st->fs = 80; st->fsu = U_PCT; }
        else if (xeq(p, "xx-small", 8)) { st->fs = 65; st->fsu = U_PCT; }
        else if (xeq(p, "medium", 6)) { st->fs = 100; st->fsu = U_PCT; }
        else if (xeq(p, "large", 5)) { st->fs = 120; st->fsu = U_PCT; }
        else if (xeq(p, "x-large", 7)) { st->fs = 150; st->fsu = U_PCT; }
        else if (parse_len(&p, &st->fs, &st->fsu)) {}
        if (1) st_set(st, SM_FS);
    } else if (P_IS("line-height")) {
        if (xeq(p, "normal", 6)) { st->lhu = U_NUM; st->lh = 12; }
        else if (parse_len(&p, &st->lh, &st->lhu)) {
            /* U_NUM (unitless multiplier) kept as-is */
        } else {
            /* unitless number like 1.9: parse_len got it as px-less digits */
            int v = 0, any = 0, tenth = 0;
            const char *q = p;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (*q++ - '0'); any = 1; }
            if (*q == '.') { ++q; if (*q >= '0' && *q <= '9') tenth = *q++ - '0'; }
            if (any) {
                st->lh = (short)(v * 10 + tenth);
                st->lhu = U_NUM;
                p = q;
            }
        }
        st_set(st, SM_LH);
    } else if (P_IS("text-align")) {
        if (xeq(p, "center", 6)) { st->align = AL_CENTER; st_set(st, SM_ALIGN); }
        else if (xeq(p, "right", 5)) { st->align = AL_RIGHT; st_set(st, SM_ALIGN); }
        else if (xeq(p, "left", 6) || xeq(p, "justify", 7)) { st->align = AL_LEFT; st_set(st, SM_ALIGN); }
    } else if (P_IS("float")) {
        if (xeq(p, "left", 4)) { st->flt = FL_LEFT; st_set(st, SM_FLT); }
        else if (xeq(p, "right", 5)) { st->flt = FL_RIGHT; st_set(st, SM_FLT); }
        else if (xeq(p, "none", 4)) { st->flt = FL_NONE; st_set(st, SM_FLT); }
    } else if (P_IS("clear")) {
        if (xeq(p, "left", 4)) { st->clr = CL_LEFT; st_set(st, SM_CLR); }
        else if (xeq(p, "right", 5)) { st->clr = CL_RIGHT; st_set(st, SM_CLR); }
        else if (xeq(p, "both", 4)) { st->clr = CL_BOTH; st_set(st, SM_CLR); }
        else if (xeq(p, "none", 4)) { st->clr = CL_NONE; st_set(st, SM_CLR); }
    } else if (P_IS("display")) {
        if (xeq(p, "none", 4)) { st->disp = D_NONE; st_set(st, SM_DISP); }
        else if (xeq(p, "block", 5)) { st->disp = D_BLOCK; st_set(st, SM_DISP); }
        else if (xeq(p, "inline", 6)) { st->disp = D_INLINE; st_set(st, SM_DISP); }
        else if (xeq(p, "list-item", 9)) { st->disp = D_LIST_ITEM; st_set(st, SM_DISP); }
        else if (xeq(p, "table-cell", 10)) { st->disp = D_TABLE_CELL; st_set(st, SM_DISP); }
        else { st->disp = D_BLOCK; st_set(st, SM_DISP); } /* table etc */
    } else if (P_IS("font-weight")) {
        if (xeq(p, "bold", 4) || xeq(p, "bolder", 6)) { st->weight = 1; st_set(st, SM_WEIGHT); }
        else if (xeq(p, "normal", 6)) { st->weight = 0; st_set(st, SM_WEIGHT); }
        else {
            int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            if (v) { st->weight = v >= 600; st_set(st, SM_WEIGHT); }
        }
    } else if (P_IS("font-style")) {
        if (xeq(p, "italic", 6) || xeq(p, "oblique", 7)) { st->fstyle = 1; st_set(st, SM_FSTYLE); }
        else if (xeq(p, "normal", 6)) { st->fstyle = 0; st_set(st, SM_FSTYLE); }
    } else if (P_IS("font-family") || P_IS("font-variant") || P_IS("letter-spacing") ||
               P_IS("word-spacing") || P_IS("text-decoration") || P_IS("white-space") ||
               P_IS("list-style") || P_IS("vertical-align") || P_IS("text-indent")) {
        /* accepted and ignored (decoration handled via tags for now) */
    }
#undef P_IS
}

/* font shorthand: [style] [weight] size[/line-height] family */
static void css_font_shorthand(CssSt *st, const char *val)
{
    const char *p = cskip(val);
    for (int guard = 0; guard < 6; ++guard) {
        p = cskip(p);
        if (!*p || *p == ';')
            break;
        if (xeq(p, "italic", 6)) { st->fstyle = 1; st_set(st, SM_FSTYLE); p += 6; continue; }
        if (xeq(p, "oblique", 7)) { st->fstyle = 1; st_set(st, SM_FSTYLE); p += 7; continue; }
        if (xeq(p, "bold", 4)) { st->weight = 1; st_set(st, SM_WEIGHT); p += 4; continue; }
        if (xeq(p, "bolder", 6)) { st->weight = 1; st_set(st, SM_WEIGHT); p += 6; continue; }
        if (xeq(p, "normal", 6)) { p += 6; continue; }
        if (xeq(p, "small-caps", 10)) { p += 10; continue; }
        short v;
        unsigned char u;
        const char *q = p;
        if (parse_len(&q, &v, &u)) {
            st->fs = v;
            st->fsu = u;
            st_set(st, SM_FS);
            p = q;
            if (*p == '/') {
                ++p;
                short lh;
                unsigned char lu;
                const char *r = p;
                if (parse_len(&r, &lh, &lu)) {
                    st->lh = lh;
                    st->lhu = lu;
                    st_set(st, SM_LH);
                    p = r;
                }
            }
            break; /* family names follow: done */
        }
        break;
    }
}

static int intern_name(const char *s, int len)
{
    if (len <= 0 || len >= NAME_CAP || nname >= NNAME)
        return -1;
    for (int i = 0; i < nname; ++i)
        if (xlen(names[i]) == len && xeq(names[i], s, len))
            return i;
    for (int i = 0; i < len; ++i)
        names[nname][i] = (char)xlow((unsigned char)s[i]);
    names[nname][len] = 0;
    return nname++;
}

/* parse the whole <style> text into rules[] */
static void css_parse(const char *css)
{
    const char *p = css;
    for (;;) {
        p = cskip(p);
        if (!*p)
            break;
        if (*p == '@') { /* at-rule: skip to ; or block */
            while (*p && *p != ';' && *p != '{')
                ++p;
            if (*p == '{') {
                int d = 1;
                ++p;
                while (*p && d) {
                    if (*p == '{') ++d;
                    else if (*p == '}') --d;
                    ++p;
                }
            } else if (*p)
                ++p;
            continue;
        }
        /* selector list */
        CssRule r;
        for (unsigned i = 0; i < sizeof(r); ++i)
            ((unsigned char *)&r)[i] = 0; /* zero init (tag any=0 handled below) */
        for (int i = 0; i < 3; ++i) {
            r.cls[i] = -1;
            r.id[i] = -1;
            r.tag[i] = 0xFF;
        }
        int nsel = 0, spec = 0, got = 0, any = 0;
        const char *s = p;
        while (*p && *p != '{' && *p != ',') {
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '>') {
                if (got && nsel < 3) {
                    ++nsel;
                    got = 0;
                }
                ++p;
                continue;
            }
            any = 1;
            /* one simple selector */
            unsigned char *tg = &r.tag[nsel];
            short *cl = &r.cls[nsel];
            short *id = &r.id[nsel];
            if (*p == '*') {
                *tg = 0xFF;
                ++p;
                got = 1;
                continue;
            }
            if (*p == '#') {
                ++p;
                const char *b = p;
                while (xisalnum((unsigned char)*p))
                    ++p;
                *id = intern_name(b, (int)(p - b));
                spec += 100;
                got = 1;
                continue;
            }
            if (*p == '.') {
                ++p;
                const char *b = p;
                while (xisalnum((unsigned char)*p))
                    ++p;
                *cl = intern_name(b, (int)(p - b));
                spec += 10;
                got = 1;
                continue;
            }
            if (xisalpha((unsigned char)*p)) {
                const char *b = p;
                while (xisalnum((unsigned char)*p))
                    ++p;
                int l = (int)(p - b);
                int found = TG_NONE;
                static const struct { const char *n; int t; } TAGN[] = {
                    {"p", TG_P}, {"h1", TG_H1}, {"h2", TG_H2}, {"h3", TG_H3},
                    {"h4", TG_H4}, {"h5", TG_H5}, {"h6", TG_H6},
                    {"div", TG_DIV}, {"span", TG_SPAN}, {"a", TG_A},
                    {"b", TG_B}, {"i", TG_I}, {"u", TG_U}, {"em", TG_EM},
                    {"strong", TG_STRONG}, {"code", TG_CODE}, {"pre", TG_PRE},
                    {"br", TG_BR}, {"img", TG_IMG}, {"input", TG_INPUT},
                    {"form", TG_FORM}, {"ul", TG_UL}, {"ol", TG_OL},
                    {"li", TG_LI}, {"blockquote", TG_BLOCKQUOTE}, {"hr", TG_HR},
                    {"table", TG_TABLE}, {"tr", TG_TR}, {"td", TG_TD},
                    {"th", TG_TH}, {"dl", TG_DL}, {"dt", TG_DT}, {"dd", TG_DD},
                    {"html", TG_HTML}, {"body", TG_BODY}, {"center", TG_CENTER},
                    {"font", TG_FONT}, {"small", TG_SMALL}, {"big", TG_BIG},
                    {"sub", TG_SUB}, {"sup", TG_SUP}, {"s", TG_S},
                    {"strike", TG_STRIKE}, {"del", TG_DEL}, {"tt", TG_TT},
                    {"mark", TG_MARK}, {"kbd", TG_KBD}, {"samp", TG_SAMP},
                    {"cite", TG_CITE}, {"var", TG_VAR}, {"ins", TG_INS},
                    {"select", TG_SELECT},
                    {"option", TG_OPTION}, {"address", TG_ADDRESS},
                    {"section", TG_SECTION}, {"article", TG_ARTICLE},
                    {"header", TG_HEADER}, {"footer", TG_FOOTER},
                    {"nav", TG_NAV}, {"main", TG_MAIN}, {"label", TG_LABEL},
                };
                for (unsigned k = 0; k < sizeof(TAGN) / sizeof(TAGN[0]); ++k) {
                    if ((int)xlen(TAGN[k].n) == l) {
                        int eq = 1;
                        for (int q = 0; q < l; ++q)
                            if (xlow((unsigned char)b[q]) != TAGN[k].n[q])
                                eq = 0;
                        if (eq) {
                            found = TAGN[k].t;
                            break;
                        }
                    }
                }
                *tg = (unsigned char)found;
                spec += 1;
                got = 1;
                continue;
            }
            ++p; /* stray char */
        }
        if (got && nsel < 3)
            ++nsel;
        if (!any)
            nsel = 0;
        if (*p == ',') { /* selector lists: duplicate later; here: skip rest */
            while (*p && *p != '{')
                ++p;
        }
        if (*p != '{') {
            while (*p && *p != '}')
                ++p;
            if (*p)
                ++p;
            continue;
        }
        ++p;
        (void)s;
        r.nsel = (unsigned char)nsel;
        r.spec = (unsigned char)(spec > 200 ? 200 : spec);
        if (!nsel || nrule >= NRULE) {
            while (*p && *p != '}')
                ++p;
            if (*p)
                ++p;
            continue;
        }
        /* declarations */
        for (;;) {
            p = cskip(p);
            if (!*p || *p == '}')
                break;
            const char *b = p;
            while (*p && *p != ':' && *p != ';' && *p != '}')
                ++p;
            if (*p != ':') {
                while (*p && *p != ';' && *p != '}')
                    ++p;
                if (*p == ';')
                    ++p;
                continue;
            }
            char prop[24];
            int pl = (int)(p - b);
            while (pl > 0 && (b[pl - 1] == ' ' || b[pl - 1] == '\t'))
                --pl;
            if (pl >= (int)sizeof(prop))
                pl = sizeof(prop) - 1;
            for (int i = 0; i < pl; ++i)
                prop[i] = (char)xlow((unsigned char)b[i]);
            prop[pl] = 0;
            ++p;
            const char *v = cskip(p);
            while (*p && *p != ';' && *p != '}')
                ++p;
            char vbuf[96];
            int vl = (int)(p - v);
            while (vl > 0 && (v[vl - 1] == ' ' || v[vl - 1] == '\t' || v[vl - 1] == '\n'))
                --vl;
            if (vl >= (int)sizeof(vbuf))
                vl = sizeof(vbuf) - 1;
            for (int i = 0; i < vl; ++i)
                vbuf[i] = v[i];
            vbuf[vl] = 0;
            if (pl == 4 && xeq(prop, "font", 4))
                css_font_shorthand(&r.st, vbuf);
            else
                css_decl(&r.st, prop, pl, vbuf);
            if (*p == ';')
                ++p;
        }
        if (*p == '}')
            ++p;
        rules[nrule++] = r;
    }
    /* stable insertion sort by specificity: low applies first */
    for (int i = 1; i < nrule; ++i) {
        CssRule key = rules[i];
        int j = i - 1;
        while (j >= 0 && rules[j].spec > key.spec) {
            rules[j + 1] = rules[j];
            --j;
        }
        rules[j + 1] = key;
    }
}

/* does element bi match rule part i (rightmost = nsel-1)? */
static int part_match(const CssRule *r, int pi, int bi)
{
    const Blk *b = &blks[bi];
    if (r->tag[pi] != 0xFF && r->tag[pi] != b->tag)
        return 0;
    if (r->id[pi] >= 0 && r->id[pi] != b->id)
        return 0;
    if (r->cls[pi] >= 0 &&
        r->cls[pi] != b->cls[0] && r->cls[pi] != b->cls[1])
        return 0;
    return 1;
}

static int rule_matches(const CssRule *r, int bi)
{
    if (!part_match(r, r->nsel - 1, bi))
        return 0;
    int ai = bi; /* ancestor cursor */
    for (int pi = r->nsel - 2; pi >= 0; --pi) {
        ai = blks[ai].parent;
        while (ai >= 0 && !part_match(r, pi, ai))
            ai = blks[ai].parent;
        if (ai < 0)
            return 0;
    }
    return 1;
}

/* merge a CssSt into another, only fields with set bits */
static void st_merge(CssSt *dst, const CssSt *src)
{
    for (int b = 0; b < SM_COUNT_; ++b) {
        if (!(src->set & (1ull << b)))
            continue;
        switch (b) {
        case SM_MT: dst->m[0] = src->m[0]; break;
        case SM_MR: dst->m[1] = src->m[1]; break;
        case SM_MB: dst->m[2] = src->m[2]; break;
        case SM_ML: dst->m[3] = src->m[3]; break;
        case SM_PT: dst->p[0] = src->p[0]; break;
        case SM_PR: dst->p[1] = src->p[1]; break;
        case SM_PB: dst->p[2] = src->p[2]; break;
        case SM_PL: dst->p[3] = src->p[3]; break;
        case SM_BT: dst->bw[0] = src->bw[0]; break;
        case SM_BR: dst->bw[1] = src->bw[1]; break;
        case SM_BB: dst->bw[2] = src->bw[2]; break;
        case SM_BL: dst->bw[3] = src->bw[3]; break;
        case SM_BDC: dst->bdc = src->bdc; break;
        case SM_BNONE: dst->bnone = src->bnone; break;
        case SM_W: dst->w = src->w; dst->wu = src->wu; break;
        case SM_H: dst->h = src->h; dst->hu = src->hu; break;
        case SM_BG: dst->bg = src->bg; break;
        case SM_FG: dst->fg = src->fg; break;
        case SM_FS: dst->fs = src->fs; dst->fsu = src->fsu; break;
        case SM_LH: dst->lh = src->lh; dst->lhu = src->lhu; break;
        case SM_DISP: dst->disp = src->disp; break;
        case SM_FLT: dst->flt = src->flt; break;
        case SM_CLR: dst->clr = src->clr; break;
        case SM_ALIGN: dst->align = src->align; break;
        case SM_WEIGHT: dst->weight = src->weight; break;
        case SM_FSTYLE: dst->fstyle = src->fstyle; break;
        case SM_DECO: dst->deco = src->deco; break;
        case SM_MONO: dst->mono = src->mono; break;
        default: break;
        }
        dst->set |= (1ull << b);
    }
    /* units follow their group when the whole group was set */
    if ((src->set & 0xF) == 0xF)
        dst->mu = src->mu;
    if ((src->set & (0xF << 4)) == (0xF << 4))
        dst->pu = src->pu;
    if ((src->set & (0xF << 8)) == (0xF << 8))
        dst->bu = src->bu;
}

/* =============== text arena (decoded/escaped runs live here) =============== */
static char g_txt[10240];
static int g_txt_len;
static int txt_put(const char *s, int len)
{
    if (len <= 0)
        return g_txt_len;
    if (g_txt_len + len + 1 > (int)sizeof(g_txt)) {
        int room = (int)sizeof(g_txt) - g_txt_len - 1;
        if (room <= 0)
            return g_txt_len;
        len = room;
    }
    for (int i = 0; i < len; ++i)
        g_txt[g_txt_len + i] = s[i];
    g_txt_len += len;
    g_txt[g_txt_len] = 0;
    return g_txt_len - len;
}

/* entity decode: returns bytes written to out (<= in len) */
static int entity(const char *p, int len, char *out, int cap)
{
    /* p points at '&' */
    if (len < 3)
        return 0;
    int used = 0;
    char ch = 0;
    if (p[1] == '#') {
        int code = 0, i = 2;
        if (p[2] == 'x' || p[2] == 'X') {
            i = 3;
            while (i < len && xdigit((unsigned char)p[i]) >= 0 && code < 0x110000)
                code = code * 16 + xdigit((unsigned char)p[i++]);
        } else {
            while (i < len && p[i] >= '0' && p[i] <= '9' && code < 0x110000)
                code = code * 10 + (p[i++] - '0');
        }
        if (i >= len || p[i] != ';')
            return 0;
        used = i + 1;
        ch = code > 0 && code < 127 ? (char)code : '?';
    } else {
        static const struct { const char *n; char c; } ENT[] = {
            {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'},
            {"apos;", '\''}, {"nbsp;", ' '}, {"copy;", 63}, {"reg;", '?'},
            {"hellip;", '.'}, {"mdash;", '-'}, {"ndash;", '-'}, {"middot;", '.'},
            {"laquo;", '<'}, {"raquo;", '>'}, {"bull;", '-'}, {"prime;", '\''},
        };
        for (unsigned k = 0; k < sizeof(ENT) / sizeof(ENT[0]); ++k) {
            int l = xlen(ENT[k].n);
            if (len >= 1 + l && xeq(p + 1, ENT[k].n, l)) {
                ch = ENT[k].c;
                used = 1 + l;
                break;
            }
        }
        if (!used)
            return 0;
    }
    if (cap < 1)
        return 0;
    *out = ch;
    return used;
}

/* =============== doc tree builders =============== */
static int blk_new(int parent, unsigned char tag, unsigned char kind)
{
    if (nblk >= NBLK)
        return -1;
    Blk *b = &blks[nblk];
    for (int i = 0; i < (int)sizeof(Blk); ++i)
        ((unsigned char *)b)[i] = 0;
    b->tag = (unsigned char)tag;
    b->kind = (unsigned char)kind;
    b->parent = (short)parent;
    b->first_child = b->next_sib = b->last_child = -1;
    b->inl = -1;
    b->id = -1;
    b->cls[0] = b->cls[1] = -1;
    b->st.bg = b->st.fg = b->st.bdc = UNSET;
    if (parent >= 0) {
        Blk *par = &blks[parent];
        if (par->first_child < 0)
            par->first_child = (short)nblk;
        else
            blks[par->last_child].next_sib = (short)nblk;
        par->last_child = (short)nblk;
    }
    return nblk++;
}

static void blk_attach_inline(int bi)
{
    blks[bi].inl = (short)ninl;
    blks[bi].ninl = 0;
}

/* =============== HTML parser =============== */
typedef struct {
    unsigned char flags; /* MDTF_* */
    unsigned color;      /* 0 inherit */
    signed char fdelta;
    short link;
} ISt;

typedef struct {
    int cur;
    ISt ist[12];
    int nist;
} HParser;

static void ps_push(HParser *ps, unsigned char flags, unsigned color,
                    signed char fd, short link)
{
    if (ps->nist < 12) {
        ps->ist[ps->nist].flags = flags;
        ps->ist[ps->nist].color = color;
        ps->ist[ps->nist].fdelta = fd;
        ps->ist[ps->nist].link = link;
    }
    ++ps->nist;
}
static void ps_pop(HParser *ps)
{
    if (ps->nist > 0)
        --ps->nist;
}
static ISt ps_top(const HParser *ps)
{
    static const ISt zero = {0, 0, 0, -1}; /* flags, color, fdelta, link */
    return ps->nist ? ps->ist[ps->nist - 1] : zero;
}

static int add_inl(HParser *ps, unsigned char kind)
{
    if (ninl >= NINL)
        return -1;
    Inl *it = &inls[ninl];
    for (int i = 0; i < (int)sizeof(Inl); ++i)
        ((unsigned char *)it)[i] = 0;
    it->kind = kind;
    it->link = -1;
    it->img = -1;
    it->owner = (short)ps->cur;
    if (blks[ps->cur].inl < 0)
        blk_attach_inline(ps->cur);
    ++blks[ps->cur].ninl;
    return ninl++;
}

static void ps_text(HParser *ps, const char *s, int len)
{
    if (len <= 0)
        return;
    ISt st = ps_top(ps);
    /* merge with previous item when same block, same style, contiguous */
    Blk *cb = &blks[ps->cur];
    if (ninl > 0 && cb->inl >= 0 && cb->ninl > 0 &&
        ninl == cb->inl + cb->ninl) {
        Inl *prev = &inls[ninl - 1];
        if (prev->kind == T_TEXT && prev->flags == st.flags &&
            prev->color == st.color && prev->fdelta == st.fdelta &&
            prev->link == st.link && prev->s + prev->len == s) {
#ifdef MD_DEBUG2
            fprintf(stderr, "[merge] cur=%d prev#%d len %d += %d (off %ld) '%.*s'\n",
                    ps->cur, (int)(prev - inls), prev->len, len,
                    (long)(s - g_txt), len > 20 ? 20 : len, s);
#endif
            prev->len = (short)(prev->len + len);
            return;
        }
    }
    int idx = add_inl(ps, T_TEXT);
    if (idx < 0)
        return;
    inls[idx].s = s;
    inls[idx].len = (short)len;
    inls[idx].flags = st.flags;
    inls[idx].color = st.color;
    inls[idx].fdelta = st.fdelta;
    inls[idx].link = st.link;
}

static int link_add(const char *href, int len)
{
    if (nlnk >= NLNK || len <= 0)
        return -1;
    lnks[nlnk].s = href;
    lnks[nlnk].len = (short)len;
    return nlnk++;
}

static int img_slot(const char *name, int len)
{
    if (len <= 0 || len >= 27)
        return -1;
    for (int i = 0; i < nimg; ++i)
        if (xlen(imgs[i].name) == len && xeq(imgs[i].name, name, len))
            return i;
    if (nimg >= NIMG)
        return -1;
    Img *im = &imgs[nimg];
    for (int i = 0; i < len; ++i)
        im->name[i] = name[i];
    im->name[len] = 0;
    im->len = 0;
    im->vw = im->vh = -1;
    return nimg++;
}

typedef struct {
    const char *id;
    int id_len;
    const char *cls;
    int cls_len;
    const char *style, *href, *src, *val;
    int style_len, href_len, src_len, val_len, alt_len;
    int w, h;
    unsigned char align; /* 0 none 1 center 2 right */
    unsigned color;      /* font color attr, UNSET */
    unsigned char inp;   /* IN_* */
    unsigned char checked;
    unsigned char fsize; /* font size attr 1..7 -> delta */
} TagAttr;

static void attr_parse(const char *p, const char *end, TagAttr *at)
{
    for (int i = 0; i < (int)sizeof(TagAttr); ++i)
        ((unsigned char *)at)[i] = 0;
    at->color = UNSET;
    at->w = at->h = -1;
    while (p < end) {
        while (p < end && (xisspace((unsigned char)*p) || *p == '/'))
            ++p;
        if (p >= end)
            break;
        const char *nb = p;
        while (p < end && !xisspace((unsigned char)*p) && *p != '=')
            ++p;
        const char *vn = nb;
        int vl = (int)(p - nb);
        const char *v = 0;
        int vlen = 0;
        if (p < end && *p == '=') {
            ++p;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p++;
                v = p;
                while (p < end && *p != q)
                    ++p;
                vlen = (int)(p - v);
                if (p < end)
                    ++p;
            } else {
                v = p;
                while (p < end && !xisspace((unsigned char)*p))
                    ++p;
                vlen = (int)(p - v);
            }
        }
#define A_IS(s) (vl == xlen(s) && xeq(vn, s, vl))
        if (A_IS("id")) { at->id = v; at->id_len = vlen; }
        else if (A_IS("class")) { at->cls = v; at->cls_len = vlen; }
        else if (A_IS("style")) { at->style = v; at->style_len = vlen; }
        else if (A_IS("href")) { at->href = v; at->href_len = vlen; }
        else if (A_IS("src")) { at->src = v; at->src_len = vlen; }
        else if (A_IS("value") || A_IS("alt")) { at->val = v; at->val_len = vlen; }
        else if (A_IS("width")) {
            int n = 0, any = 0;
            for (int i = 0; i < vlen && v[i] >= '0' && v[i] <= '9'; ++i) { n = n * 10 + v[i] - '0'; any = 1; }
            if (any) at->w = n;
        } else if (A_IS("height")) {
            int n = 0, any = 0;
            for (int i = 0; i < vlen && v[i] >= '0' && v[i] <= '9'; ++i) { n = n * 10 + v[i] - '0'; any = 1; }
            if (any) at->h = n;
        } else if (A_IS("align")) {
            if (vlen >= 6 && xeq(v, "center", 6)) at->align = 1;
            else if (vlen >= 5 && xeq(v, "right", 5)) at->align = 2;
        } else if (A_IS("color")) {
            const char *vp = v;
            unsigned c = UNSET;
            if (parse_color(&vp, &c))
                at->color = c;
        } else if (A_IS("size")) {
            if (vlen && v[0] >= '1' && v[0] <= '7')
                at->fsize = (unsigned char)(v[0] - '4'); /* delta around medium */
        } else if (A_IS("type")) {
            if (vlen >= 5 && xeq(v, "radio", 5)) at->inp = IN_RADIO;
            else if (vlen >= 8 && xeq(v, "checkbox", 8)) at->inp = IN_CHECK;
            else if (vlen >= 6 && (xeq(v, "submit", 6) || xeq(v, "button", 6))) at->inp = IN_SUBMIT;
            else at->inp = IN_TEXT;
        } else if (A_IS("checked")) {
            at->checked = 1;
        }
#undef A_IS
    }
}

static void apply_style_attr(Blk *b, const char *style, int len)
{
    if (len <= 0 || len > 200)
        return;
    char buf[208];
    for (int i = 0; i < len; ++i)
        buf[i] = style[i];
    buf[len] = 0;
    const char *p = buf;
    for (;;) {
        p = cskip(p);
        if (!*p)
            break;
        const char *b0 = p;
        while (*p && *p != ':' && *p != ';')
            ++p;
        if (*p != ':')
            break;
        char prop[24];
        int pl = (int)(p - b0);
        while (pl > 0 && (b0[pl - 1] == ' ' || b0[pl - 1] == '\t'))
            --pl;
        if (pl >= (int)sizeof(prop))
            pl = sizeof(prop) - 1;
        for (int i = 0; i < pl; ++i)
            prop[i] = (char)xlow((unsigned char)b0[i]);
        prop[pl] = 0;
        ++p;
        const char *v = p;
        while (*p && *p != ';')
            ++p;
        char vbuf[96];
        int vl = (int)(p - v);
        while (vl > 0 && (v[vl - 1] == ' ' || v[vl - 1] == '\t'))
            --vl;
        if (vl >= (int)sizeof(vbuf))
            vl = sizeof(vbuf) - 1;
        for (int i = 0; i < vl; ++i)
            vbuf[i] = v[i];
        vbuf[vl] = 0;
        if (pl == 4 && xeq(prop, "font", 4))
            css_font_shorthand(&b->st, vbuf);
        else
            css_decl(&b->st, prop, pl, vbuf);
        if (*p == ';')
            ++p;
    }
}

/* should `newtag` implicitly close `top`? */
static int tag_closes(int newt, int top)
{
    if (top == TG_P && TG_IS_BLOCK[newt] && newt != TG_P)
        return 1;
    if (newt == TG_P && top == TG_P)
        return 1;
    if (newt == TG_LI && top == TG_LI)
        return 1;
    if ((newt == TG_DT || newt == TG_DD) && (top == TG_DT || top == TG_DD))
        return 1;
    if (newt == TG_TR && (top == TG_TD || top == TG_TH || top == TG_TR))
        return 1;
    if ((newt == TG_TD || newt == TG_TH) && (top == TG_TD || top == TG_TH))
        return 1;
    if (newt == TG_OPTION && top == TG_OPTION)
        return 1;
    return 0;
}

/* open a block under ps->cur, running implied closes up the stack */
static int ps_open_block(HParser *ps, int tag, TagAttr *at)
{
    /* implied closes: walk up while the new tag closes the current */
    for (;;) {
        int closed = 0;
        for (int b = ps->cur; b >= 0; ) {
            /* only close blocks that are "inline-leaf" style containers:
             * p, li, dt, dd, td, th, option, and inline parents like a */
            int t = blks[b].tag;
            if (tag_closes(tag, t)) {
                /* pop style stack entries that belong inside (they were
                 * pushed while inside the closing block: we track none,
                 * so just reset the stack to the block boundary = 0) */
                ps->cur = blks[b].parent;
                ps->nist = 0;
                closed = 1;
                break;
            }
            if (blks[b].parent < 0)
                break;
            b = blks[b].parent;
        }
        if (!closed)
            break;
    }
    int bi = blk_new(ps->cur, tag, 0);
    if (bi < 0)
        return -1;
    Blk *b = &blks[bi];
    if (at) {
        if (at->id_len > 0)
            b->id = (short)intern_name(at->id, at->id_len);
        if (at->cls_len > 0) {
            const char *c = at->cls;
            const char *end = c + at->cls_len;
            for (int k = 0; k < 2; ++k) {
                while (c < end && *c == ' ')
                    ++c;
                const char *w = c;
                while (c < end && *c != ' ')
                    ++c;
                if (c > w)
                    b->cls[k] = (short)intern_name(w, (int)(c - w));
            }
        }
        apply_style_attr(b, at->style, at->style_len);
        if (at->align) {
            b->st.align = (unsigned char)(at->align == 1 ? AL_CENTER : AL_RIGHT);
            st_set(&b->st, SM_ALIGN);
        }
        if (at->color != UNSET) {
            b->st.fg = at->color;
            st_set(&b->st, SM_FG);
        }
        if (at->fsize) {
            b->st.fs = (short)(100 + at->fsize * 20);
            b->st.fsu = U_PCT;
            st_set(&b->st, SM_FS);
        }
    }
    ps->cur = bi;
    return bi;
}

static void ps_close_block(HParser *ps, int tag)
{
    for (int b = ps->cur; b >= 0; b = blks[b].parent) {
        if (blks[b].tag == tag) {
            ps->cur = blks[b].parent;
            ps->nist = 0;
            return;
        }
    }
}

/* handle one inline html tag (positioned at '<'); shared with markdown.
 * returns end pointer after '>'. */
static const char *html_inline_tag(HParser *ps, const char *p, const char *end)
{
    const char *tg = p + 1;
    int close = 0;
    if (tg < end && *tg == '/') {
        close = 1;
        ++tg;
    }
    if (tg >= end || !xisalpha((unsigned char)*tg))
        return 0; /* not a tag */
    const char *te = tg;
    while (te < end && xisalnum((unsigned char)*te))
        ++te;
    int tl = (int)(te - tg);
    const char *tagend = te;
    while (tagend < end && *tagend != '>')
        ++tagend;
    if (tagend >= end)
        return 0;
    TagAttr at;
    attr_parse(te, tagend, &at);
    /* lowercase tag id */
    char lname[12];
    if (tl > 11)
        tl = 11;
    for (int i = 0; i < tl; ++i)
        lname[i] = (char)xlow((unsigned char)tg[i]);
    lname[tl] = 0;
#define TG_IS(s) (tl == xlen(s) && xeq(lname, s, tl))
    if (!close) {
        if (TG_IS("br")) {
            add_inl(ps, T_BR);
        } else if (TG_IS("img")) {
            int slot = img_slot(at.src, at.src_len);
            if (slot >= 0) {
                int idx = add_inl(ps, T_IMG);
                if (idx >= 0) {
                    inls[idx].img = (short)slot;
                    inls[idx].w = (short)at.w;
                    inls[idx].h = (short)at.h;
                }
            }
        } else if (TG_IS("input")) {
            int idx = add_inl(ps, T_INPUT);
            if (idx >= 0) {
                inls[idx].inp = at.inp;
                inls[idx].h = at.checked;
                if (at.val_len > 0) {
                    int off = txt_put(at.val, at.val_len);
                    inls[idx].s = g_txt + off;
                    inls[idx].len = (short)at.val_len;
                }
            }
        } else if (TG_IS("a")) {
            int li = link_add(at.href, at.href_len);
            ps_push(ps, ps_top(ps).flags | MDTF_UL, 0, 0, (short)li);
        } else if (TG_IS("b") || TG_IS("strong"))
            ps_push(ps, ps_top(ps).flags | MDTF_BOLD, 0, 0, -1);
        else if (TG_IS("i") || TG_IS("em") || TG_IS("cite") || TG_IS("var"))
            ps_push(ps, ps_top(ps).flags | MDTF_ITAL, 0, 0, -1);
        else if (TG_IS("u") || TG_IS("ins"))
            ps_push(ps, ps_top(ps).flags | MDTF_UL, 0, 0, -1);
        else if (TG_IS("s") || TG_IS("strike") || TG_IS("del"))
            ps_push(ps, ps_top(ps).flags | MDTF_STRIKE, 0, 0, -1);
        else if (TG_IS("code") || TG_IS("tt") || TG_IS("kbd") || TG_IS("samp"))
            ps_push(ps, ps_top(ps).flags, 0x606060, 0, -1);
        else if (TG_IS("small"))
            ps_push(ps, ps_top(ps).flags, 0, -2, -1);
        else if (TG_IS("big"))
            ps_push(ps, ps_top(ps).flags, 0, 2, -1);
        else if (TG_IS("sub") || TG_IS("sup"))
            ps_push(ps, ps_top(ps).flags, 0, -4, -1);
        else if (TG_IS("font")) {
            ISt st = ps_top(ps);
            if (at.color != UNSET)
                st.color = at.color;
            if (at.fsize)
                st.fdelta = (signed char)(at.fsize * 2);
            ps_push(ps, st.flags, st.color, st.fdelta, st.link);
        }
        /* unknown/other inline tags: transparent */
    } else {
        if (TG_IS("a") || TG_IS("b") || TG_IS("strong") || TG_IS("i") ||
            TG_IS("em") || TG_IS("cite") || TG_IS("var") || TG_IS("u") ||
            TG_IS("ins") || TG_IS("s") || TG_IS("strike") || TG_IS("del") ||
            TG_IS("code") || TG_IS("tt") || TG_IS("kbd") || TG_IS("samp") ||
            TG_IS("small") || TG_IS("big") || TG_IS("sub") || TG_IS("sup") ||
            TG_IS("font") || TG_IS("span"))
            ps_pop(ps);
    }
#undef TG_IS
    return tagend + 1;
}

/* decode text chunk with entities into the txt arena, then emit */
static void ps_text_ent(HParser *ps, const char *s, int len)
{
    int out0 = g_txt_len;
    for (int i = 0; i < len;) {
        if (s[i] == '&') {
            char ch;
            int used = entity(s + i, len - i, &ch, 1);
            if (used) {
                g_txt[g_txt_len++] = ch;
                i += used;
                continue;
            }
        }
        g_txt[g_txt_len++] = (unsigned char)s[i] < 32 ? ' ' : s[i];
        ++i;
        if (g_txt_len >= (int)sizeof(g_txt) - 1)
            break;
    }
    g_txt[g_txt_len] = 0;
    ps_text(ps, g_txt + out0, g_txt_len - out0);
}

static void html_parse(HParser *ps, const char *p, const char *end)
{
    int in_head = 0, in_title = 0, title0 = -1;
    while (p < end) {
        if (*p == '<') {
            /* doctype / declarations: <!DOCTYPE ...>, <![CDATA[...]> */
            if (p + 1 < end && p[1] == '!') {
                while (p < end && *p != '>')
                    ++p;
                if (p < end)
                    ++p;
                continue;
            }
            /* comment */
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
                p += 4;
                while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>'))
                    ++p;
                p = p + 2 < end ? p + 3 : end;
                continue;
            }
            const char *tg = p + 1;
            int close = 0;
            if (tg < end && *tg == '/') { close = 1; ++tg; }
            if (tg >= end || !xisalpha((unsigned char)*tg)) {
                ++p;
                continue;
            }
            const char *te = tg;
            while (te < end && xisalnum((unsigned char)*te))
                ++te;
            int tl = (int)(te - tg);
            const char *tagend = te;
            while (tagend < end && *tagend != '>')
                ++tagend;
            if (tagend >= end)
                break;
            char lname[12];
            int cl = tl > 11 ? 11 : tl;
            for (int i = 0; i < cl; ++i)
                lname[i] = (char)xlow((unsigned char)tg[i]);
            lname[cl] = 0;
#define TIS(s) (cl == xlen(s) && xeq(lname, s, cl))
            if (TIS("head")) {
                if (!close) in_head = 1;
                else in_head = 0;
                p = tagend + 1;
                continue;
            }
            if (TIS("title")) {
                if (!close) { in_title = 1; title0 = g_txt_len; }
                else if (in_title) {
                    in_title = 0;
                    int n = g_txt_len - title0;
                    if (n > 0 && g_title_len == 0) {
                        if (n > (int)sizeof(g_title) - 1)
                            n = sizeof(g_title) - 1;
                        for (int i = 0; i < n; ++i)
                            g_title[i] = g_txt[title0 + i];
                        g_title_len = n;
                    }
                    g_txt_len = title0;
                    g_txt[g_txt_len] = 0;
                }
                p = tagend + 1;
                continue;
            }
            if (TIS("style")) {
                if (!close) {
                    if (cssbuf_len < 0)
                        cssbuf_len = 0;
                    p = tagend + 1;
                    while (p + 7 < end && !(p[0] == '<' && p[1] == '/' &&
                          xlow((unsigned char)p[2]) == 's' &&
                          xlow((unsigned char)p[3]) == 't')) {
                        if (cssbuf_len < (int)sizeof(cssbuf) - 1)
                            cssbuf[cssbuf_len++] = *p;
                        ++p;
                    }
                    while (p < end && *p != '>')
                        ++p;
                    if (p < end)
                        ++p;
                    cssbuf[cssbuf_len] = 0;
#ifdef MD_DEBUG
                    fprintf(stderr, "[css] collected %d bytes\n", cssbuf_len);
#endif
                    css_parse(cssbuf);
                    cssbuf_len = -1;
#ifdef MD_DEBUG
                    fprintf(stderr, "[css] rules now %d\n", nrule);
#endif
                } else
                    p = tagend + 1;
                continue;
            }
            if (TIS("script")) {
                if (!close) {
                    const char *b = tagend + 1;
                    const char *q = b;
                    while (q + 7 < end && !(q[0] == '<' && q[1] == '/' &&
                          xlow((unsigned char)q[2]) == 's' &&
                          xlow((unsigned char)q[3]) == 'c'))
                        ++q;
                    if (g_host && g_host->chakra) {
                        int n = (int)(q - b);
                        if (n > 400)
                            n = 400;
                        /* copy + NUL (chakra takes a C string) */
                        static char sbuf[404];
                        for (int i = 0; i < n; ++i)
                            sbuf[i] = b[i];
                        sbuf[n] = 0;
                        g_host->chakra(sbuf);
                    }
                    p = (q + 7 < end) ? q : end;
                    /* skip to '>' */
                    while (p < end && *p != '>')
                        ++p;
                    if (p < end)
                        ++p;
                } else
                    p = tagend + 1;
                continue;
            }
            if (!close && (in_head || TIS("link") || TIS("meta") ||
                           TIS("base") || TIS("doctype"))) {
                p = tagend + 1;
                continue;
            }
            /* block vs inline */
            int tagid = TG_NONE;
            static const struct { const char *n; int t; } TAGN2[] = {
                {"p", TG_P}, {"h1", TG_H1}, {"h2", TG_H2}, {"h3", TG_H3},
                {"h4", TG_H4}, {"h5", TG_H5}, {"h6", TG_H6},
                {"div", TG_DIV}, {"span", TG_SPAN}, {"a", TG_A},
                {"b", TG_B}, {"i", TG_I}, {"u", TG_U}, {"em", TG_EM},
                {"strong", TG_STRONG}, {"code", TG_CODE}, {"pre", TG_PRE},
                {"br", TG_BR}, {"img", TG_IMG}, {"input", TG_INPUT},
                {"form", TG_FORM}, {"ul", TG_UL}, {"ol", TG_OL},
                {"li", TG_LI}, {"blockquote", TG_BLOCKQUOTE}, {"hr", TG_HR},
                {"table", TG_TABLE}, {"tr", TG_TR}, {"td", TG_TD},
                {"th", TG_TH}, {"dl", TG_DL}, {"dt", TG_DT}, {"dd", TG_DD},
                {"html", TG_HTML}, {"body", TG_BODY}, {"center", TG_CENTER},
                {"font", TG_FONT}, {"small", TG_SMALL}, {"big", TG_BIG},
                {"sub", TG_SUB}, {"sup", TG_SUP}, {"s", TG_S},
                {"strike", TG_STRIKE}, {"del", TG_DEL}, {"tt", TG_TT},
                {"mark", TG_MARK}, {"kbd", TG_KBD}, {"samp", TG_SAMP},
                {"cite", TG_CITE}, {"var", TG_VAR}, {"ins", TG_INS},
                {"select", TG_SELECT},
                {"option", TG_OPTION}, {"address", TG_ADDRESS},
                {"section", TG_SECTION}, {"article", TG_ARTICLE},
                {"header", TG_HEADER}, {"footer", TG_FOOTER},
                {"nav", TG_NAV}, {"main", TG_MAIN}, {"label", TG_LABEL},
            };
            for (unsigned k = 0; k < sizeof(TAGN2) / sizeof(TAGN2[0]); ++k)
                if (cl == xlen(TAGN2[k].n) && xeq(lname, TAGN2[k].n, cl)) {
                    tagid = TAGN2[k].t;
                    break;
                }
            if (tagid != TG_NONE && TG_IS_BLOCK[tagid]) {
                TagAttr at;
                attr_parse(te, tagend, &at);
                if (!close) {
                    int bi = ps_open_block(ps, tagid, &at);
                    if (bi >= 0 && tagid == TG_HR)
                        blks[bi].kind = BK_HR;
                    if (bi >= 0 && tagid == TG_PRE)
                        blks[bi].kind = BK_PRE;
                    /* table col alignment etc kept via attrs */
                    if (bi >= 0 && (tagid == TG_TD || tagid == TG_TH))
                        blks[bi].kind = BK_CELL;
                } else {
                    ps_close_block(ps, tagid);
                }
                p = tagend + 1;
                continue;
            }
            /* inline tag: delegate */
            const char *after = html_inline_tag(ps, p, end);
            p = after ? after : tagend + 1;
            continue;
        }
        /* text run: inside <title> it feeds the title buffer, not items */
        const char *q = p;
        while (q < end && *q != '<')
            ++q;
        if (q > p) {
            if (in_title) {
                for (const char *r = p; r < q; ++r)
                    if (!xisspace((unsigned char)*r) || g_txt_len == 0 ||
                        g_txt[g_txt_len - 1] != ' ')
                        g_txt[g_txt_len++] = *r;
                g_txt[g_txt_len] = 0;
            } else {
                ps_text_ent(ps, p, (int)(q - p));
            }
        }
        p = q;
    }
#undef TIS
}

/* =============== Markdown parser =============== */
/* link reference definition: "[id]: url" — pre-scan pass */
static void md_collect_refs(void)
{
    const char *p = g_src, *end = g_src + g_src_len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n')
            ++nl;
        int len = (int)(nl - p);
        if (len > 0 && p[len - 1] == '\r')
            --len;
        if (len > 2 && p[0] == '[' && nref < NREF) {
            int i = 1;
            while (i < len && p[i] != ']')
                ++i;
            if (i < len && i > 1 && i < 1 + NAME_CAP && p[i + 1] == ':') {
                int j = i + 2;
                while (j < len && (p[j] == ' ' || p[j] == '\t'))
                    ++j;
                int url0 = j;
                while (j < len && p[j] != ' ' && p[j] != '\t')
                    ++j;
                if (j > url0) {
                    MdRef *r = &refs[nref++];
                    int idl = i - 1;
                    if (idl > NAME_CAP - 1)
                        idl = NAME_CAP - 1;
                    for (int q = 0; q < idl; ++q)
                        r->id[q] = (char)xlow((unsigned char)p[1 + q]);
                    r->id[idl] = 0;
                    r->url = p + url0;
                    r->len = (short)(j - url0);
                }
            }
        }
        p = nl + 1;
    }
}

static const MdRef *md_find_ref(const char *id, int len)
{
    if (len <= 0)
        return 0;
    char buf[NAME_CAP];
    if (len >= NAME_CAP)
        len = NAME_CAP - 1;
    for (int i = 0; i < len; ++i)
        buf[i] = (char)xlow((unsigned char)id[i]);
    for (int i = 0; i < nref; ++i)
        if (xlen(refs[i].id) == len && xeq(refs[i].id, buf, len))
            return &refs[i];
    return 0;
}

/* forward: markdown inline scanner */
static void md_inline(HParser *ps, const char *s, int len, int depth);

/* find the end of a [ ... ] group with balanced brackets/escapes */
static int bracket_end(const char *s, int len, int start)
{
    int depth = 0;
    for (int i = start; i < len; ++i) {
        if (s[i] == '\\')
            ++i;
        else if (s[i] == '[')
            ++depth;
        else if (s[i] == ']') {
            --depth;
            if (!depth)
                return i;
        }
    }
    return -1;
}

/* find end of ( ... ) url group with nesting */
static int paren_end(const char *s, int len, int start)
{
    int depth = 0;
    for (int i = start; i < len; ++i) {
        if (s[i] == '\\')
            ++i;
        else if (s[i] == '(')
            ++depth;
        else if (s[i] == ')') {
            --depth;
            if (!depth)
                return i;
        }
    }
    return -1;
}

static void emit_escaped(HParser *ps, const char *s, int len)
{
    /* copy with backslash escapes resolved into the txt arena */
    int out0 = g_txt_len;
    for (int i = 0; i < len;) {
        if (s[i] == '\\' && i + 1 < len &&
            (s[i + 1] == '*' || s[i + 1] == '_' || s[i + 1] == '[' ||
             s[i + 1] == ']' || s[i + 1] == '(' || s[i + 1] == ')' ||
             s[i + 1] == '`' || s[i + 1] == '~' || s[i + 1] == '\\' ||
             s[i + 1] == '#' || s[i + 1] == '+' || s[i + 1] == '-' ||
             s[i + 1] == '!' || s[i + 1] == '<' || s[i + 1] == '>')) {
            g_txt[g_txt_len++] = s[i + 1];
            i += 2;
        } else {
            g_txt[g_txt_len++] = s[i++];
        }
        if (g_txt_len >= (int)sizeof(g_txt) - 1)
            break;
    }
    g_txt[g_txt_len] = 0;
    if (g_txt_len > out0)
        ps_text(ps, g_txt + out0, g_txt_len - out0);
}

static void md_link_core(HParser *ps, const char *s, int t0, int t1,
                         const char *url, int ulen)
{
    int li = link_add(url, ulen);
    unsigned char pf = ps_top(ps).flags;
    ps_push(ps, (unsigned char)(pf | MDTF_UL), 0, 0, (short)li);
    md_inline(ps, s + t0, t1 - t0, 3);
    ps_pop(ps);
}

static void md_inline(HParser *ps, const char *s, int len, int depth)
{
    int i = 0;
    while (i < len) {
        char c = s[i];
        /* code span first (highest precedence) */
        if (c == '`') {
            int j = i + 1;
            while (j < len && s[j] == '`')
                ++j;
            int ticks = j - i;
            /* find matching run */
            int k = j;
            for (;;) {
                while (k < len && s[k] != '`')
                    ++k;
                if (k >= len)
                    break;
                int k2 = k;
                while (k2 < len && s[k2] == '`')
                    ++k2;
                if (k2 - k == ticks) {
                    /* code text: trim one space each side */
                    int a = j, b = k;
                    if (b - a >= 2 && s[a] == ' ' && s[b - 1] == ' ' &&
                        s[a + 1] != ' ') {
                        ++a;
                        --b;
                    }
                    ISt st = ps_top(ps);
                    ps_push(ps, st.flags, 0x606060, 0, -1);
                    emit_escaped(ps, s + a, b - a);
                    ps_pop(ps);
                    i = k2;
                    break;
                }
                k = k2;
            }
            if (k < len || (i > 0 && s[i - 1] == '`')) {
                if (k >= len) { /* no closer: literal */
                    emit_escaped(ps, s + i, ticks);
                    i += ticks;
                }
                continue;
            }
            emit_escaped(ps, s + i, 1);
            ++i;
            continue;
        }
        if (c == '\\' && i + 1 < len) {
            emit_escaped(ps, s + i, 2);
            i += 2;
            continue;
        }
        /* image / link */
        if (c == '!' && i + 1 < len && s[i + 1] == '[') {
            int be = bracket_end(s, len, i + 1);
            if (be > 0) {
                int alt0 = i + 2, alt1 = be;
                if (be + 1 < len && s[be + 1] == '(') {
                    int pe = paren_end(s, len, be + 1);
                    if (pe > 0) {
                        int u0 = be + 2, u1 = pe;
                        while (u0 < u1 && s[u0] == ' ')
                            ++u0;
                        while (u1 > u0 && s[u1 - 1] == ' ')
                            --u1;
                        if (u1 > u0 && s[u0] == '<' && s[u1 - 1] == '>') { ++u0; --u1; }
                        int slot = img_slot(s + u0, u1 - u0);
                        if (slot >= 0) {
                            int idx = add_inl(ps, T_IMG);
                            if (idx >= 0) {
                                inls[idx].img = (short)slot;
                                inls[idx].len = 0; /* width/height auto */
                            }
                        }
                        i = pe + 1;
                        continue;
                    }
                } else if (be + 1 < len && s[be + 1] == '[') {
                    int be2 = bracket_end(s, len, be + 1);
                    if (be2 > 0) {
                        int r0 = be + 2, r1 = be2;
                        if (r1 == r0) { r0 = alt0; r1 = alt1; } /* collapsed */
                        const MdRef *r = md_find_ref(s + r0, r1 - r0);
                        if (r) {
                            int slot = img_slot(r->url, r->len);
                            if (slot >= 0) {
                                int idx = add_inl(ps, T_IMG);
                                if (idx >= 0)
                                    inls[idx].img = (short)slot;
                            }
                        }
                        i = be2 + 1;
                        continue;
                    }
                }
            }
            emit_escaped(ps, s + i, 1);
            ++i;
            continue;
        }
        if (c == '[') {
            int be = bracket_end(s, len, i);
            if (be > 0 && be + 1 < len && (s[be + 1] == '(' || s[be + 1] == '[')) {
                if (s[be + 1] == '(') {
                    int pe = paren_end(s, len, be + 1);
                    if (pe > 0) {
                        int u0 = be + 2, u1 = pe;
                        while (u0 < u1 && s[u0] == ' ')
                            ++u0;
                        while (u1 > u0 && s[u1 - 1] == ' ')
                            --u1;
                        if (u1 > u0 && s[u0] == '<' && s[u1 - 1] == '>') { ++u0; --u1; }
                        /* strip trailing "title" */
                        int u1b = u1;
                        while (u1b > u0 && s[u1b - 1] != '"')
                            --u1b;
                        if (u1b > u0 + 1 && u1b - 1 > u0 && s[u1 - 1] == '"')
                            u1 = u1b - 1;
                        if (u1 > u0) {
                            md_link_core(ps, s, i + 1, be, s + u0, u1 - u0);
                            i = pe + 1;
                            continue;
                        }
                    }
                } else {
                    int be2 = bracket_end(s, len, be + 1);
                    if (be2 > 0) {
                        int r0 = be + 2, r1 = be2;
                        if (r1 == r0) { r0 = i + 1; r1 = be; } /* collapsed */
                        const MdRef *r = md_find_ref(s + r0, r1 - r0);
                        if (r) {
                            md_link_core(ps, s, i + 1, be, r->url, r->len);
                            i = be2 + 1;
                            continue;
                        }
                    }
                }
            } else if (be > 0 && be + 1 < len && s[be + 1] == ':') {
                /* definition inside text: skip */
                i = be + 1;
                continue;
            }
            emit_escaped(ps, s + i, 1);
            ++i;
            continue;
        }
        /* autolink <http://x> */
        if (c == '<' && i + 1 < len && (s[i + 1] == 'h' || s[i + 1] == 'w')) {
            int j = i + 1;
            while (j < len && s[j] != '>' && s[j] != ' ' && s[j] != '<')
                ++j;
            if (j < len && s[j] == '>' && j - i > 8) {
                md_link_core(ps, s, i + 1, j, s + i + 1, j - i - 1);
                i = j + 1;
                continue;
            }
        }
        /* bare url */
        if ((c == 'h' && i + 7 <= len && xeq(s + i, "http://", 7) != 0) ||
            (c == 'h' && i + 8 <= len && xeq(s + i, "https://", 8) != 0)) {
            int j = i;
            while (j < len && s[j] != ' ' && s[j] != ')' && s[j] != '<' &&
                   s[j] != '"' && s[j] != '\'' && s[j] != ',' && s[j] != ';')
                ++j;
            while (j > i && (s[j - 1] == '.' || s[j - 1] == ':' || s[j - 1] == '!'))
                --j;
            if (j - i > 8) {
                md_link_core(ps, s, i, j, s + i, j - i);
                i = j;
                continue;
            }
        }
        /* emphasis: ** then * ; __ then _ (only at non-word edges for _) */
        if (c == '*' || c == '_') {
            int strong = (i + 1 < len && s[i + 1] == c) ? 1 : 0;
            int dlen = strong ? 2 : 1;
            if (c == '_' && !strong && i > 0 && xisalnum((unsigned char)s[i - 1]) &&
                i + 1 < len && xisalnum((unsigned char)s[i + 1])) {
                emit_escaped(ps, s + i, 1);
                ++i;
                continue;
            }
            int j = i + dlen;
            int found = -1;
            while (j + dlen <= len) {
                if (s[j] == '\\' && j + 1 < len) {
                    j += 2;
                    continue;
                }
                if (s[j] == c) {
                    if (strong) {
                        if (j + 1 < len && s[j + 1] == c) {
                            found = j;
                            break;
                        }
                    } else {
                        if (j + 1 >= len || s[j + 1] != c) {
                            found = j;
                            break;
                        }
                        ++j; /* skip ** while scanning for * */
                    }
                }
                ++j;
            }
            if (found > i + dlen && depth < 6) {
                ISt st = ps_top(ps);
                unsigned char nf = st.flags;
                if (c == '*')
                    nf |= strong ? MDTF_BOLD : MDTF_ITAL;
                else
                    nf |= strong ? MDTF_BOLD : MDTF_ITAL;
                ps_push(ps, nf, st.color, st.fdelta, st.link);
                md_inline(ps, s + i + dlen, found - i - dlen, depth + 1);
                ps_pop(ps);
                i = found + dlen;
                continue;
            }
            emit_escaped(ps, s + i, dlen);
            i += dlen;
            continue;
        }
        /* strikethrough ~~ */
        if (c == '~' && i + 1 < len && s[i + 1] == '~') {
            int j = i + 2;
            while (j + 1 < len && !(s[j] == '~' && s[j + 1] == '~'))
                ++j;
            if (j + 1 < len) {
                ISt st = ps_top(ps);
                ps_push(ps, (unsigned char)(st.flags | MDTF_STRIKE), st.color,
                        st.fdelta, st.link);
                md_inline(ps, s + i + 2, j - i - 2, depth + 1);
                ps_pop(ps);
                i = j + 2;
                continue;
            }
        }
        /* raw html inline tag */
        if (c == '<' && i + 1 < len &&
            (xisalpha((unsigned char)s[i + 1]) || s[i + 1] == '/')) {
            const char *after = html_inline_tag(ps, s + i, s + len);
            if (after) {
                i = (int)(after - s);
                continue;
            }
        }
        /* plain run: until next special char */
        int j = i + 1;
        while (j < len && s[j] != '*' && s[j] != '_' && s[j] != '`' &&
               s[j] != '[' && s[j] != '!' && s[j] != '~' && s[j] != '<' &&
               s[j] != '\\')
            ++j;
        /* include a bare http run start */
        emit_escaped(ps, s + i, j - i);
        i = j;
    }
}

/* md block structure */
typedef struct {
    int list_blk[4];  /* open list blocks per level */
    int last_li[4];   /* last li block per level */
    int list_lvl;
    int outer;        /* block to return to when all lists close */
} MdLists;

static void md_blocks(HParser *ps, const char *p, const char *end, MdLists *ls);

static int line_is_hr(const char *p, int len)
{
    if (len < 3)
        return 0;
    char c = p[0];
    if (c != '-' && c != '*' && c != '_')
        return 0;
    int n = 0;
    for (int i = 0; i < len; ++i) {
        if (p[i] == c)
            ++n;
        else if (p[i] != ' ' && p[i] != '\t')
            return 0;
    }
    return n >= 3;
}

static int li_marker(const char *p, int len, int *content_at, int *ordered,
                     int *number)
{
    if (len < 2)
        return 0;
    *number = 0;
    if (p[0] == '-' || p[0] == '+' || p[0] == '*') {
        if (p[1] != ' ' && p[1] != '\t')
            return 0;
        *content_at = 2;
        *ordered = 0;
        return 1;
    }
    if (p[0] >= '0' && p[0] <= '9') {
        int i = 0, n = 0;
        while (i < len && p[i] >= '0' && p[i] <= '9' && i < 8) {
            n = n * 10 + (p[i] - '0');
            ++i;
        }
        if (i < len && (p[i] == '.' || p[i] == ')') && i + 1 < len &&
            p[i + 1] == ' ') {
            *content_at = i + 2;
            *ordered = 1;
            *number = n;
            return 1;
        }
    }
    return 0;
}

static void md_close_lists(HParser *ps, MdLists *ls, int keep)
{
    while (ls->list_lvl > keep) {
        --ls->list_lvl;
        ls->list_blk[ls->list_lvl] = -1;
    }
    if (ls->list_lvl > 0)
        ps->cur = ls->last_li[ls->list_lvl - 1] >= 0
                      ? ls->last_li[ls->list_lvl - 1]
                      : ls->list_blk[ls->list_lvl - 1];
    else
        ps->cur = ls->outer;
}

/* open one table row of cells; returns cells parsed */
static void md_table_row(HParser *ps, int tb, const char *rp, int rl,
                         const unsigned char *aligns, int ncol)
{
    blk_new(tb, TG_TR, 0);
    int tr = blks[tb].last_child;
    (void)tr;
    /* find tr block: blk_new returned it; children attach to it */
    int ci = 1, col = 0;
    int parent = blks[tb].last_child;
    while (ci <= rl && col < ncol) {
        int ce = ci;
        while (ce < rl && rp[ce] != '|')
            ++ce;
        int td = blk_new(parent, TG_TD, BK_CELL);
        if (td >= 0) {
            blks[td].st.align = aligns[col];
            st_set(&blks[td].st, SM_ALIGN);
            int a = ci, b = ce;
            while (a < b && rp[a] == ' ')
                ++a;
            while (b > a && rp[b - 1] == ' ')
                --b;
            int save = ps->cur;
            ps->cur = td;
            md_inline(ps, rp + a, b - a, 0);
            ps->cur = save;
        }
        ci = ce + 1;
        ++col;
    }
}

static void md_blocks(HParser *ps, const char *p, const char *end, MdLists *ls)
{
    const char *lin = p;
    int para_open = 0; /* ps->cur is a TG_P we opened */
    int quote_blk = -1; /* open blockquote for consecutive > lines */
    int blank_seen = 0;
    while (lin < end) {
        const char *nl = lin;
        while (nl < end && *nl != '\n')
            ++nl;
        int len = (int)(nl - lin);
        if (len > 0 && lin[len - 1] == '\r')
            --len;
        const char *s = lin;
        lin = nl + 1;
        int indent = 0;
        while (s < lin && (*s == ' ' || *s == '\t') && indent < 16) {
            indent += *s == '\t' ? 4 : 1;
            ++s;
            --len;
        }
        /* blank line */
        if (len == 0) {
            if (para_open) {
                ps_close_block(ps, TG_P);
                para_open = 0;
            }
            quote_blk = -1;
            blank_seen = 1;
            continue;
        }
        /* fenced code */
        if (len >= 3 && (s[0] == '`' || s[0] == '~') &&
            (s[0] == s[1] && s[1] == s[2])) {
            char fc = s[0];
            if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
            md_close_lists(ps, ls, 0);
            quote_blk = -1;
            int bi = blk_new(ps->cur, TG_PRE, BK_PRE);
            if (bi >= 0) {
                int save_cur = ps->cur;
                ps->cur = bi;
                const char *q = lin;
                while (q < end) {
                    const char *qn = q;
                    while (qn < end && *qn != '\n')
                        ++qn;
                    int ql = (int)(qn - q);
                    if (ql > 0 && q[ql - 1] == '\r')
                        --ql;
                    if (ql >= 3 && q[0] == fc && q[1] == fc && q[2] == fc)
                        break;
                    int idx = add_inl(ps, T_TEXT);
                    if (idx >= 0) {
                        inls[idx].s = q;
                        inls[idx].len = (short)ql;
                    }
                    if (qn < end)
                        add_inl(ps, T_BR);
                    q = qn + 1;
                }
                ps->cur = save_cur;
                lin = q < end ? q : end;
                while (lin < end && *lin != '\n')
                    ++lin;
                if (lin < end)
                    ++lin;
            }
            continue;
        }
        /* ATX heading */
        if (s[0] == '#' && len > 1) {
            int lvl = 0;
            while (lvl < len && s[lvl] == '#')
                ++lvl;
            if (lvl <= 6 && lvl < len && s[lvl] == ' ') {
                if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
                md_close_lists(ps, ls, 0);
                quote_blk = -1;
                int t = TG_H1 + lvl - 1;
                int bi = ps_open_block(ps, t, 0);
                if (bi >= 0) {
                    int a = lvl + 1, b = len;
                    while (b > a && s[b - 1] == '#')
                        --b;
                    while (b > a && s[b - 1] == ' ')
                        --b;
                    md_inline(ps, s + a, b - a, 0);
                    ps_close_block(ps, (unsigned char)t);
                }
                continue;
            }
        }
        /* hr */
        if (line_is_hr(s, len)) {
            if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
            md_close_lists(ps, ls, 0);
            quote_blk = -1;
            blk_new(ps->cur, TG_HR, BK_HR);
            continue;
        }
        /* blockquote: consecutive > lines merge into one quote block */
        if (s[0] == '>') {
            if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
            md_close_lists(ps, ls, 0);
            int qi = 1;
            if (qi < len && s[qi] == ' ')
                ++qi;
            if (quote_blk < 0) {
                quote_blk = blk_new(ps->cur, TG_BLOCKQUOTE, BK_QUOTE);
                if (quote_blk >= 0) {
                    int save_cur = ps->cur;
                    ps->cur = quote_blk;
                    md_inline(ps, s + qi, len - qi, 0);
                    ps->cur = save_cur;
                }
            } else if (quote_blk < nblk) {
                int save_cur = ps->cur;
                ps->cur = quote_blk;
                add_inl(ps, T_BR);
                md_inline(ps, s + qi, len - qi, 0);
                ps->cur = save_cur;
            }
            continue;
        }
        /* list item */
        int cat = 0, ord = 0, num = 0;
        if (li_marker(s, len, &cat, &ord, &num)) {
            if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
            quote_blk = -1;
            blank_seen = 0;
            int lvl = indent / 2;
            if (lvl > 3)
                lvl = 3;
            md_close_lists(ps, ls, lvl + 1);
            while (ls->list_lvl < lvl + 1) {
                if (ls->list_lvl == 0)
                    ls->outer = ps->cur;
                int nlb = ps_open_block(ps, ord ? TG_OL : TG_UL, 0);
                if (nlb < 0)
                    break;
                blks[nlb].kind = BK_LIST;
                blks[nlb].level = (short)(ls->list_lvl);
                blks[nlb].marker = (unsigned char)(ord ? 1 : 0);
                ls->list_blk[ls->list_lvl] = nlb;
                ls->last_li[ls->list_lvl] = -1;
                ++ls->list_lvl;
            }
            int lbi = blk_new(ls->list_blk[lvl], TG_LI, BK_ITEM);
            if (lbi >= 0) {
                blks[lbi].marker = (unsigned char)(ord ? 1 : 0);
                blks[lbi].level = (short)num;
                ls->last_li[lvl] = (short)lbi;
                ps->cur = lbi;
            }
            md_inline(ps, s + cat, len - cat, 0);
            continue;
        }
        /* table: current line has | and next line is a separator */
        if (len > 2 && s[0] == '|') {
            const char *n2 = lin;
            int n2len = 0;
            int sep = 0;
            if (n2 < end) {
                const char *n2nl = n2;
                while (n2nl < end && *n2nl != '\n')
                    ++n2nl;
                n2len = (int)(n2nl - n2);
                if (n2len > 0 && n2[n2len - 1] == '\r')
                    --n2len;
                sep = n2len >= 3;
                for (int i = 0; sep && i < n2len; ++i)
                    if (n2[i] != '|' && n2[i] != ':' && n2[i] != '-' &&
                        n2[i] != ' ' && n2[i] != '\t')
                        sep = 0;
            }
            if (sep) {
                if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
                md_close_lists(ps, ls, 0);
                quote_blk = -1;
                int tb = ps_open_block(ps, TG_TABLE, 0);
                if (tb >= 0) {
                    unsigned char aligns[8];
                    int ncol = 0;
                    int i = 0;
                    while (i < n2len && ncol < 8) {
                        int l = 0, r = 0;
                        while (i < n2len && n2[i] == ' ')
                            ++i;
                        if (i < n2len && n2[i] == ':') { l = 1; ++i; }
                        while (i < n2len && n2[i] == '-')
                            ++i;
                        if (i < n2len && n2[i] == ':') { r = 1; ++i; }
                        while (i < n2len && n2[i] == ' ')
                            ++i;
                        if (i < n2len && n2[i] == '|')
                            ++i;
                        aligns[ncol++] = (unsigned char)(l && r ? 1 : r ? 2 : 0);
                        if (i >= n2len)
                            break;
                    }
                    md_table_row(ps, tb, s, len, aligns, ncol);
                    /* body rows */
                    const char *rp = lin;
                    while (rp < end) {
                        const char *rn = rp;
                        while (rn < end && *rn != '\n')
                            ++rn;
                        int rl = (int)(rn - rp);
                        if (rl > 0 && rp[rl - 1] == '\r')
                            --rl;
                        if (rl <= 0 || rp[0] != '|')
                            break;
                        md_table_row(ps, tb, rp, rl, aligns, ncol);
                        rp = rn + 1;
                    }
                    lin = rp;
                    ps->cur = tb;
                    ps_close_block(ps, TG_TABLE);
                }
                continue;
            }
        }
        /* html block: line begins with a block-level tag */
        if (s[0] == '<' && len > 3 && xisalpha((unsigned char)s[1])) {
            char tname[12];
            int tl = 0;
            const char *q = s + 1;
            while (q < s + len && xisalnum((unsigned char)*q) && tl < 11)
                tname[tl++] = *q++;
            tname[tl] = 0;
            static const char *BT[] = {"div", "p", "table", "ul", "ol",
                                       "center", "blockquote", "pre", "h1",
                                       "h2", "h3", "dl", "section", 0};
            int is_blk = 0;
            for (int k = 0; BT[k]; ++k)
                if (xeq(tname, BT[k], tl))
                    is_blk = 1;
            if (is_blk) {
                if (para_open) { ps_close_block(ps, TG_P); para_open = 0; }
                md_close_lists(ps, ls, 0);
                quote_blk = -1;
                const char *hb = s;
                const char *he = hb;
                int blank = 0;
                while (he < end && !blank) {
                    const char *hn = he;
                    while (hn < end && *hn != '\n')
                        ++hn;
                    int hl = (int)(hn - he);
                    int nonblank = 0;
                    for (int i = 0; i < hl; ++i)
                        if (he[i] != ' ' && he[i] != '\t' && he[i] != '\r')
                            nonblank = 1;
                    if (!nonblank)
                        blank = 1;
                    else
                        he = hn < end ? hn + 1 : end;
                }
                html_parse(ps, hb, he);
                lin = he;
                continue;
            }
        }
        /* setext heading: === or --- under a paragraph */
        if (para_open && len >= 2 &&
            (s[0] == '=' || (s[0] == '-' && s[1] == '-'))) {
            char c0 = s[0];
            int all = 1;
            for (int i = 0; i < len; ++i)
                if (s[i] != c0 && s[i] != ' ')
                    all = 0;
            if (all) {
                int pi = ps->cur;
                blks[pi].tag = (unsigned char)(c0 == '=' ? TG_H1 : TG_H2);
                ps_close_block(ps, blks[pi].tag);
                para_open = 0;
                continue;
            }
        }
        /* paragraph after a closed list: leave the list first */
        if (blank_seen && ls->list_lvl > 0 && indent < 2) {
            md_close_lists(ps, ls, 0);
        }
        blank_seen = 0;
        /* paragraph continuation / start */
        if (!para_open) {
            if (quote_blk >= 0) {
                /* continuation of a quote paragraph */
                int save_cur = ps->cur;
                ps->cur = quote_blk;
                md_inline(ps, s, len, 0);
                ps->cur = save_cur;
                continue;
            }
            int pi = ps_open_block(ps, TG_P, 0);
            if (pi >= 0)
                para_open = 1;
        }
        /* hard break: two trailing spaces */
        int hard = 0;
        if (len >= 2 && s[len - 1] == ' ' && s[len - 2] == ' ')
            hard = 1;
        int tl2 = len;
        while (tl2 > 0 && s[tl2 - 1] == ' ')
            --tl2;
        md_inline(ps, s, tl2, 0);
        if (hard)
            add_inl(ps, T_BR);
        else if (lin < end)
            ps_text(ps, " ", 1);
    }
    if (para_open)
        ps_close_block(ps, TG_P);
    md_close_lists(ps, ls, 0);
}

/* =============== style resolution =============== */
static const short FONT_H[] = {9, 11, 12, 14, 18, 22};
static int map_font(int h)
{
    if (h <= FONT_H[0])
        return FONT_H[0];
    int best = FONT_H[0], bd = 9999;
    for (unsigned i = 0; i < sizeof(FONT_H) / sizeof(FONT_H[0]); ++i) {
        int d = FONT_H[i] - h;
        if (d < 0)
            d = -d;
        if (d < bd) {
            best = FONT_H[i];
            bd = d;
        }
    }
    return best;
}

static int resolve_len(short v10, unsigned char unit, int ctx_px, int font_px)
{
    switch (unit) {
    case U_EM: return v10 * font_px / 10;
    case U_PCT: return v10 * ctx_px / 1000;
    default: return v10 / 10; /* px or unitless */
    }
}

/* UA defaults + cascade + inherit -> RSt. par = parent resolved style. */
static void resolve_style(int bi, int cw, const RSt *par, RSt *r)
{
    Blk *b = &blks[bi];
    int base_fs = par ? par->fs : 14;
    for (int i = 0; i < (int)sizeof(RSt); ++i)
        ((unsigned char *)r)[i] = 0;
    r->fg = par ? par->fg : 0x000000;
    r->align = par ? par->align : AL_LEFT;
    r->weight = par ? par->weight : 0;
    r->fstyle = par ? par->fstyle : 0;
    r->fs = (short)base_fs;
    r->lh = par ? par->lh : 0;
    r->w = r->h = -1;
    r->bg = r->bdc = UNSET;
    r->disp = TG_IS_BLOCK[b->tag] ? D_BLOCK : D_INLINE;
    if (b->tag == TG_LI)
        r->disp = D_LIST_ITEM;
    if (b->kind == BK_CELL)
        r->disp = D_TABLE_CELL;
    /* UA box defaults */
    switch (b->tag) {
    case TG_P: r->m[0] = 6; r->m[2] = 6; break;
    case TG_H1: r->m[0] = 12; r->m[2] = 6; r->fs = 22; r->weight = 1; break;
    case TG_H2: r->m[0] = 10; r->m[2] = 5; r->fs = 18; r->weight = 1; break;
    case TG_H3: r->m[0] = 10; r->m[2] = 4; r->fs = 16; r->weight = 1; break;
    case TG_H4: r->m[0] = 8; r->m[2] = 4; r->fs = 14; r->weight = 1; break;
    case TG_H5: case TG_H6: r->m[0] = 8; r->m[2] = 4; r->fs = 12; r->weight = 1; break;
    case TG_UL: case TG_OL: r->m[0] = 4; r->m[2] = 4; r->p[3] = 20; break;
    case TG_LI: r->m[0] = 2; r->m[2] = 2; break;
    case TG_BLOCKQUOTE: r->m[0] = 4; r->m[2] = 4; r->p[3] = 14; break;
    case TG_PRE: r->m[0] = 6; r->m[2] = 6; r->p[0] = r->p[1] = r->p[2] = r->p[3] = 6;
        r->bg = 0xF0F0F0; r->mono = 1; r->fs = 11; break;
    case TG_HR: r->m[0] = 8; r->m[2] = 8; break;
    case TG_TABLE: r->m[0] = 6; r->m[2] = 6; break;
    case TG_TD: case TG_TH: r->p[0] = r->p[1] = r->p[2] = r->p[3] = 4; break;
    case TG_DT: r->m[0] = 6; break;
    case TG_DD: r->m[0] = 2; r->p[3] = 22; break;
    case TG_A: r->fg = 0xAD4506; break; /* #0645AD */
    case TG_FORM: r->m[0] = r->m[2] = 2; break;
    case TG_ADDRESS: r->fstyle = 1; break;
    case TG_SMALL: r->fs = (short)(base_fs - 2); break;
    case TG_BIG: r->fs = (short)(base_fs + 2); break;
    case TG_CODE: case TG_TT: case TG_KBD: case TG_SAMP:
        r->mono = 1; break;
    case TG_S: case TG_STRIKE: case TG_DEL: r->deco = 1; break;
    default: break;
    }
    if (b->kind == BK_PRE) {
        r->mono = 1;
        r->bg = 0xF0F0F0;
        r->p[0] = r->p[1] = r->p[2] = r->p[3] = 6;
    }
    if (b->kind == BK_QUOTE) {
        r->p[3] = 14;
        r->m[0] = 4;
        r->m[2] = 4;
    }
    if (b->kind == BK_CELL) {
        r->p[0] = r->p[1] = r->p[2] = r->p[3] = 4;
    }
    if (b->tag == TG_LI) {
        r->mark = b->marker ? 1 : 0;
        for (int a = b->parent; a > 0 && a < nblk; a = blks[a].parent)
            if (blks[a].tag == TG_OL) {
                r->mark = 1;
                break;
            }
    }

    /* author cascade (rules + attrs already merged into b->st) */
    const CssSt *st = &b->st;
    if (st->set & (1ull << SM_FS)) {
        if (st->fsu == U_EM)
            r->fs = (short)(st->fs * base_fs / 10);
        else if (st->fsu == U_PCT)
            r->fs = (short)(st->fs * base_fs / 1000);
        else
            r->fs = (short)(st->fs / 10);
    }
    if (st->set & (1ull << SM_LH)) {
        if (st->lhu == U_EM || st->lhu == U_NUM)
            r->lh = (short)(st->lh * r->fs / 10);
        else if (st->lhu == U_PCT)
            r->lh = (short)(st->lh * r->fs / 1000);
        else
            r->lh = (short)(st->lh / 10);
    }
    if (st->set & (1ull << SM_FG))
        r->fg = st->fg;
    if (st->set & (1ull << SM_BG))
        r->bg = st->bg;
    if (st->set & (1ull << SM_BDC))
        r->bdc = st->bdc;
    if (st->set & (1ull << SM_DISP))
        r->disp = st->disp;
    if (st->set & (1ull << SM_FLT))
        r->flt = st->flt;
    if (st->set & (1ull << SM_CLR))
        r->clr = st->clr;
    if (st->set & (1ull << SM_ALIGN))
        r->align = st->align;
    if (st->set & (1ull << SM_WEIGHT))
        r->weight = st->weight;
    if (st->set & (1ull << SM_FSTYLE))
        r->fstyle = st->fstyle;
#define APPLY_GROUP(field, setbit, out)                                    \
    do {                                                                   \
        if (st->set & (1ull << setbit))                                    \
            r->out = (short)resolve_len(LENV(st->field), LENU(st->field),  \
                                        cw, r->fs);                        \
    } while (0)
    APPLY_GROUP(m[0], SM_MT, m[0]);
    APPLY_GROUP(m[1], SM_MR, m[1]);
    APPLY_GROUP(m[2], SM_MB, m[2]);
    APPLY_GROUP(m[3], SM_ML, m[3]);
    APPLY_GROUP(p[0], SM_PT, p[0]);
    APPLY_GROUP(p[1], SM_PR, p[1]);
    APPLY_GROUP(p[2], SM_PB, p[2]);
    APPLY_GROUP(p[3], SM_PL, p[3]);
    APPLY_GROUP(bw[0], SM_BT, bw[0]);
    APPLY_GROUP(bw[1], SM_BR, bw[1]);
    APPLY_GROUP(bw[2], SM_BB, bw[2]);
    APPLY_GROUP(bw[3], SM_BL, bw[3]);
    if (st->set & (1ull << SM_BNONE) && st->bnone)
        r->bw[0] = r->bw[1] = r->bw[2] = r->bw[3] = 0;
    if (st->set & (1ull << SM_W)) {
        if (st->wu == U_AUTO)
            r->w = -1;
        else {
            r->w = (short)resolve_len(st->w, st->wu, cw, r->fs);
            if (r->w < 4)
                r->w = 4;
        }
    }
    if (st->set & (1ull << SM_H)) {
        if (st->hu == U_AUTO)
            r->h = -1;
        else
            r->h = (short)resolve_len(st->h, st->hu, cw, r->fs);
    }
    /* mark: borders imply a colour default (black) */
    if (r->bw[0] || r->bw[1] || r->bw[2] || r->bw[3]) {
        if (r->bdc == UNSET)
            r->bdc = 0x000000;
    }
    r->fh = (short)map_font(r->fs);

    /* reader mode: strip chrome, normalize typography */
    if (g_reader && bi != 0) {
        r->bg = UNSET;
        r->bw[0] = r->bw[1] = r->bw[2] = r->bw[3] = 0;
        r->bdc = UNSET;
        r->w = r->h = -1;
        r->flt = FL_NONE;
        r->clr = CL_NONE;
        if (b->tag != TG_TABLE && b->tag != TG_TR && b->kind != BK_CELL)
            r->align = AL_LEFT;
        r->fg = 0x24231D;
        if (b->kind != BK_PRE && b->tag != TG_PRE) {
            r->fs = b->tag == TG_H1 ? 24 : b->tag == TG_H2 ? 20
                 : b->tag == TG_H3 ? 17 : b->tag == TG_H4 ? 16
                 : b->tag == TG_H5 || b->tag == TG_H6 ? 15 : 14;
            r->fh = (short)map_font(r->fs);
            r->lh = b->tag == TG_P ? 19 : 0;
            r->m[1] = r->m[3] = 0;
            r->p[0] = r->p[1] = r->p[2] = r->p[3] = 0;
            if (b->tag == TG_P) { r->m[0] = 7; r->m[2] = 9; }
            if (b->tag >= TG_H1 && b->tag <= TG_H6) { r->m[0] = 14; r->m[2] = 6; }
        }
        if (b->tag == TG_PRE || b->kind == BK_PRE) {
            r->bg = 0xEFEAE0;
            r->p[0] = r->p[2] = 8;
            r->p[1] = r->p[3] = 6;
        }
    }
}

/* =============== layout =============== */
static int op_fill(int x, int y, int w, int h, unsigned c)
{
    if (nops >= NOPS || w <= 0 || h <= 0)
        return -1;
    POp *o = &ops[nops];
    for (int i = 0; i < (int)sizeof(POp); ++i)
        ((unsigned char *)o)[i] = 0;
    o->k = PO_FILL;
    o->x = (short)x; o->y = (short)y; o->w = (short)w; o->h = (short)h;
    o->c = c;
    return nops++;
}
static int op_text(int x, int y, const char *s, int len, unsigned c, int fh,
                   int flags, int link)
{
    if (nops >= NOPS || len <= 0)
        return -1;
    POp *o = &ops[nops];
    for (int i = 0; i < (int)sizeof(POp); ++i)
        ((unsigned char *)o)[i] = 0;
    o->k = PO_TEXT;
    o->x = (short)x; o->y = (short)y;
    o->s = s; o->len = (short)len;
    o->c = c; o->fh = (unsigned char)fh; o->f = (unsigned char)flags;
    o->link = (short)link;
    o->w = g_host && g_host->text_w ? (short)g_host->text_w(s, len, fh) : len * fh / 2;
    o->h = (short)fh;
    return nops++;
}
static void op_input(int x, int y, int w, int h, unsigned char kind,
                     const char *val, int len, unsigned c, int checked, int fh)
{
    if (nops >= NOPS)
        return;
    POp *o = &ops[nops++];
    for (int i = 0; i < (int)sizeof(POp); ++i)
        ((unsigned char *)o)[i] = 0;
    o->k = PO_INPUT;
    o->x = (short)x; o->y = (short)y; o->w = (short)w; o->h = (short)h;
    o->f = kind;
    o->s = val;
    o->len = (short)len;
    o->c = c;
    o->img = (short)(checked ? 1 : 0);
    o->fh = (unsigned char)fh;
}

/* float queries */
static void float_band(int y0, int y1, int cx, int cxr, int *lx0, int *rx1)
{
    int l = cx, r = cxr;
    for (int i = 0; i < nflt; ++i) {
        Flt *f = &flts[i];
        if (f->yb <= y0 || f->yt >= y1)
            continue;
        if (f->side == FL_LEFT) {
            if (f->xr > l)
                l = f->xr;
        } else if (f->xl < r)
            r = f->xl;
    }
    *lx0 = l;
    *rx1 = r;
}
static int clear_y(int y, int clr)
{
    for (int i = 0; i < nflt; ++i) {
        Flt *f = &flts[i];
        if (clr == CL_BOTH || (clr == CL_LEFT && f->side == FL_LEFT) ||
            (clr == CL_RIGHT && f->side == FL_RIGHT)) {
            if (f->yb > y)
                y = f->yb;
        }
    }
    return y;
}

static int layout_children(int bi, int cx, int cy, int cw, int depth);
static int layout_block_child(int bi, int cx, int y, int cw, RSt *r, int depth);
static void layout_float(int bi, int cx, int y, int cw, RSt *r);

/* measure a float's auto height: scratch layout, roll back ops/floats */
static int float_measure(int bi, int content_w, RSt *r)
{
    int save_ops = nops, save_flt = nflt;
    int save_pw = g_page_w, save_ph = g_page_h;
    g_page_w = 0;
    g_page_h = 0;
    RSt mr = *r;
    mr.w = (short)content_w;
    int bottom = layout_block_child(bi, 0, 0, content_w, &mr, 5);
    nops = save_ops;
    nflt = save_flt;
    g_page_w = save_pw;
    g_page_h = save_ph;
    return bottom - r->m[0];
}

static void layout_float(int bi, int cx, int y, int cw, RSt *r)
{
    int chrome_lr = r->p[3] + r->p[1] + r->bw[3] + r->bw[1];
    int fw = r->w >= 0 ? r->w + chrome_lr : (cw - chrome_lr) * 2 / 5;
    if (fw > cw)
        fw = cw;
    int outer_w = r->m[3] + fw + r->m[1];
    int fh_ = r->h;
    if (fh_ < 0)
        fh_ = float_measure(bi, r->w >= 0 ? r->w : fw - chrome_lr, r) +
              r->p[0] + r->p[2] + r->bw[0] + r->bw[2];
    if (fh_ < 4)
        fh_ = 4;
#ifdef MD_DEBUG2
    fprintf(stderr, "[flt] bi=%d tag=%d fw=%d outer=%d chrome=%d w=%d m=%d,%d y=%d cw=%d\n",
            bi, blks[bi].tag, fw, outer_w, chrome_lr, r->w, r->m[3], r->m[1],
            y, cw);
#endif
    for (int tries = 0; tries < 10; ++tries) {
        int l = cx, rr = cx + cw;
        float_band(y, y + 4, cx, cx + cw, &l, &rr);
        int fx = r->flt == FL_LEFT ? l : rr - outer_w;
        if (fx >= cx && fx + outer_w <= cx + cw) {
            RSt fr = *r;
            fr.w = (short)(fw - chrome_lr);
            int bottom = layout_block_child(bi, fx, y, cw, &fr, 4);
            if (nflt < NFLT) {
                Flt *f = &flts[nflt++];
                f->side = r->flt;
                f->xl = (short)fx;
                f->xr = (short)(fx + outer_w);
                f->yt = (short)y;
                f->yb = (short)(bottom + r->m[2]);
            }
            if (fx + outer_w > g_page_w)
                g_page_w = fx + outer_w;
            if (bottom + r->m[2] > g_page_h)
                g_page_h = bottom + r->m[2];
            return;
        }
        int drop = y;
        for (int i = 0; i < nflt; ++i)
            if (flts[i].yt < y + 4 && flts[i].yb > drop &&
                ((r->flt == FL_LEFT && flts[i].xr > cx) ||
                 (r->flt == FL_RIGHT && flts[i].xl < cx + cw)))
                drop = flts[i].yb;
        if (drop <= y)
            ++drop;
        y = drop;
    }
}

/* marker text ring (list numbering) */
static char markbuf[24][10];
static int markidx;
static const char *marker_text(int n)
{
    char *b = markbuf[markidx];
    markidx = (markidx + 1) % 24;
    char tmp[8];
    int i = 0;
    if (n <= 0)
        n = 1;
    while (n && i < 7) {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    }
    int j = 0;
    while (i)
        b[j++] = tmp[--i];
    b[j++] = '.';
    b[j] = 0;
    return b;
}

/* li marker emission */
static void emit_marker(int bi, RSt *r, int bx, int by)
{
    if (r->mark) {
        int n = blks[bi].level;
        const char *t = marker_text(n);
        op_text(bx - 22, by, t, xlen(t), r->fg, r->fh, 0, -1);
    } else {
        op_fill(bx - 12, by + r->fh / 2 - 1, 4, 5, r->fg);
    }
}

int layout_inline(int bi, int cx, int cy, int cw);

/* generic in-flow block child layout. emits box ops AROUND children:
 * bg/border ops are reserved before content and patched after. */
static int layout_block_child(int bi, int cx, int y, int cw, RSt *r, int depth)
{
    Blk *b = &blks[bi];
    if (r->clr)
        y = clear_y(y, r->clr);
    /* CSS1 box model: width/height set the CONTENT box */
    int chrome_lr = r->p[3] + r->p[1] + r->bw[3] + r->bw[1];
    int content_w = r->w >= 0 ? r->w : cw - r->m[3] - r->m[1] - chrome_lr;
    if (content_w < 2)
        content_w = 2;
    int bw = content_w + chrome_lr;
    int bx = cx + r->m[3];
    int by = y + r->m[0];
#ifdef MD_DEBUG2
    fprintf(stderr, "[blk] bi=%d tag=%d by=%d m0=%d bw0=%d p0=%d cwid=%d chrome=%d\n",
            bi, blks[bi].tag, by, r->m[0], r->bw[0], r->p[0], content_w,
            chrome_lr);
#endif
    int nb = (r->bw[0] | r->bw[1] | r->bw[2] | r->bw[3]) != 0;
    unsigned bc = r->bdc == UNSET ? 0x000000 : r->bdc;
    /* reserve: bg, 4 border sides */
    int opbg = -1, opb[4] = {-1, -1, -1, -1};
    if (r->bg != UNSET && bi != 0) {
        opbg = op_fill(bx, by, bw, 1, r->bg);
#ifdef MD_DEBUG2
        fprintf(stderr, "[bg] bi=%d tag=%d x=%d y=%d w=%d c=%06x\n", bi,
                blks[bi].tag, bx, by, bw, r->bg);
#endif
    }
    if (nb && bi != 0) {
        if (r->bw[0]) opb[0] = op_fill(bx, by, bw, r->bw[0], bc);
        if (r->bw[1]) opb[1] = op_fill(bx, by, r->bw[1], 1, bc);
        if (r->bw[2]) opb[2] = op_fill(bx, by, bw, r->bw[2], bc);
        if (r->bw[3]) opb[3] = op_fill(bx, by, r->bw[3], 1, bc);
    }
    int ibox = bx + r->bw[3] + r->p[3];
    int jbox = by + r->bw[0] + r->p[0];
    int iw = bw - r->bw[3] - r->bw[1] - r->p[3] - r->p[1];
    if (iw < 2)
        iw = 2;
    int bot = jbox;
    if (b->kind == BK_HR || b->tag == TG_HR) {
        int ly = by + 2;
        op_fill(bx + 4, ly, bw - 8, 2, 0xAAAAAA);
        bot = ly + 4;
    } else {
        if (b->ninl > 0) {
            if (r->mark || b->tag == TG_LI)
                emit_marker(bi, r, ibox, jbox);
            bot = layout_inline(bi, ibox, jbox, iw);
        }
        if (b->first_child >= 0) {
            int cb = layout_children(bi, ibox, bot, iw, depth + 1);
            if (cb > bot)
                bot = cb;
        }
    }
    int content_h = bot - jbox;
    if (r->h >= 0 && r->h > content_h)
        content_h = r->h;
    int used = content_h + r->p[2] + r->bw[2];
    int total = used + r->bw[0] + r->bw[2];
    /* patch reserved ops now that the height is known */
    if (opbg >= 0)
        ops[opbg].h = (short)total;
    if (opb[0] >= 0)
        ops[opb[0]].h = (short)r->bw[0];
    if (opb[1] >= 0) {
        ops[opb[1]].h = (short)total;
        ops[opb[1]].x = (short)(bx + bw - r->bw[1]);
    }
    if (opb[2] >= 0) {
        ops[opb[2]].y = (short)(by + total - r->bw[2]);
        ops[opb[2]].h = (short)r->bw[2];
    }
    if (opb[3] >= 0)
        ops[opb[3]].h = (short)total;
    int bottom = by + total;
    if (bx + bw + r->m[1] > g_page_w)
        g_page_w = bx + bw + r->m[1];
    if (bottom + r->m[2] > g_page_h)
        g_page_h = bottom + r->m[2];
    return bottom;
}

/* table layout: rows of cells, equal column widths */
static int layout_table(int bi, int cx, int cy, int cw, RSt *r, int depth)
{
    int bx = cx + r->m[3];
    int bw = r->w >= 0 ? r->w : cw - r->m[3] - r->m[1];
    int by = cy + r->m[0];
    if (r->bg != UNSET)
        op_fill(bx, by, bw, 2, r->bg); /* placeholder patched below */
    int bg_op = nops - 1;
    int y = by;
    int first_row = 1;
    for (int tr = blks[bi].first_child; tr >= 0; tr = blks[tr].next_sib) {
        if (blks[tr].tag != TG_TR)
            continue;
        int ncol = 0;
        for (int td = blks[tr].first_child; td >= 0; td = blks[td].next_sib)
            ++ncol;
        if (!ncol)
            continue;
        int colw = bw / ncol;
        int rh = 0;
        int ci = 0;
        for (int td = blks[tr].first_child; td >= 0; td = blks[td].next_sib) {
            RSt cr;
            resolve_style(td, colw, r, &cr);
            rs_[td] = cr;
            int tx = bx + ci * colw;
            cr.bw[0] = cr.bw[1] = cr.bw[2] = cr.bw[3] = 1;
            cr.bdc = 0xBBBBBB;
            if (g_reader)
                cr.bdc = 0xD8D2C6;
            if (first_row && blks[td].tag == TG_TD)
                cr.weight = 1;
            int bot = layout_block_child(td, tx, y, colw, &cr, depth);
            if (bot - y > rh)
                rh = bot - y;
            ++ci;
        }
        y += rh;
        first_row = 0;
    }
    ops[bg_op].h = (short)(y - by);
    if (bx + bw > g_page_w)
        g_page_w = bx + bw;
    if (y + r->m[2] > g_page_h)
        g_page_h = y + r->m[2];
    return y;
}

static int layout_children(int bi, int cx, int cy, int cw, int depth)
{
    if (depth > 8)
        return cy;
    int y = cy;
    for (int c = blks[bi].first_child; c >= 0; c = blks[c].next_sib) {
        RSt r;
        resolve_style(c, cw, &rs_[bi], &r);
        rs_[c] = r;
        if (r.disp == D_NONE)
            continue;
        if (r.flt && c != 0) {
            layout_float(c, cx, y, cw, &r);
            continue;
        }
        if (blks[c].tag == TG_TABLE)
            y = layout_table(c, cx, y, cw, &r, depth);
        else
            y = layout_block_child(c, cx, y, cw, &r, depth);
    }
    return y;
}

/* word-wrap an inline flow. returns bottom y.
 * A "line" collects op indices; alignment shifts them at flush time. */
typedef struct {
    int ops_idx[30];
    int n;
    int w;
    int y, lh, a0, a1;
} LineBuf;

static void line_flush(LineBuf *ln, const RSt *r)
{
    if (!ln->n)
        return;
    int dx = 0;
    if (r->align == AL_CENTER)
        dx = (ln->a1 - ln->a0 - ln->w) / 2;
    else if (r->align == AL_RIGHT)
        dx = ln->a1 - ln->a0 - ln->w;
    if (dx < 0)
        dx = 0;
    for (int i = 0; i < ln->n; ++i)
        ops[ln->ops_idx[i]].x = (short)(ops[ln->ops_idx[i]].x + dx);
    ln->n = 0;
    ln->w = 0;
}

int layout_inline(int bi, int cx, int cy, int cw)
{
    RSt *r = &rs_[bi];
    int nowrap = r->mono && (blks[bi].kind == BK_PRE || blks[bi].tag == TG_PRE);
    int def_lh = r->lh > 0 ? r->lh : r->fh + 3;
    LineBuf ln;
    ln.n = 0;
    ln.w = 0;
    int y = cy;
    int lh = def_lh;
    int x = cx;
    int any = 0;
    float_band(y, y + lh, cx, cx + cw, &ln.a0, &ln.a1);

    for (int ii = blks[bi].inl; ii >= 0 && ii < blks[bi].inl + blks[bi].ninl;
         ++ii) {
        Inl *it = &inls[ii];
        if (it->kind == T_BR) {
            y += lh;
            x = cx;
            lh = def_lh;
            float_band(y, y + lh, cx, cx + cw, &ln.a0, &ln.a1);
            if (x < ln.a0)
                x = ln.a0;
            continue;
        }
        if (it->kind == T_IMG || it->kind == T_INPUT) {
            int iw, ih;
            int fh = r->fh;
            if (it->kind == T_IMG) {
                iw = it->w > 0 ? it->w : 64;
                Img *im = it->img >= 0 ? &imgs[it->img] : 0;
                if (im && !im->len && g_host && g_host->read_file) {
                    im->len = (unsigned)g_host->read_file(
                        im->name, im->data, IMG_CAP);
                    if (im->len >= 9 && im->data[4] == 0x60) {
                        im->vw = (short)((im->data[5] << 8) | im->data[6]);
                        im->vh = (short)((im->data[7] << 8) | im->data[8]);
                        if (im->vw <= 0 || im->vh <= 0 || im->vw > 4096 ||
                            im->vh > 4096)
                            im->vw = im->vh = -1;
                    }
                }
                if (im && im->vw > 0 && im->vh > 0)
                    ih = it->h > 0 ? it->h : iw * im->vh / im->vw;
                else
                    ih = it->h > 0 ? it->h : iw;
                if (iw > cw) {
                    ih = ih * cw / iw;
                    iw = cw;
                }
            } else {
                if (it->inp == IN_RADIO || it->inp == IN_CHECK) {
                    iw = ih = 13;
                } else if (it->inp == IN_SUBMIT) {
                    iw = (g_host && g_host->text_w && it->len
                              ? g_host->text_w(it->s, it->len, r->fh)
                              : it->len * 6) + 18;
                    ih = 22;
                } else {
                    iw = 90;
                    ih = 18;
                }
            }
            if (x > ln.a0 && x + iw > ln.a1) {
                line_flush(&ln, r);
                y += lh;
                lh = def_lh;
                float_band(y, y + lh, cx, cx + cw, &ln.a0, &ln.a1);
                x = ln.a0;
            }
            int iy = y + lh - ih;
            if (it->kind == T_IMG) {
                if (nops < NOPS && it->img >= 0) {
                    POp *o = &ops[nops++];
                    for (int q = 0; q < (int)sizeof(POp); ++q)
                        ((unsigned char *)o)[q] = 0;
                    o->k = PO_SVF;
                    o->x = (short)x; o->y = (short)iy;
                    o->w = (short)iw; o->h = (short)ih;
                    o->img = it->img;
                    if (ln.n < 30)
                        ln.ops_idx[ln.n++] = nops - 1;
                }
            } else {
                op_input(x, iy, iw, ih, it->inp, it->s, it->len, r->fg,
                         it->h, fh);
                if (nops > 0 && ln.n < 30)
                    ln.ops_idx[ln.n++] = nops - 1;
            }
            if (ih > lh) {
                lh = ih + 1;
                float_band(y, y + lh, cx, cx + cw, &ln.a0, &ln.a1);
            }
            x += iw + 3;
            ln.w = x - ln.a0;
            any = 1;
            continue;
        }
        /* text words */
        int fh = r->fh;
        if (it->fdelta)
            fh = map_font(r->fs + it->fdelta);
        unsigned col = it->color ? it->color : r->fg;
        if (it->link >= 0 && !it->color)
            col = 0xAD4506; /* #0645AD link blue */
        int flags = it->flags;
        if (r->weight)
            flags |= MDTF_BOLD;
        if (r->fstyle)
            flags |= MDTF_ITAL;
        const char *s = it->s;
        int len = it->len;
        int i = 0;
        while (i < len) {
            while (i < len && s[i] == ' ')
                ++i;
            int w0 = i;
            while (i < len && s[i] != ' ')
                ++i;
            if (i == w0)
                break;
            int wlen = i - w0;
            int lead = 0;
            if (ln.n) {
                lead = g_host && g_host->text_w
                           ? g_host->text_w(" ", 1, fh)
                           : fh / 3;
            }
            int ww = g_host && g_host->text_w
                         ? g_host->text_w(s + w0, wlen, fh)
                         : wlen * fh / 2;
            if (nowrap) {
                int limit = cw + 280;
                if (x + lead + ww > cx + limit)
                    ww = cx + limit - x - lead;
                if (ww < 1)
                    ww = 1;
            } else if (x + lead + ww > ln.a1 && (ln.n || x > ln.a0)) {
                line_flush(&ln, r);
                y += lh;
                lh = def_lh;
                float_band(y, y + lh, cx, cx + cw, &ln.a0, &ln.a1);
                x = ln.a0;
                lead = 0;
            }
            int tx = x + lead;
            int ty = y + (lh - fh) / 2;
            any = 1;
            if (it->color == 0x606060 && !nowrap)
                op_fill(tx - 2, ty - 2, ww + 4, fh + 4, 0xEFEFEF);
            int oi = op_text(tx, ty, s + w0, wlen, col, fh, flags, it->link);
            if (oi >= 0 && ln.n < 30)
                ln.ops_idx[ln.n++] = oi;
            if (it->link >= 0 || (flags & MDTF_UL))
                op_fill(tx, ty + fh, ww, 1, col);
            if (flags & MDTF_STRIKE)
                op_fill(tx, ty + fh / 2, ww, 1, col);
            x = tx + ww;
            ln.w = x - ln.a0;
        }
    }
    line_flush(&ln, r);
#ifdef MD_DEBUG2
    fprintf(stderr, "[inl-done] bi=%d cy=%d y=%d lh=%d any=%d -> %d\n", bi, cy,
            y, lh, any, any ? y + lh : cy);
#endif
    return any ? y + lh : cy;
}


/* =============== paint =============== */
static void paint_one(const MdHost *h, const POp *o, int sx, int sy)
{
    int x = o->x - sx, y = o->y - sy;
    switch (o->k) {
    case PO_FILL:
        h->fill(h->dc, x, y, x + o->w, y + o->h, o->c);
        break;
    case PO_TEXT:
        h->text(h->dc, x, y, o->s, o->len, o->c, o->fh, o->f);
        break;
    case PO_SVF:
        if (o->img >= 0 && o->img < nimg && imgs[o->img].len)
            h->svf(h->dc, imgs[o->img].data, imgs[o->img].len, x, y,
                   o->w, o->h);
        else
            h->fill(h->dc, x, y, x + o->w, y + o->h, 0x767676);
        break;
    case PO_ELL:
        h->ellipse(h->dc, x, y, x + o->w, y + o->h, o->c, o->f);
        break;
    case PO_INPUT:
        if (o->f == IN_RADIO) {
            h->ellipse(h->dc, x, y, x + o->w, y + o->h, o->c, 0);
            if (o->img)
                h->ellipse(h->dc, x + 3, y + 3, x + o->w - 3, y + o->h - 3,
                           o->c, 1);
        } else if (o->f == IN_CHECK) {
            h->fill(h->dc, x, y, x + o->w, y + o->h, 0xFFFFFF);
            h->fill(h->dc, x, y, x + o->w, y + 1, o->c);
            h->fill(h->dc, x, y + o->h - 1, x + o->w, y + o->h, o->c);
            h->fill(h->dc, x, y, x + 1, y + o->h, o->c);
            h->fill(h->dc, x + o->w - 1, y, x + o->w, y + o->h, o->c);
            if (o->img) { /* tick */
                h->fill(h->dc, x + 2, y + o->h / 2, x + o->w / 2, y + o->h / 2 + 1, o->c);
                h->fill(h->dc, x + o->w / 2, y + o->h / 2, x + o->w / 2 + 1, y + o->h - 3, o->c);
            }
        } else if (o->f == IN_SUBMIT) {
            h->fill(h->dc, x, y, x + o->w, y + o->h, 0xCCCCCC);
            h->fill(h->dc, x, y, x + o->w, y + 1, 0xFFFFFF);
            h->fill(h->dc, x, y + o->h - 1, x + o->w, y + o->h, 0x888888);
            if (o->len > 0)
                h->text(h->dc, x + 9, y + (o->h - o->fh) / 2, o->s, o->len,
                        0x000000, o->fh, 0);
        } else { /* text field */
            h->fill(h->dc, x, y, x + o->w, y + o->h, 0xFFFFFF);
            h->fill(h->dc, x, y, x + o->w, y + 1, o->c);
            h->fill(h->dc, x, y + o->h - 1, x + o->w, y + o->h, o->c);
            h->fill(h->dc, x, y, x + 1, y + o->h, o->c);
            h->fill(h->dc, x + o->w - 1, y, x + o->w, y + o->h, o->c);
            if (o->len > 0)
                h->text(h->dc, x + 4, y + (o->h - o->fh) / 2, o->s, o->len,
                        0x000000, o->fh, 0);
        }
        break;
    default:
        break;
    }
}

void md_paint(const MdHost *h, int sx, int sy, int vw, int vh)
{
    if (!h)
        return;
    for (int i = 0; i < nops; ++i) {
        POp *o = &ops[i];
        if (o->y + o->h < sy || o->y > sy + vh)
            continue;
        if (o->x + o->w < sx || o->x > sx + vw)
            continue;
        paint_one(h, o, sx, sy);
    }
}

/* =============== find-in-page =============== */
int md_find(const char *query, int *x, int *y, int *w, int cap)
{
    if (!query || !g_host || !g_host->text_w)
        return 0;
    int ql = xlen(query);
    if (ql <= 0)
        return 0;
    int n = 0;
    int i = 0;
    static char linebuf[192];
    while (i < nops && n < cap) {
        /* collect a line: consecutive TEXT ops with the same y */
        if (ops[i].k != PO_TEXT) {
            ++i;
            continue;
        }
        int ly = ops[i].y;
        int start = i;
        int bl = 0;
        while (i < nops && ops[i].k == PO_TEXT && ops[i].y == ly && bl < 188) {
            int l = ops[i].len;
            if (l > 188 - bl)
                l = 188 - bl;
            for (int q = 0; q < l; ++q)
                linebuf[bl++] = ops[i].s[q];
            ++i;
        }
        linebuf[bl] = 0;
        if (!bl) {
            ++i;
            continue;
        }
        for (int q = 0; q + ql <= bl && n < cap; ++q) {
            int hit = 1;
            for (int k = 0; k < ql; ++k)
                if (xlow((unsigned char)linebuf[q + k]) !=
                    xlow((unsigned char)query[k])) {
                    hit = 0;
                    break;
                }
            if (hit) {
                /* x offset: width of the prefix, measured at the op font */
                int px = 0;
                int ci = start;
                int rem = q;
                while (ci < i && rem > 0) {
                    int l = ops[ci].len;
                    if (rem < l) {
                        px += g_host->text_w(ops[ci].s, rem, ops[ci].fh);
                        rem = 0;
                    } else {
                        px += g_host->text_w(ops[ci].s, l, ops[ci].fh);
                        rem -= l;
                    }
                    ++ci;
                }
                int wl = g_host->text_w(linebuf + q, ql, ops[start].fh);
                x[n] = px + ops[start].x;
                y[n] = ops[start].y - 2;
                w[n] = wl;
                ++n;
                q += ql - 1;
            }
        }
    }
    return n;
}

/* =============== link hit-test =============== */
int md_link_at(int px, int py, const char **href, int *len)
{
    for (int i = 0; i < nops; ++i) {
        POp *o = &ops[i];
        if (o->k != PO_TEXT || o->link < 0 || o->link >= nlnk)
            continue;
        if (px >= o->x - 2 && px <= o->x + o->w + 2 && py >= o->y - 3 &&
            py <= o->y + o->h + 3) {
            if (href)
                *href = lnks[o->link].s;
            if (len)
                *len = lnks[o->link].len;
            return 1;
        }
    }
    return 0;
}

/* =============== item compaction ===============
 * Items are appended in document order, so interleaved blocks (body text,
 * then a child block, then more body text) own non-contiguous ranges.
 * Layout assumes [inl, inl+ninl) is contiguous: rebuild the arena ordered
 * by owner. Run-once O(blocks x items), pointers into g_src/g_txt stay. */
static void compact_items(void)
{
    static Inl tmp[NINL];
    int n = ninl;
    if (n <= 0)
        return;
    for (int i = 0; i < n; ++i)
        tmp[i] = inls[i];
    ninl = 0;
    for (int b = 0; b < nblk; ++b) {
        blks[b].inl = -1;
        blks[b].ninl = 0;
        for (int i = 0; i < n; ++i) {
            if (tmp[i].owner != b)
                continue;
            if (blks[b].inl < 0)
                blks[b].inl = (short)ninl;
            inls[ninl++] = tmp[i];
            ++blks[b].ninl;
        }
    }
}

/* =============== cascade application =============== */
static void apply_css(void)
{
    /* rules are sorted by specificity; merge each match in order.
     * inline style="" attrs are stashed aside and merged last. */
    for (int bi = 0; bi < nblk; ++bi) {
        CssSt inline_st = blks[bi].st;
        unsigned long long had = blks[bi].st.set;
        for (int i = 0; i < (int)sizeof(CssSt); ++i)
            ((unsigned char *)&blks[bi].st)[i] = 0;
        blks[bi].st.set = 0;
        blks[bi].st.bg = blks[bi].st.fg = blks[bi].st.bdc = UNSET;
        for (int ri = 0; ri < nrule; ++ri)
            if (rule_matches(&rules[ri], bi))
                st_merge(&blks[bi].st, &rules[ri].st);
        /* restore attr-derived parts with top priority */
        if (had) {
            CssSt keep = inline_st;
            keep.set = had;
            st_merge(&blks[bi].st, &keep);
        }
    }
}

/* =============== title / doc finish =============== */
static void finish_title(void)
{
    if (g_title_len)
        return;
    for (int bi = 0; bi < nblk && !g_title_len; ++bi) {
        if (blks[bi].tag != TG_H1 || blks[bi].inl < 0)
            continue;
        int n = 0;
        for (int ii = blks[bi].inl;
             ii < blks[bi].inl + blks[bi].ninl && n < (int)sizeof(g_title) - 1;
             ++ii) {
            if (inls[ii].kind != T_TEXT)
                continue;
            int l = inls[ii].len;
            if (l > (int)sizeof(g_title) - 1 - n)
                l = (int)sizeof(g_title) - 1 - n;
            for (int q = 0; q < l; ++q)
                g_title[n + q] = inls[ii].s[q];
            n += l;
        }
        while (n > 0 && g_title[n - 1] == ' ')
            --n;
        g_title_len = n;
    }
}

/* =============== public API =============== */
void md_set_host(const MdHost *host)
{
    g_host = host;
}

static void reset_doc(void)
{
    nblk = ninl = nlnk = nref = nimg = nname = nrule = 0;
    nops = nflt = g_txt_len = g_title_len = cssbuf_len = 0;
    g_page_w = 0;
    g_page_h = 0;
    g_dialect = MD_SRC_MD;
    for (int i = 0; i < (int)sizeof(g_src); ++i)
        g_src[i] = 0;
}

int md_load(int dialect, const char *src, int len)
{
    if (!src || len <= 0)
        return 0;
    if (len > MAX_SRC - 1)
        len = MAX_SRC - 1;
    reset_doc();
    g_dialect = dialect;
    for (int i = 0; i < len; ++i)
        g_src[i] = src[i];
    g_src[len] = 0;
    g_src_len = len;

    int root = blk_new(-1, TG_HTML, 0);
    if (root < 0)
        return 0;
    HParser ps;
    for (int i = 0; i < (int)sizeof(ps); ++i)
        ((unsigned char *)&ps)[i] = 0;
    ps.cur = root;
    ps.nist = 0;

    if (dialect == MD_SRC_HTML) {
        html_parse(&ps, g_src, g_src + g_src_len);
    } else {
        const char *body = g_src;
        const char *end = g_src + g_src_len;
        /* mdx frontmatter: --- title: x --- */
        if (g_src_len > 4 && g_src[0] == '-' && g_src[1] == '-' &&
            g_src[2] == '-' && g_src[3] == '\n') {
            const char *q = g_src + 4;
            while (q + 3 < end && !(q[0] == '\n' && q[1] == '-' && q[2] == '-' &&
                                    q[3] == '-'))
                ++q;
            if (q + 3 < end) {
                for (const char *f = g_src + 4; f < q; ++f) {
                    if (q - f > 6 && xeq(f, "title:", 6)) {
                        const char *v = f + 6;
                        while (v < q && *v == ' ')
                            ++v;
                        int n = 0;
                        while (v + n < q && v[n] != '\n' && v[n] != '\r' &&
                               n < (int)sizeof(g_title) - 1)
                            ++n;
                        while (n > 0 && v[n - 1] == ' ')
                            --n;
                        for (int k = 0; k < n; ++k)
                            g_title[k] = v[k];
                        g_title_len = n;
                        break;
                    }
                }
                body = q + 4;
                if (*body == '\n')
                    ++body;
            }
        }
        MdLists ls;
        for (int i = 0; i < (int)sizeof(ls); ++i)
            ((unsigned char *)&ls)[i] = 0;
        for (int i = 0; i < 4; ++i)
            ls.list_blk[i] = -1;
        md_collect_refs();
        md_blocks(&ps, body, end, &ls);
    }
    /* pending <style> text (unclosed tag): parse now */
    if (cssbuf_len > 0) {
        cssbuf[cssbuf_len] = 0;
        css_parse(cssbuf);
    }
    compact_items();
    apply_css();
    finish_title();
    return 1;
}

#ifdef MD_DEBUG
static void md_debug_dump(void)
{
    fprintf(stderr, "doc: %d blocks, %d inls, %d rules, %d links, %d imgs\n",
            nblk, ninl, nrule, nlnk, nimg);
    for (int i = 0; i < nrule; ++i)
        fprintf(stderr, "rule %d: nsel=%d spec=%d tag0=%d taglast=%d set=%llx\n",
                i, rules[i].nsel, rules[i].spec, rules[i].tag[0],
                rules[i].tag[rules[i].nsel - 1],
                (unsigned long long)rules[i].st.set);
    for (int i = 0; i < nblk && i < 40; ++i)
        fprintf(stderr, "blk %d: tag=%d kind=%d parent=%d fc=%d inl=%d/%d "
                "id=%d cls=%d,%d set=%llx\n",
                i, blks[i].tag, blks[i].kind, blks[i].parent,
                blks[i].first_child, blks[i].inl, blks[i].ninl, blks[i].id,
                blks[i].cls[0], blks[i].cls[1],
                (unsigned long long)blks[i].st.set);
    fprintf(stderr, "after layout: %d ops, %d floats, page %dx%d\n", nops,
            nflt, g_page_w, g_page_h);
#ifdef MD_DEBUG2
    for (int i = 0; i < nblk && i < 22; ++i)
        fprintf(stderr, "rs %d: tag=%d m=%d,%d,%d,%d p=%d,%d,%d,%d bw=%d,%d,%d,%d "
                "w=%d h=%d fs=%d fh=%d flt=%d bg=%x fg=%x disp=%d align=%d lh=%d\n",
                i, blks[i].tag, rs_[i].m[0], rs_[i].m[1], rs_[i].m[2],
                rs_[i].m[3], rs_[i].p[0], rs_[i].p[1], rs_[i].p[2],
                rs_[i].p[3], rs_[i].bw[0], rs_[i].bw[1], rs_[i].bw[2],
                rs_[i].bw[3], rs_[i].w, rs_[i].h, rs_[i].fs, rs_[i].fh,
                rs_[i].flt, rs_[i].bg, rs_[i].fg, rs_[i].disp, rs_[i].align,
                rs_[i].lh);
#endif
    for (int i = 0; i < nflt; ++i)
        fprintf(stderr, "float %d: side=%d x=%d..%d y=%d..%d\n", i,
                flts[i].side, flts[i].xl, flts[i].xr, flts[i].yt, flts[i].yb);
}
#endif

void md_layout(int view_w)
{
    g_view_w = view_w;
    nops = 0;
    nflt = 0;
    g_page_w = view_w;
    g_page_h = 0;
    /* reserve op[0] for the canvas fill */
    op_fill(0, 0, view_w, 2, 0xFFFFFF);
    RSt rootrs;
    resolve_style(0, view_w, 0, &rootrs);
    rs_[0] = rootrs;
    layout_children(0, 0, 0, view_w, 0);
    /* canvas colour: html/body bg or white (reader: warm paper) */
    unsigned canvas = 0xFFFFFF;
    if (g_reader) {
        canvas = 0xEDE6D9;
    } else {
        for (int bi = 0; bi < nblk && bi < 4; ++bi) {
            if (rs_[bi].bg != UNSET) {
                canvas = rs_[bi].bg;
                if (blks[bi].tag == TG_BODY)
                    break;
            }
            if (blks[bi].tag == TG_BODY || blks[bi].tag == TG_HTML)
                break;
        }
    }
    ops[0].c = canvas;
    ops[0].w = (short)g_page_w;
    ops[0].h = (short)(g_page_h + 2);
    if (g_page_h < 2)
        g_page_h = 2;
#ifdef MD_DEBUG
    md_debug_dump();
#endif
#ifdef MD_DEBUG2
    for (int i = 0; i < nops && i < 80; ++i) {
        if (ops[i].k == PO_FILL)
            fprintf(stderr, "[fill] %d,%d %dx%d c=%06x\n", ops[i].x,
                    ops[i].y, ops[i].w, ops[i].h, ops[i].c);
        else if (ops[i].k == PO_TEXT)
            fprintf(stderr, "[text] %d,%d fh=%d w=%d f=%d link=%d '%.*s'\n",
                    ops[i].x, ops[i].y, ops[i].fh, ops[i].w, ops[i].f,
                    ops[i].link, ops[i].len > 30 ? 30 : ops[i].len, ops[i].s);
    }
#endif
#ifdef MD_DEBUG2
    for (int i = 0; i < ninl && i < 60; ++i) {
        int owner = -1;
        for (int b = 0; b < nblk; ++b)
            if (i >= blks[b].inl && i < blks[b].inl + blks[b].ninl)
                owner = b;
        fprintf(stderr, "inl %d: k=%d own=%d len=%d off=%ld '%.*s'\n", i,
                inls[i].kind, owner, inls[i].len,
                inls[i].kind == T_TEXT ? (long)(inls[i].s - g_txt) : -1L,
                inls[i].len > 24 ? 24 : inls[i].len,
                inls[i].kind == T_TEXT ? inls[i].s : "");
    }
#endif
}

void md_reader(int on)
{
    g_reader = on ? 1 : 0;
}

int md_reader_is(void)
{
    return g_reader;
}

void md_size(int *w, int *h)
{
    if (w)
        *w = g_page_w;
    if (h)
        *h = g_page_h;
}

const char *md_title(int *len)
{
    if (len)
        *len = g_title_len;
    return g_title_len ? g_title : 0;
}

int md_anchor_scroll(int y)
{
    if (y < 0)
        y = 0;
    if (y > g_page_h)
        y = g_page_h;
    return y;
}
