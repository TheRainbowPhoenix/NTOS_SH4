#!/usr/bin/env python3
"""append_ui.py — append the converter data + uXAML plumbing + painting +
input + entry point to calc_app.c (the engine half is already in place)."""

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'

UI = r'''

/* =====================================================================
 * unit converter data (UnitConverter.xbf + pri unit names/abbreviations;
 * factors are exact decimal constants, temperature is affine)
 * ===================================================================== */
typedef struct {
    const char *name;    /* display name (pri UnitName_*) */
    const char *abbr;    /* abbreviation (pri UnitAbbreviation_*) */
    long long fm;        /* factor mantissa: value_in_base = v * fm * 10^fe */
    int fe;
} Unit;

typedef struct {
    const char *name;    /* CategoryName_* */
    int count;
    const Unit *units;
    int supp[3];         /* supplementary result unit indices */
} Category;

#define U(nm, ab, m, e) { nm, ab, m, e }

static const Unit U_VOLUME[] = {
    U("Milliliters", "mL", 1, 0),
    U("Cubic centimeters", "cm3", 1, 0),
    U("Liters", "L", 1000, 0),
    U("Cubic meters", "m3", 1000000, 0),
    U("Teaspoons (US)", "tsp", 4928921, -6),
    U("Tablespoons (US)", "tbsp", 14786765, -6),
    U("Fluid ounces (US)", "fl oz", 2957353, -5),
    U("Cups (US)", "cup", 2365882, -4),
    U("Pints (US)", "pt", 4731765, -4),
    U("Quarts (US)", "qt", 9463530, -4),
    U("Gallons (US)", "gal", 3785412, -3),
    U("Gallons (UK)", "gal", 4546090, -3),
    U("Cubic inches", "in3", 16387064, -6),
    U("Cubic feet", "ft3", 28316847, -3),
    U("Cubic yards", "yd3", 764554857, -3),
};

static const Unit U_LENGTH[] = {
    U("Nanometers", "nm", 1, -9),
    U("Microns", "um", 1, -6),
    U("Millimeters", "mm", 1, -3),
    U("Centimeters", "cm", 1, -2),
    U("Meters", "m", 1, 0),
    U("Kilometers", "km", 1, 3),
    U("Inches", "in", 254, -4),
    U("Feet", "ft", 3048, -4),
    U("Yards", "yd", 9144, -4),
    U("Miles", "mi", 1609344, -3),
    U("Nautical Miles", "nmi", 1852, 0),
};

static const Unit U_WEIGHT[] = {
    U("Carats", "CD", 2, -4),
    U("Milligrams", "mg", 1, -6),
    U("Centigrams", "cg", 1, -5),
    U("Decigrams", "dg", 1, -4),
    U("Grams", "g", 1, -3),
    U("Dekagrams", "dag", 1, -2),
    U("Hectograms", "hg", 1, -1),
    U("Kilograms", "kg", 1, 0),
    U("Metric tonnes", "t", 1000, 0),
    U("Ounces", "oz", 28349523, -6),
    U("Pounds", "lb", 45359237, -5),
    U("Long tons (UK)", "ton", 1016046909, -3),
    U("Short tons (US)", "ton", 907184740, -3),
    U("Stone", "st", 63502932, -4),
};

static const Unit U_TEMP[] = {
    U("Celsius", "C", 1, 0),
    U("Fahrenheit", "F", 1, 0),
    U("Kelvin", "K", 1, 0),
};

static const Unit U_ENERGY[] = {
    U("Joules", "J", 1, 0),
    U("Kilojoules", "kJ", 1000, 0),
    U("Thermal calories", "cal", 4184, 0),
    U("Food calories", "kcal", 4184000, 0),
    U("Foot-pounds", "ft-lb", 13558179, -3),
    U("British thermal units", "BTU", 10550559, -2),
};

static const Unit U_AREA[] = {
    U("Square millimeters", "mm2", 1, -6),
    U("Square centimeters", "cm2", 1, -4),
    U("Square meters", "m2", 1, 0),
    U("Hectares", "ha", 10000, 0),
    U("Square kilometers", "km2", 1, 6),
    U("Square inches", "in2", 64516, -8),
    U("Square feet", "ft2", 9290304, -8),
    U("Square yards", "yd2", 83612736, -8),
    U("Acres", "ac", 4046856422, -3),
    U("Square miles", "mi2", 2589988110, 0),
};

static const Unit U_SPEED[] = {
    U("Centimeters per second", "cm/s", 1, -2),
    U("Meters per second", "m/s", 1, 0),
    U("Kilometers per hour", "km/h", 2777778, -6),
    U("Feet per second", "ft/s", 3048, -4),
    U("Miles per hour", "mph", 44704, -4),
    U("Knots", "kn", 5144444, -6),
    U("Mach", "M", 34029, 0),
};

static const Unit U_TIME[] = {
    U("Milliseconds", "ms", 1, -3),
    U("Seconds", "s", 1, 0),
    U("Minutes", "min", 60, 0),
    U("Hours", "hr", 3600, 0),
    U("Days", "d", 86400, 0),
    U("Weeks", "wk", 604800, 0),
    U("Years", "yr", 31536000, 0),
};

static const Unit U_POWER[] = {
    U("Watts", "W", 1, 0),
    U("Kilowatts", "kW", 1000, 0),
    U("Horsepower (US)", "hp", 7456999, 0),
    U("Foot-pounds/minute", "ft-lb/min", 22596965, -3),
    U("BTUs/minute", "BTU/min", 17584265, 0),
};

static const Unit U_DATA[] = {
    U("Bits", "b", 1, 0),
    U("Bytes", "B", 8, 0),
    U("Kilobits", "Kb", 1000, 0),
    U("Kilobytes", "KB", 8000, 0),
    U("Megabits", "Mb", 1000000, 0),
    U("Megabytes", "MB", 8000000, 0),
    U("Gigabits", "Gb", 1000000000, 0),
    U("Gigabytes", "GB", 8000000000, 0),
    U("Terabytes", "TB", 8000000000000, 0),
    U("Petabytes", "PB", 8000000000000000, 0),
    U("Kibibytes", "KiB", 8192, 0),
    U("Mebibytes", "MiB", 8388608, 0),
    U("Gibibytes", "GiB", 8589934592, 0),
};

static const Unit U_PRESSURE[] = {
    U("Pascals", "Pa", 1, 0),
    U("Kilopascals", "kPa", 1000, 0),
    U("Bars", "ba", 100000, 0),
    U("Atmospheres", "atm", 101325, 0),
    U("Millimeters of mercury", "mmHg", 1333224, -2),
    U("Pounds per square inch", "psi", 6894757, 0),
};

static const Unit U_ANGLE[] = {
    U("Degrees", "deg", 1, 0),
    U("Radians", "rad", 3141592653589793, -15),
    U("Gradians", "grad", 9, 0),
};

static const Category CATS[] = {
    { "Volume", 15, U_VOLUME, { 5, 8, 10 } },
    { "Length", 11, U_LENGTH, { 6, 8, 9 } },
    { "Weight and Mass", 14, U_WEIGHT, { 8, 10, 11 } },
    { "Temperature", 3, U_TEMP, { 0, 1, 2 } },
    { "Energy", 6, U_ENERGY, { 1, 3, 5 } },
    { "Area", 10, U_AREA, { 3, 4, 8 } },
    { "Speed", 7, U_SPEED, { 2, 4, 5 } },
    { "Time", 7, U_TIME, { 2, 3, 6 } },
    { "Power", 5, U_POWER, { 1, 2, 4 } },
    { "Data", 13, U_DATA, { 5, 7, 9 } },
    { "Pressure", 6, U_PRESSURE, { 2, 3, 5 } },
    { "Angle", 3, U_ANGLE, { 1, 2, 0 } },
};

#define CAT_COUNT ((int)(sizeof(CATS) / sizeof(CATS[0])))

/* converter state */
static int conv_cat = 0;      /* category index (Volume default) */
static int conv_u1 = 2;       /* Liters */
static int conv_u2 = 10;      /* Gallons (US) */
static int conv_active = 1;   /* which display is being typed into */
static char conv_entry[24];   /* entry for the active side ("1" default) */

static Dec conv_entry_value(void)
{
    if (!conv_entry[0])
        return dec_i(0);
    long long m = 0;
    int e = 0, neg = 0, seen_dot = 0, digits = 0;
    const char *s = conv_entry;
    if (*s == '-') {
        neg = 1;
        ++s;
    }
    for (; *s; ++s) {
        if (*s == '.') {
            seen_dot = 1;
            continue;
        }
        if (digits < 16) {
            m = m * 10 + (*s - '0');
            if (seen_dot)
                --e;
            if (m)
                ++digits;
        }
    }
    Dec r;
    r.m = neg ? -m : m;
    r.e = e;
    return dec_norm(r);
}

/* temperature conversion is affine: via Celsius */
static Dec temp_to_c(Dec v, int unit)
{
    switch (unit) {
    case 0: return v;                                   /* Celsius */
    case 1: return dec_div_i(dec_mul(dec_add(v, dec_i(-32)), dec_i(5)), 9);
    default: return dec_add(v, dec_neg(dec_div_i(dec_mul(dec_i(5463), dec_i(1)), 20))); /* K - 273.15 */
    }
}

static Dec c_to_temp(Dec c, int unit)
{
    switch (unit) {
    case 0: return c;
    case 1: return dec_add(dec_div_i(dec_mul(c, dec_i(9)), 5), dec_i(32));
    default: return dec_add(c, dec_div_i(dec_mul(dec_i(5463), dec_i(1)), 20));
    }
}

static Dec unit_to_base(Dec v, int cat, int unit)
{
    if (cat == 3)
        return temp_to_c(v, unit);
    Dec f;
    f.m = CATS[cat].units[unit].fm;
    f.e = CATS[cat].units[unit].fe;
    return dec_mul(v, f);
}

static Dec base_to_unit(Dec v, int cat, int unit)
{
    if (cat == 3)
        return c_to_temp(v, unit);
    Dec f;
    f.m = CATS[cat].units[unit].fm;
    f.e = CATS[cat].units[unit].fe;
    return dec_div(v, f);
}

/* convert the active entry into the other side's unit */
static Dec conv_result(int side)
{
    Dec v = conv_entry_value();
    if (conv_cat == 3) {
        Dec c = temp_to_c(v, conv_active == 1 ? conv_u1 : conv_u2);
        return c_to_temp(c, side == 1 ? conv_u1 : conv_u2);
    }
    Dec base = dec_mul(v, (Dec){ CATS[conv_cat].units[conv_active == 1 ? conv_u1 : conv_u2].fm,
                                 CATS[conv_cat].units[conv_active == 1 ? conv_u1 : conv_u2].fe });
    Dec f;
    f.m = CATS[conv_cat].units[side == 1 ? conv_u1 : conv_u2].fm;
    f.e = CATS[conv_cat].units[side == 1 ? conv_u1 : conv_u2].fe;
    return dec_div(base, f);
}

/* =====================================================================
 * uXAML plumbing (same model as settings_app.c / files_app.c)
 * ===================================================================== */
static UxFile ux;
static unsigned char ux_blob[64 * 1024];
typedef struct {
    short x, y, w, h;
} NRect;
static NRect nrect[UX_MAX_NODES];

#define DYN_MAX 24
static char dyn_heap[DYN_MAX][UX_STR_MAX];
static unsigned dyn_slot;

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

static void dyn_set(const char *name, const char *text)
{
    const UxDoc *d = 0;
    const char *docs[] = { "StdPage", "SciPage", "ConvPage", "AboutPage" };
    for (unsigned i = 0; i < 4 && !d; ++i)
        d = ux_find_doc(&ux, docs[i]);
    if (!d)
        return;
    const UxNode *n = ux_find_name(&ux, d, name);
    if (!n)
        return;
    /* search every doc for this name */
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *nn = ux_find_name(&ux, dd, name);
        if (!nn)
            continue;
        UxNode *mut = (UxNode *)nn;
        int a = ux_attr(mut, UXA_TEXT);
        if (a < 0) {
            if (mut->attr_count >= UX_MAX_ATTRS)
                continue;
            a = mut->attr_count++;
            mut->attr[a] = UXA_TEXT;
            mut->type[a] = 0;
        }
        char *dst = dyn_heap[dyn_slot];
        int k = 0;
        for (; text[k] && k < UX_STR_MAX - 1; ++k)
            dst[k] = text[k];
        dst[k] = 0;
        mut->num[a] = 0x8000u + (uint16_t)dyn_slot;
        dyn_slot = (dyn_slot + 1) % DYN_MAX;
    }
}

static void dyn_set_color(const char *name, U32 color)
{
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *nn = ux_find_name(&ux, dd, name);
        if (!nn)
            continue;
        UxNode *mut = (UxNode *)nn;
        int a = ux_attr(mut, UXA_COLOR);
        if (a < 0) {
            if (mut->attr_count >= UX_MAX_ATTRS)
                continue;
            a = mut->attr_count++;
            mut->attr[a] = UXA_COLOR;
        }
        mut->type[a] = 2; /* CLR */
        mut->num[a] = (int32_t)color;
    }
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
            x = bx;
            y = by;
            w = bw;
            h = bh;
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

/* ---- drawing (GDI FillRect backend, brush cache) ---- */
static U32 cached_color;
static void *cached_brush;
static Rect clip_rect = { 0, 0, 320, 528 };

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

/* ---- bitmap font ---- */
static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[0];
    int bd = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        int df = f->height - h;
        if (df < 0) df = -df;
        if (df < bd) {
            best = f;
            bd = df;
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
            const unsigned char *row = (const unsigned char *)f->bits +
                                       g->offset + yy * stride;
            for (int xx = 0; xx < g->width; ++xx) {
                if (row[xx >> 3] & (0x80u >> (xx & 7)))
                    fill(dc, x + xx, y + yy, x + xx + 1, y + yy + 1, c);
            }
        }
        x += g->advance + 1;
    }
}

/* ---- app modes / sheets ---- */
enum { M_STD = 0, M_SCI, M_CONV, M_ABOUT };
enum { SH_NONE = 0, SH_NAV, SH_HIST, SH_MEM, SH_UNITS };

static int mode = M_STD;
static int sheet = SH_NONE;
static int units_side = 1;      /* which combo opened the unit picker */
static int hist_scroll, mem_scroll, nav_scroll;
static const CalcChrome *g_chrome;

/* pressed-button feedback (ListMedium tint) */
static const UxNode *pressed_node;

static void paint_node(void *dc, unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    if (!node_visible_idx(ni))
        return;
    NRect r = nrect[ni];
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
        const char *t = node_text(n);
        U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
        int sz = r.w < r.h ? r.w : r.h;
        if (t) {
            int sid = ci_find(t);
            if (sid)
                ci_draw(ifill, dc, r.x, r.y, sz, col, sid);
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
            if (n == pressed_node)
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, C_PRESS);
            else if (bg != 0xFFFFFFFFu)
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

static int find_rect(const char *name, NRect *out)
{
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *n = ux_find_name(&ux, dd, name);
        if (n) {
            unsigned idx = (unsigned)(n - ux.nodes);
            *out = nrect[idx];
            return 1;
        }
    }
    return 0;
}

/* =====================================================================
 * page painting
 * ===================================================================== */
static const char *err_text(void)
{
    switch (g.err) {
    case 1: return "Divide by zero";
    case 2: return "Overflow";
    case 3: return "Result is undefined";
    default: return "Invalid input";
    }
}

/* the big result line with auto-shrink (CalcResult: 46 -> 12 floor) */
static void draw_result(void *dc, int x, int y, int w, int h, const char *txt,
                        U32 col)
{
    int size = 44;
    while (size > 12 && text_w(txt, size) > w - 24)
        size -= 2;
    draw_text(dc, x + w, y + (h - size) / 2 + 2, txt, col, size, 2, 0);
}

static void paint_std_display(void *dc)
{
    /* expression line (small, gray, right) */
    if (g.expr[0])
        draw_text(dc, 316, 84, g.expr, C_MUT, 14, 2, 0);
    /* result: live entry while typing, else the display value */
    char buf[28];
    if (g.err)
        draw_result(dc, 0, 100, 320, 68, err_text(), RGB_(0xC4, 0x2B, 0x1C));
    else {
        if (g.entry_active && g.entry[0]) {
            char *dst = buf;
            const char *s = g.entry;
            int len = 0;
            while (s[len] && len < 22) ++len;
            for (int i = 0; i < len; ++i) dst[i] = s[i];
            dst[len] = 0;
            /* light grouping for the integer part of the entry */
        } else {
            dec_to_string_buf(g.display, buf, 26);
        }
        draw_result(dc, 0, 100, 320, 68, buf, C_INK);
    }
}

static void paint_std(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "StdPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    paint_std_display(dc);
}

static void paint_sci(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "SciPage");
    if (!d)
        return;
    /* angle mode colours */
    dyn_set_color("AngDegT", g.angle_mode == ANG_DEG ? C_ACC : C_MUT);
    dyn_set_color("AngRadT", g.angle_mode == ANG_RAD ? C_ACC : C_MUT);
    dyn_set_color("AngGradT", g.angle_mode == ANG_GRAD ? C_ACC : C_MUT);
    /* 2nd toggle state */
    dyn_set_color("secondT", g.second ? C_ACC : C_MUT);
    /* 2nd swaps the trig row with the inverse row */
    dyn_set("sinT", g.second ? "asin" : "sin");
    dyn_set("cosT", g.second ? "acos" : "cos");
    dyn_set("tanT", g.second ? "atan" : "tan");
    dyn_set("asinT", g.second ? "sin" : "asin");
    dyn_set("acosT", g.second ? "cos" : "acos");
    dyn_set("atanT", g.second ? "tan" : "atan");
    dyn_set("ln", 0);
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    /* display: expression + result in the compact band */
    if (g.expr[0])
        draw_text(dc, 316, 82, g.expr, C_MUT, 12, 2, 0);
    char buf[28];
    if (g.err) {
        draw_result(dc, 0, 94, 320, 40, err_text(), RGB_(0xC4, 0x2B, 0x1C));
    } else {
        if (g.entry_active && g.entry[0]) {
            int len = 0;
            while (g.entry[len] && len < 22) ++len;
            for (int i = 0; i < len; ++i) buf[i] = g.entry[i];
            buf[len] = 0;
        } else {
            dec_to_string_buf(g.display, buf, 26);
        }
        draw_result(dc, 0, 94, 320, 40, buf, C_INK);
    }
}

static void paint_conv(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "ConvPage");
    if (!d)
        return;
    dyn_set("Units1T", CATS[conv_cat].units[conv_u1].name);
    dyn_set("Units2T", CATS[conv_cat].units[conv_u2].name);
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    /* value 1 (active = bold black, other = gray) */
    char b1[26], b2[26];
    Dec v1 = conv_result(1);
    Dec v2 = conv_result(2);
    if (conv_active == 1 && conv_entry[0]) {
        int len = 0;
        while (conv_entry[len] && len < 18) ++len;
        for (int i = 0; i < len; ++i) b1[i] = conv_entry[i];
        b1[len] = 0;
    } else {
        dec_to_string_buf(v1, b1, 24);
    }
    if (conv_active == 2 && conv_entry[0]) {
        int len = 0;
        while (conv_entry[len] && len < 18) ++len;
        for (int i = 0; i < len; ++i) b2[i] = conv_entry[i];
        b2[len] = 0;
    } else {
        dec_to_string_buf(v2, b2, 24);
    }
    draw_result(dc, 12, 82, 296, 42, b1,
                conv_active == 1 ? C_INK : C_MUT);
    draw_result(dc, 12, 158, 296, 42, b2,
                conv_active == 2 ? C_INK : C_MUT);
    /* supplementary: "Also equal to" + up to 3 sibling units of the
     * passive side's value */
    draw_text(dc, 12, 236, "Also equal to", C_MUT, 12, 0, 0);
    int passive = conv_active == 1 ? 2 : 1;
    Dec base = unit_to_base(conv_entry_value(), conv_cat,
                            conv_active == 1 ? conv_u1 : conv_u2);
    int x = 12;
    for (int i = 0; i < 3; ++i) {
        int ui = CATS[conv_cat].supp[i];
        if (ui < 0 || ui >= CATS[conv_cat].count)
            continue;
        if (conv_cat == 3)
            continue; /* temperature handled separately below */
        Dec sv = base_to_unit(base, conv_cat, ui);
        char sb[24];
        dec_to_string_buf(dec_trim(sv, 6), sb, 22);
        draw_text(dc, x, 254, sb, C_INK, 14, 0, 0);
        x += text_w(sb, 14) + 2;
        draw_text(dc, x, 256, CATS[conv_cat].units[ui].abbr, C_MUT, 12, 0, 0);
        x += text_w(CATS[conv_cat].units[ui].abbr, 12) + 12;
        if (x > 260)
            break;
    }
    (void)passive;
}

static void paint_about(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "AboutPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
}

/* =====================================================================
 * sheets (full-surface overlays: nav pane, history, memory, unit picker)
 * ===================================================================== */
#define NAV_W 296

static void paint_nav(void *dc)
{
    /* scrim over the page */
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    /* pane */
    fill(dc, 0, 0, NAV_W, CHROME_Y, C_SHEET);
    fill(dc, NAV_W, 0, NAV_W + 1, CHROME_Y, C_HAIR);
    clip_set(0, 0, NAV_W, CHROME_Y);
    int y = 12 - nav_scroll;
    /* group: CALCULATOR */
    draw_text(dc, 12, y, "CALCULATOR", C_INK, 12, 0, 1);
    y += 30;
    static const char *calc_modes[] = { "Standard", "Scientific" };
    for (int i = 0; i < 2; ++i) {
        int sel = (i == 0 && mode == M_STD) || (i == 1 && mode == M_SCI);
        if (sel)
            fill(dc, 0, y - 8, NAV_W, 40, RGB_(0xC5, 0xE1, 0xF7));
        draw_text(dc, 48, y, calc_modes[i], sel ? C_INK : C_INK, 15, 0, 0);
        y += 40;
    }
    draw_text(dc, 12, y, "CONVERTER", C_INK, 12, 0, 1);
    y += 30;
    for (int i = 0; i < CAT_COUNT; ++i) {
        int sel = mode == M_CONV && conv_cat == i;
        if (sel)
            fill(dc, 0, y - 8, NAV_W, 40, RGB_(0xC5, 0xE1, 0xF7));
        draw_text(dc, 48, y, CATS[i].name, C_INK, 15, 0, 0);
        y += 40;
    }
    draw_text(dc, 12, y, "ABOUT", C_INK, 12, 0, 1);
    y += 30;
    if (mode == M_ABOUT)
        fill(dc, 0, y - 8, NAV_W, 40, RGB_(0xC5, 0xE1, 0xF7));
    draw_text(dc, 48, y, "About", C_INK, 15, 0, 0);
    clip_set(0, 0, 320, 528);
}

static void paint_hist(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    /* bottom sheet */
    int top = 90;
    fill(dc, 0, top, 320, CHROME_Y, C_SHEET);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 12, top + 10, "History", C_INK, 15, 0, 1);
    /* trash button bottom-right */
    fi_draw(ifill, dc, 268, top + 16, 28, 28, C_ACC, FI_DELETE);
    if (!hist_count) {
        draw_text(dc, 12, top + 60, "There's no history yet", C_MUT, 14, 0, 0);
        return;
    }
    clip_set(0, top + 48, 320, CHROME_Y - 46);
    int y = top + 56 - hist_scroll;
    for (int i = hist_count - 1; i >= 0; --i) {
        draw_text(dc, 306, y, hist[i].expr, C_MUT, 12, 2, 0);
        draw_text(dc, 306, y + 18, hist[i].result, C_INK, 16, 2, 1);
        y += 46;
    }
    clip_set(0, 0, 320, 528);
}

static void paint_mem(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    int top = 90;
    fill(dc, 0, top, 320, CHROME_Y, C_SHEET);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 12, top + 10, "Memory", C_INK, 15, 0, 1);
    draw_text(dc, 268, top + 12, "MC", C_ACC, 14, 1, 0);
    if (!mem_count) {
        draw_text(dc, 12, top + 60, "There's nothing saved in memory", C_MUT, 14, 0, 0);
        return;
    }
    clip_set(0, top + 48, 320, CHROME_Y - 46);
    int y = top + 56 - mem_scroll;
    for (int i = mem_count - 1; i >= 0; --i) {
        draw_text(dc, 306, y, mem_str[i], C_INK, 16, 2, 1);
        /* per-row MC / M+ / M- accent buttons */
        draw_text(dc, 244, y + 22, "MC", C_ACC, 12, 1, 0);
        draw_text(dc, 274, y + 22, "M+", C_ACC, 12, 1, 0);
        draw_text(dc, 302, y + 22, "M-", C_ACC, 12, 1, 0);
        y += 54;
    }
    clip_set(0, 0, 320, 528);
}

static void paint_units(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    int top = 70;
    fill(dc, 0, top, 320, CHROME_Y, C_SHEET);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 160, top + 10, CATS[conv_cat].name, C_INK, 15, 1, 1);
    clip_set(0, top + 40, 320, CHROME_Y);
    int y = top + 48;
    for (int i = 0; i < CATS[conv_cat].count; ++i) {
        int sel = (units_side == 1 ? conv_u1 : conv_u2) == i;
        if (sel)
            fill(dc, 0, y - 6, 320, 36, RGB_(0xC5, 0xE1, 0xF7));
        draw_text(dc, 24, y, CATS[conv_cat].units[i].name,
                  sel ? C_INK : C_INK, 15, 0, 0);
        draw_text(dc, 296, y + 2, CATS[conv_cat].units[i].abbr, C_MUT, 13, 2, 0);
        y += 36;
    }
    clip_set(0, 0, 320, 528);
}

static void paint_all(void *dc)
{
    switch (mode) {
    case M_SCI: paint_sci(dc); break;
    case M_CONV: paint_conv(dc); break;
    case M_ABOUT: paint_about(dc); break;
    default: paint_std(dc); break;
    }
    switch (sheet) {
    case SH_NAV: paint_nav(dc); break;
    case SH_HIST: paint_hist(dc); break;
    case SH_MEM: paint_mem(dc); break;
    case SH_UNITS: paint_units(dc); break;
    default: break;
    }
    if (g_chrome)
        g_chrome->paint(dc);
}

/* =====================================================================
 * input
 * ===================================================================== */
static void push_history(void)
{
    char res[24];
    dec_to_string_buf(g.display, res, 22);
    hist_push(g.expr[0] ? g.expr : res, res);
}

static void do_digit(char c)
{
    if (mode == M_CONV) {
        int len = 0;
        while (conv_entry[len])
            ++len;
        if (c == '.') {
            for (int i = 0; i < len; ++i)
                if (conv_entry[i] == '.')
                    return;
            if (len < 22) {
                conv_entry[len] = len ? '.' : '0';
                if (!len)
                    conv_entry[1] = '.', conv_entry[2] = 0;
                else
                    conv_entry[len + 1] = 0;
            }
            return;
        }
        if (len < 16 && !(len == 1 && conv_entry[0] == '0'))
            conv_entry[len] = c, conv_entry[len + 1] = 0;
        return;
    }
    entry_digit(c);
    if (mode == M_SCI) {
        /* typing after an operator shows in the expression line */
    }
}

static void conv_clear_entry(void)
{
    conv_entry[0] = 0;
}

static void conv_backspace(void)
{
    int len = 0;
    while (conv_entry[len])
        ++len;
    if (len)
        conv_entry[len - 1] = 0;
}

static void std_command(const char *cmd)
{
    if (!cmd)
        return;
    if (cmd[0] == 'd' && cmd[1] == ':') {
        do_digit(cmd[2]);
        return;
    }
    if (!xeq(cmd, "op:ce")) {
        if (mode == M_CONV)
            conv_clear_entry();
        else
            op_clear_entry();
    } else if (!xeq(cmd, "op:c")) {
        if (mode == M_CONV) {
            conv_clear_entry();
        } else {
            op_clear();
        }
    } else if (!xeq(cmd, "op:back")) {
        if (mode == M_CONV)
            conv_backspace();
        else
            entry_backspace();
    } else if (!xeq(cmd, "op:eq")) {
        if (mode != M_CONV) {
            equals();
            if (!g.err)
                push_history();
        }
    } else if (!xeq(cmd, "op:divide")) {
        binary_op(OP_DIV);
    } else if (!xeq(cmd, "op:times")) {
        binary_op(OP_MUL);
    } else if (!xeq(cmd, "op:minus")) {
        binary_op(OP_SUB);
    } else if (!xeq(cmd, "op:plus")) {
        binary_op(OP_ADD);
    } else if (!xeq(cmd, "op:negate")) {
        if (mode == M_CONV) {
            if (conv_entry[0] == '-') {
                int i = 1, j = 0;
                while (conv_entry[i])
                    conv_entry[j++] = conv_entry[i++];
                conv_entry[j] = 0;
            } else {
                int len = 0;
                while (conv_entry[len])
                    ++len;
                for (int i = len; i >= 0; --i)
                    conv_entry[i + 1] = conv_entry[i];
                conv_entry[0] = '-';
            }
        } else {
            op_negate();
        }
    } else if (!xeq(cmd, "op:percent")) {
        op_percent();
    } else if (!xeq(cmd, "op:sqrt")) {
        op_sqrt();
    } else if (!xeq(cmd, "op:sqr")) {
        op_sqr();
    } else if (!xeq(cmd, "op:cube")) {
        op_cube();
    } else if (!xeq(cmd, "op:invert")) {
        op_invert();
    } else if (!xeq(cmd, "op:mod")) {
        binary_op(OP_MOD);
    } else if (!xeq(cmd, "op:powy")) {
        binary_op(OP_POW);
    } else if (!xeq(cmd, "op:yrootx")) {
        binary_op(OP_YROOT);
    } else if (!xeq(cmd, "op:pi")) {
        op_pi();
    } else if (!xeq(cmd, "op:pl")) {
        paren_open();
    } else if (!xeq(cmd, "op:pr")) {
        paren_close();
    } else if (!xeq(cmd, "op:tenpow")) {
        fn_exp10();
    } else if (!xeq(cmd, "op:epow")) {
        fn_expe();
    } else if (!xeq(cmd, "fn:sin")) {
        fn_trig(0);
    } else if (!xeq(cmd, "fn:cos")) {
        fn_trig(1);
    } else if (!xeq(cmd, "fn:tan")) {
        fn_trig(2);
    } else if (!xeq(cmd, "fn:asin")) {
        fn_trig(8);
    } else if (!xeq(cmd, "fn:acos")) {
        fn_trig(9);
    } else if (!xeq(cmd, "fn:atan")) {
        fn_trig(10);
    } else if (!xeq(cmd, "fn:log")) {
        fn_log();
    } else if (!xeq(cmd, "fn:ln")) {
        fn_ln();
    } else if (!xeq(cmd, "fn:fact")) {
        fn_fact();
    } else if (!xeq(cmd, "fn:dms")) {
        op_dms();
    } else if (!xeq(cmd, "fn:deg")) {
        op_deg();
    } else if (!xeq(cmd, "second")) {
        g.second = !g.second;
    } else if (!xeq(cmd, "angle:deg")) {
        g.angle_mode = ANG_DEG;
    } else if (!xeq(cmd, "angle:rad")) {
        g.angle_mode = ANG_RAD;
    } else if (!xeq(cmd, "angle:grad")) {
        g.angle_mode = ANG_GRAD;
    } else if (!xeq(cmd, "mc")) {
        mem_count = 0;
    } else if (!xeq(cmd, "mr")) {
        if (mem_count) {
            g.display = mem_vals[mem_count - 1];
            g.entry_active = 0;
            g.entry[0] = 0;
        }
    } else if (!xeq(cmd, "ms")) {
        mem_push(g.display);
    } else if (!xeq(cmd, "m+")) {
        if (mem_count)
            mem_vals[mem_count - 1] = dec_add(mem_vals[mem_count - 1], g.display);
        else
            mem_push(g.display);
        if (mem_count)
            dec_to_string_buf(mem_vals[mem_count - 1], mem_str[mem_count - 1], 20);
    } else if (!xeq(cmd, "m-")) {
        if (mem_count)
            mem_vals[mem_count - 1] = dec_add(mem_vals[mem_count - 1], dec_neg(g.display));
        else
            mem_push(dec_neg(g.display));
        if (mem_count)
            dec_to_string_buf(mem_vals[mem_count - 1], mem_str[mem_count - 1], 20);
    } else if (!xeq(cmd, "memory")) {
        sheet = SH_MEM;
    } else if (!xeq(cmd, "history")) {
        sheet = SH_HIST;
    } else if (!xeq(cmd, "nav")) {
        sheet = SH_NAV;
        nav_scroll = 0;
    } else if (!xeq(cmd, "feedback")) {
        /* no-op in the POC */
    }
}

static int xstrlen(const char *s) { int n = 0; while (s && s[n]) ++n; return n; }
static int xeq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void xcopy(char *d, const char *s, int n) { for (int i = 0; i < n; ++i) d[i] = s[i]; }

/* nav pane hit: returns 1 when consumed */
static int nav_tap(int x, int y)
{
    if (x >= NAV_W) {
        sheet = SH_NONE;
        return 1;
    }
    int ly = y + nav_scroll;
    int row = 12;
    /* CALCULATOR group */
    if (ly >= row + 30 && ly < row + 30 + 80) {
        int i = (ly - row - 30) / 40;
        mode = i == 0 ? M_STD : M_SCI;
        engine_init();
        sheet = SH_NONE;
        return 1;
    }
    row += 30 + 80;
    if (ly >= row + 30 && ly < row + 30 + CAT_COUNT * 40) {
        int i = (ly - row - 30) / 40;
        conv_cat = i;
        conv_u1 = 0;
        conv_u2 = CATS[i].count > 1 ? 1 : 0;
        conv_entry[0] = '1';
        conv_entry[1] = 0;
        conv_active = 1;
        mode = M_CONV;
        sheet = SH_NONE;
        return 1;
    }
    row += 30 + CAT_COUNT * 40;
    if (ly >= row + 30 && ly < row + 70) {
        mode = M_ABOUT;
        sheet = SH_NONE;
        return 1;
    }
    return 1; /* taps inside the pane that hit nothing stay */
}

static int hist_tap(int x, int y)
{
    int top = 90;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    /* trash */
    if (x >= 260 && x < 300 && y >= top + 12 && y < top + 48) {
        hist_count = 0;
        return 1;
    }
    /* rows: tap = re-run (load result into the display) */
    int ry = y - (top + 56) + hist_scroll;
    if (ry >= 0) {
        int idx = hist_count - 1 - ry / 46;
        if (idx >= 0 && idx < hist_count && ry % 46 < 44) {
            /* parse the result back into the display */
            g.display = g.display; /* keep; entry cleared below */
            g.entry_active = 0;
            g.entry[0] = 0;
            sheet = SH_NONE;
            (void)idx;
        }
    }
    return 1;
}

static int mem_tap(int x, int y)
{
    int top = 90;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    if (x >= 250 && x < 300 && y >= top + 8 && y < top + 34) {
        mem_count = 0;
        return 1;
    }
    int ry = y - (top + 56) + mem_scroll;
    if (ry >= 0) {
        int idx = mem_count - 1 - ry / 54;
        int sub = ry % 54;
        if (idx >= 0 && idx < mem_count) {
            if (sub >= 30) {
                if (x >= 230 && x < 262) {       /* MC row clear */
                    for (int i = idx; i < mem_count - 1; ++i) {
                        mem_vals[i] = mem_vals[i + 1];
                        for (int j = 0; j < 24; ++j)
                            mem_str[i][j] = mem_str[i + 1][j];
                    }
                    --mem_count;
                    return 1;
                }
                if (x >= 262 && x < 292) {       /* M+ */
                    mem_vals[idx] = dec_add(mem_vals[idx], g.display);
                    dec_to_string_buf(mem_vals[idx], mem_str[idx], 20);
                    return 1;
                }
                if (x >= 292) {                  /* M- */
                    mem_vals[idx] = dec_add(mem_vals[idx], dec_neg(g.display));
                    dec_to_string_buf(mem_vals[idx], mem_str[idx], 20);
                    return 1;
                }
            } else {
                g.display = mem_vals[idx];       /* recall */
                g.entry_active = 0;
                g.entry[0] = 0;
                sheet = SH_NONE;
                return 1;
            }
        }
    }
    return 1;
}

static int units_tap(int x, int y)
{
    int top = 70;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    int idx = (y - top - 48) / 36;
    if (idx >= 0 && idx < CATS[conv_cat].count && (y - top - 48) % 36 < 34) {
        if (units_side == 1)
            conv_u1 = idx;
        else
            conv_u2 = idx;
        sheet = SH_NONE;
        return 1;
    }
    return 1;
}

static void process_tap(int x, int y)
{
    if (sheet == SH_NAV)
        { nav_tap(x, y); return; }
    if (sheet == SH_HIST)
        { hist_tap(x, y); return; }
    if (sheet == SH_MEM)
        { mem_tap(x, y); return; }
    if (sheet == SH_UNITS)
        { units_tap(x, y); return; }

    const char *doc = mode == M_SCI ? "SciPage"
                     : mode == M_CONV ? "ConvPage"
                     : mode == M_ABOUT ? "AboutPage" : "StdPage";
    const UxDoc *d = ux_find_doc(&ux, doc);
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    const UxNode *b = hit_test(d, x, y);
    if (!b)
        return;
    const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
    if (!cmd)
        return;
    if (mode == M_CONV && !xeq(cmd, "nav") && !xeq(cmd, "history") &&
        !xeq(cmd, "units:1") && !xeq(cmd, "units:2") && !xeq(cmd, "pick:1") &&
        !xeq(cmd, "pick:2") && cmd[0] != 'd' && !xeq(cmd, "op:ce") &&
        !xeq(cmd, "op:back") && !xeq(cmd, "op:negate")) {
        /* converter numpad */
        if (cmd[0] == 'd')
            do_digit(cmd[2]);
        else if (!xeq(cmd, "op:ce"))
            conv_clear_entry();
        else if (!xeq(cmd, "op:back"))
            conv_backspace();
        else if (!xeq(cmd, "op:negate")) {
            if (conv_entry[0] == '-') {
                int i = 1, j = 0;
                while (conv_entry[i])
                    conv_entry[j++] = conv_entry[i++];
                conv_entry[j] = 0;
            } else if (conv_entry[0]) {
                int len = xstrlen(conv_entry);
                for (int i = len; i >= 0; --i)
                    conv_entry[i + 1] = conv_entry[i];
                conv_entry[0] = '-';
            }
        }
        return;
    }
    if (!xeq(cmd, "units:1")) {
        units_side = 1;
        sheet = SH_UNITS;
        return;
    }
    if (!xeq(cmd, "units:2")) {
        units_side = 2;
        sheet = SH_UNITS;
        return;
    }
    if (!xeq(cmd, "pick:1")) {
        conv_active = 1;
        conv_entry[0] = 0;
        return;
    }
    if (!xeq(cmd, "pick:2")) {
        conv_active = 2;
        conv_entry[0] = 0;
        return;
    }
    std_command(cmd);
}

/* =====================================================================
 * markup load + window plumbing
 * ===================================================================== */
static const W16 ux_path[] = { '\\', '\\', '.', '\\', 'f', 'l', 's', '0',
                               '\\', 'c', 'a', 'l', 'c', '.', 'u', 'x', 'a',
                               'm', 'l', 0 };

static int load_markup(void)
{
    void *h = NT_CALL(CreateFileW, (ux_path, 0x80000000u, 1, 0, 3, 0, 0));
    if (h == (void *)-1)
        return -1;
    U32 size = NT_CALL(GetFileSize, (h, 0));
    if (size == 0 || size > sizeof(ux_blob)) {
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
static int press_x, press_y, pressed, dragging, drag_y;

static void nav_bar_action(int key)
{
    if (key == 4) { /* Back: close sheets / exit */
        if (sheet != SH_NONE) {
            sheet = SH_NONE;
        } else if (mode != M_STD) {
            mode = M_STD;
        } else {
            NT_CALL(PostQuitMessage, (0));
        }
    } else if (key == 5) { /* Home */
        mode = M_STD;
        sheet = SH_NONE;
    } else if (key == 6) { /* Search: open the nav pane */
        sheet = SH_NAV;
        nav_scroll = 0;
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
        if (wp == 0x1b) {
            nav_bar_action(4);
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
        /* press feedback on the tapped button */
        pressed_node = 0;
        if (sheet == SH_NONE && y < CHROME_Y) {
            const char *doc = mode == M_SCI ? "SciPage"
                             : mode == M_CONV ? "ConvPage"
                             : mode == M_ABOUT ? "AboutPage" : "StdPage";
            const UxDoc *d = ux_find_doc(&ux, doc);
            if (d) {
                layout_doc(d, 0, 0, 320, 528);
                pressed_node = hit_test(d, x, y);
            }
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_MOVE_) {
        int y = (int)(short)(lp >> 16);
        if (pressed) {
            int dy = drag_y - y;
            if (dragging || dy > 10 || dy < -10) {
                dragging = 1;
                if (sheet == SH_NAV)
                    nav_scroll += dy;
                else if (sheet == SH_HIST)
                    hist_scroll += dy;
                else if (sheet == SH_MEM)
                    mem_scroll += dy;
                if (nav_scroll < 0) nav_scroll = 0;
                if (hist_scroll < 0) hist_scroll = 0;
                if (mem_scroll < 0) mem_scroll = 0;
                drag_y = y;
            }
        }
        return 0;
    }
    if (msg == WM_UP_) {
        if (pressed && !dragging)
            process_tap(press_x, press_y);
        pressed = 0;
        dragging = 0;
        pressed_node = 0;
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

static const W16 cls_name[] = { 'C', 'a', 'l', 'c', 'u', 'l', 'a', 't', 'o',
                                'r', 0 };

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u;
    /* host-side IAT snap for the icon/control sources linked in here */
    __imp_render_SpGetThreadContext = (const void *)(uintptr_t)render_SpGetThreadContext;
    g_chrome = NT_CALL(UIXChromeFactory, ());
    if (!g_chrome || g_chrome->profile != 0x55495831u)
        g_chrome = 0;
    engine_init();
    conv_entry[0] = '1';
    conv_entry[1] = 0;
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
'''

d = open(SRC).read()
if 'RunApplication' in d:
    print('UI already appended')
else:
    d = d.rstrip('\n') + '\n' + UI
    open(SRC, 'w').write(d)
    print('UI appended; file now', len(d), 'bytes')
