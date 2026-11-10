/* icons.c — MDL2-style outline icons, pure integer math (see icons.h).
 *
 * Everything reduces to fill() calls: lines are thickened Bresenham,
 * circles are midpoint-8-way with a gap option, polygons are vertex
 * tables scaled by size/100. Stroke width grows with the icon box so
 * glyphs stay visible on the 320x528 LCD.
 */
#include "icons.h"

/* ---- stroke primitives ---- */

static int iabs_(int v)
{
    return v < 0 ? -v : v;
}

void ic_line(IconFillFn f, void *dc, int x0, int y0, int x1, int y1,
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
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ic_rect(IconFillFn f, void *dc, int x, int y, int w, int h,
             int t, unsigned c)
{
    ic_line(f, dc, x, y, x + w - 1, y, t, c);
    ic_line(f, dc, x, y + h - 1, x + w - 1, y + h - 1, t, c);
    ic_line(f, dc, x, y, x, y + h - 1, t, c);
    ic_line(f, dc, x + w - 1, y, x + w - 1, y + h - 1, t, c);
}

void ic_circle(IconFillFn f, void *dc, int cx, int cy, int r,
               int t, unsigned c)
{
    /* midpoint circle, 8-way symmetry; r < 3 falls back to a dot */
    if (r < 3) {
        ic_dot(f, dc, cx, cy, r, c);
        return;
    }
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        /* 8 octant points, each thickened as a t x t block */
        int px[8] = {+x, +y, +y, +x, -x, -y, -y, -x};
        int py[8] = {+y, +x, +x, -y, -y, -x, -x, +y};
        for (int i = 0; i < 8; ++i)
            f(dc, cx + px[i] - t / 2, cy + py[i] - t / 2,
              cx + px[i] + (t + 1) / 2, cy + py[i] + (t + 1) / 2, c);
        ++y;
        if (err < 0)
            err += 2 * y + 1;
        else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void ic_dot(IconFillFn f, void *dc, int cx, int cy, int r, unsigned c)
{
    if (r < 1) {
        f(dc, cx, cy, cx + 1, cy + 1, c);
        return;
    }
    for (int dy = -r; dy <= r; ++dy) {
        int dx = 0;
        /* integer sqrt for the row extent */
        int rem = r * r - dy * dy;
        while ((dx + 1) * (dx + 1) <= rem)
            ++dx;
        f(dc, cx - dx, cy + dy, cx + dx + 1, cy + dy + 1, c);
    }
}

/* half-disc / arc helper for padlock shackle and pin head (integer sqrt) */
static void ic_arc_up(IconFillFn f, void *dc, int cx, int cy, int r,
                      int t, unsigned c)
{
    for (int dx = -r; dx <= r; ++dx) {
        int rem = r * r - dx * dx;
        int dy = 0;
        while ((dy + 1) * (dy + 1) <= rem)
            ++dy;
        /* upper half: from top of circle down to the horizontal diameter */
        for (int k = 0; k < t; ++k) {
            int yy = cy - dy + k;
            if (yy <= cy)
                f(dc, cx + dx, yy, cx + dx + 1, yy + 1, c);
        }
    }
}

/* ---- star polygon (unit outer r=100, computed offline; see icons.c) ---- */
static const signed char STAR[10][2] = {
    {0, -100}, {26, -36}, {95, -31}, {42, 14}, {59, 81},
    {0, 44}, {-59, 81}, {-42, 14}, {-95, -31}, {-26, -36},
};

static void ic_poly(IconFillFn f, void *dc, int cx, int cy, int scale100,
                    const signed char *pts, int n, int t, unsigned c)
{
    for (int i = 0; i < n; ++i) {
        const signed char *a = &pts[2 * i];
        const signed char *b = &pts[2 * ((i + 1) % n)];
        ic_line(f, dc, cx + a[0] * scale100 / 100, cy + a[1] * scale100 / 100,
                cx + b[0] * scale100 / 100, cy + b[1] * scale100 / 100, t, c);
    }
}

/* ---- icon names (markup Text on a UX_GLYPH) ---- */
static const struct {
    const char *name;
    int id;
} ICON_NAMES[] = {
    {"tabs", IC_TABS},       {"reload", IC_RELOAD},   {"lock", IC_LOCK},
    {"reader", IC_READER},   {"more", IC_MORE},       {"star", IC_STAR},
    {"book", IC_BOOK},       {"clock", IC_CLOCK},     {"download", IC_DOWNLOAD},
    {"plus", IC_PLUS},       {"inprivate", IC_INPRIVATE}, {"close", IC_CLOSE},
    {"share", IC_SHARE},     {"find", IC_FIND},       {"search", IC_FIND},
    {"pin", IC_PIN},         {"forward", IC_FORWARD}, {"back", IC_BACK},
    {"check", IC_CHECK},     {"chevron", IC_CHEVRON}, {"chevronl", IC_CHEVL},
    {"chevd", IC_CHEVD},    {"chevu", IC_CHEVU},
    {"settings", IC_SETTINGS}, {"readlist", IC_READLIST},
};

int ic_find(const char *name)
{
    if (!name)
        return IC_NONE;
    for (unsigned i = 0; i < sizeof(ICON_NAMES) / sizeof(ICON_NAMES[0]); ++i) {
        const char *a = ICON_NAMES[i].name, *b = name;
        while (*a && *a == *b) {
            ++a;
            ++b;
        }
        if (!*a && !*b)
            return ICON_NAMES[i].id;
    }
    return IC_NONE;
}

/* ---- per-icon drawing: all boxes are (x, y, size, size) ---- */

void ic_draw(IconFillFn f, void *dc, int x, int y, int size,
             unsigned color, int id)
{
    if (size < 4)
        return;
    int cx = x + size / 2, cy = y + size / 2;
    /* stroke thickness: 2px for toolbar-size (16..32px) icons, 1px for the
     * small card-header glyphs (13px new-tab icon, 12px close cross) —
     * per the reference stroke audit (user feedback, 2px/1px lines). */
    int t = size / 16 + 1;          /* stroke thickness */
    int s = size;                   /* box */

    switch (id) {
    case IC_MORE:
        /* three 3px dots, pitch 7 (address bar: x289/296/303 at y503) */
        ic_dot(f, dc, cx - 7, cy, 1, color);
        ic_dot(f, dc, cx, cy, 1, color);
        ic_dot(f, dc, cx + 7, cy, 1, color);
        break;
    case IC_PLUS:
        /* arms to 45% of the box: the tab-center + is ~19px wide at 21px
         * (refs x160-180), 2px stroke */
        ic_line(f, dc, cx - s * 9 / 20, cy, cx + s * 9 / 20, cy, t, color);
        ic_line(f, dc, cx, cy - s * 9 / 20, cx, cy + s * 9 / 20, t, color);
        break;
    case IC_CLOSE:
        ic_line(f, dc, cx - s / 4, cy - s / 4, cx + s / 4, cy + s / 4, t, color);
        ic_line(f, dc, cx + s / 4, cy - s / 4, cx - s / 4, cy + s / 4, t, color);
        break;
    case IC_FORWARD:
    case IC_BACK: {
        int dir = id == IC_FORWARD ? 1 : -1;
        int x0 = cx - dir * s * 3 / 10, x1 = cx + dir * s * 3 / 10;
        int ah = s / 4;
        ic_line(f, dc, x0, cy, x1, cy, t, color);
        ic_line(f, dc, x1 - dir * ah, cy - ah, x1, cy, t, color);
        ic_line(f, dc, x1 - dir * ah, cy + ah, x1, cy, t, color);
        break;
    }
    case IC_CHEVRON: {
        int ah = s / 4;
        ic_line(f, dc, cx - ah / 2, cy - ah, cx + ah / 2, cy, t, color);
        ic_line(f, dc, cx - ah / 2, cy + ah, cx + ah / 2, cy, t, color);
        break;
    }
    case IC_CHEVL: {
        int ah = s / 4;
        ic_line(f, dc, cx + ah / 2, cy - ah, cx - ah / 2, cy, t, color);
        ic_line(f, dc, cx + ah / 2, cy + ah, cx - ah / 2, cy, t, color);
        break;
    }
    case IC_CHEVD: {
        int ah = s / 4;
        ic_line(f, dc, cx - ah, cy - ah / 2, cx, cy + ah / 2, t, color);
        ic_line(f, dc, cx + ah, cy - ah / 2, cx, cy + ah / 2, t, color);
        break;
    }
    case IC_CHEVU: {
        int ah = s / 4;
        ic_line(f, dc, cx - ah, cy + ah / 2, cx, cy - ah / 2, t, color);
        ic_line(f, dc, cx + ah, cy + ah / 2, cx, cy - ah / 2, t, color);
        break;
    }
    case IC_READLIST: {
        /* stacked reading-list lines with a hooked first line, as on the
         * W10M reading-list hero glyph and hub pivot */
        int lw = s * 7 / 10;
        ic_line(f, dc, cx - lw / 2 + s / 10, cy - s * 2 / 5, cx + lw / 2,
                cy - s * 2 / 5, t, color);
        ic_line(f, dc, cx - lw / 2 + s / 10, cy - s * 2 / 5,
                cx - lw / 2 + s / 10, cy - s * 3 / 10, t, color);
        ic_line(f, dc, cx - lw / 2, cy - s / 10, cx + lw / 2, cy - s / 10, t, color);
        ic_line(f, dc, cx - lw / 2, cy + s / 5, cx + lw / 2, cy + s / 5, t, color);
        ic_line(f, dc, cx - lw / 2, cy + s * 2 / 5 - t, cx + lw / 2 - s / 5,
                cy + s * 2 / 5 - t, t, color);
        break;
    }
    case IC_CHECK: {
        ic_line(f, dc, cx - s / 3, cy, cx - s / 10, cy + s / 4, t, color);
        ic_line(f, dc, cx - s / 10, cy + s / 4, cx + s / 3, cy - s / 4, t, color);
        break;
    }
    case IC_TABS: {
        int w = s * 7 / 10, h = s * 9 / 10;
        ic_rect(f, dc, cx - w / 2 + s / 8, cy - h / 2 - s / 10, w, h - s / 10,
                t, color);
        ic_rect(f, dc, cx - w / 2 - s / 8, cy - h / 2 + s / 10, w, h - s / 10,
                t, color);
        break;
    }
    case IC_RELOAD: {
        int r = s * 3 / 10;
        /* ring with a gap in the upper-right octant */
        int px = r, py = 0, err = 1 - r;
        while (px >= py) {
            int qx[8] = {+px, +py, +py, +px, -px, -py, -py, -px};
            int qy[8] = {+py, +px, +px, -py, -py, -px, -px, +py};
            for (int i = 0; i < 8; ++i) {
                if (i == 1 || i == 2) /* skip NE octants -> arrow goes here */
                    continue;
                f(dc, cx + qx[i] - t / 2, cy + qy[i] - t / 2,
                  cx + qx[i] + (t + 1) / 2, cy + qy[i] + (t + 1) / 2, color);
            }
            ++py;
            if (err < 0)
                err += 2 * py + 1;
            else {
                --px;
                err += 2 * (py - px) + 1;
            }
        }
        /* arrowhead at the gap (pointing clockwise/down-right) */
        int ax = cx + r * 7 / 10, ay = cy - r * 7 / 10;
        ic_line(f, dc, ax - s / 5, ay - s / 8, ax, ay, t, color);
        ic_line(f, dc, ax + s / 8, ay - s / 5, ax, ay, t, color);
        break;
    }
    case IC_LOCK: {
        int w = s * 3 / 5, h = s * 2 / 5;
        ic_rect(f, dc, cx - w / 2, cy - h / 10, w, h, t, color);
        ic_arc_up(f, dc, cx, cy - h / 10, w / 3, t, color);
        ic_dot(f, dc, cx, cy + h / 5, 1 + s / 24, color);
        break;
    }
    case IC_READER: {
        /* open book: two pages around a center spine */
        int pw = s * 7 / 20, ph = s * 3 / 10;
        ic_line(f, dc, cx, cy - ph / 2, cx, cy + ph / 2, t, color);
        ic_line(f, dc, cx - pw, cy - ph / 2 + s / 12, cx, cy - ph / 2, t, color);
        ic_line(f, dc, cx - pw, cy - ph / 2 + s / 12, cx - pw,
                cy + ph / 2 - s / 12, t, color);
        ic_line(f, dc, cx - pw, cy + ph / 2 - s / 12, cx, cy + ph / 2, t, color);
        ic_line(f, dc, cx + pw, cy - ph / 2 + s / 12, cx, cy - ph / 2, t, color);
        ic_line(f, dc, cx + pw, cy - ph / 2 + s / 12, cx + pw,
                cy + ph / 2 - s / 12, t, color);
        ic_line(f, dc, cx + pw, cy + ph / 2 - s / 12, cx, cy + ph / 2, t, color);
        break;
    }
    case IC_BOOK: {
        /* closed book: cover + spine + page line */
        int w = s * 3 / 5, h = s * 4 / 5;
        ic_rect(f, dc, cx - w / 2, cy - h / 2, w, h, t, color);
        ic_line(f, dc, cx - w / 2 + w / 3, cy - h / 2, cx - w / 2 + w / 3,
                cy + h / 2, t, color);
        break;
    }
    case IC_CLOCK: {
        int r = s * 3 / 10;
        ic_circle(f, dc, cx, cy, r, t, color);
        ic_line(f, dc, cx, cy, cx, cy - r * 2 / 3, t, color);
        ic_line(f, dc, cx, cy, cx + r * 2 / 3, cy + r / 3, t, color);
        break;
    }
    case IC_DOWNLOAD: {
        ic_line(f, dc, cx, cy - s * 2 / 5, cx, cy + s / 6, t, color);
        ic_line(f, dc, cx - s / 5, cy - s / 12, cx, cy + s / 5, t, color);
        ic_line(f, dc, cx + s / 5, cy - s / 12, cx, cy + s / 5, t, color);
        ic_line(f, dc, cx - s * 7 / 20, cy + s * 2 / 5, cx + s * 7 / 20,
                cy + s * 2 / 5, t, color);
        break;
    }
    case IC_SHARE: {
        int bw = s * 3 / 5;
        ic_line(f, dc, cx, cy - s * 2 / 5, cx, cy, t, color);
        ic_line(f, dc, cx - s / 6, cy - s / 6, cx, cy - s * 2 / 5, t, color);
        ic_line(f, dc, cx + s / 6, cy - s / 6, cx, cy - s * 2 / 5, t, color);
        ic_line(f, dc, cx - bw / 2, cy + s / 8, cx - bw / 2, cy + s * 2 / 5,
                t, color);
        ic_line(f, dc, cx + bw / 2, cy + s / 8, cx + bw / 2, cy + s * 2 / 5,
                t, color);
        ic_line(f, dc, cx - bw / 2, cy + s * 2 / 5, cx + bw / 2, cy + s * 2 / 5,
                t, color);
        ic_line(f, dc, cx - bw / 2, cy + s / 8, cx - bw / 4, cy, t, color);
        ic_line(f, dc, cx + bw / 2, cy + s / 8, cx + bw / 4, cy, t, color);
        break;
    }
    case IC_FIND: {
        int r = s / 4;
        ic_circle(f, dc, cx - s / 8, cy - s / 8, r, t, color);
        ic_line(f, dc, cx + s / 18, cy + s / 18, cx + s * 7 / 20,
                cy + s * 7 / 20, t, color);
        break;
    }
    case IC_PIN: {
        int r = s / 5;
        ic_circle(f, dc, cx, cy - s / 5, r, t, color);
        ic_line(f, dc, cx, cy - s / 5 + r, cx, cy + s * 2 / 5, t, color);
        ic_dot(f, dc, cx, cy - s / 5, 1 + s / 32, color);
        break;
    }
    case IC_INPRIVATE: {
        /* square + hat line + '/' hatch stripes in the body (refs: the
         * tab-center toolbar icon, 21px, white 2px outline, striped body) */
        int w = s * 3 / 4, h = s * 5 / 8;
        ic_rect(f, dc, cx - w / 2, cy - h / 2, w, h, t, color);
        ic_line(f, dc, cx - w / 2, cy - h / 2 + h / 3, cx + w / 2,
                cy - h / 2 + h / 3, t, color);
        for (int k = -1; k <= 1; ++k)
            ic_line(f, dc, cx - w / 3 + k * (w / 5), cy + h / 2,
                    cx + w / 3 + k * (w / 5), cy - h / 2 + h / 3, t, color);
        break;
    }
    case IC_STAR:
        ic_poly(f, dc, cx, cy, s * 21 / 50, &STAR[0][0], 10, t, color);
        break;
    case IC_SETTINGS: {
        int r = s * 3 / 10;
        ic_circle(f, dc, cx, cy, r, t, color);
        for (int i = 0; i < 8; ++i) {
            /* 8 teeth at 45-degree steps: integer directions (1,0)(1,1)... */
            static const signed char DIR[8][2] = {
                {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1},
                {1, -1}};
            int len = s / 2;
            ic_line(f, dc, cx + DIR[i][0] * r * 4 / 3,
                    cy + DIR[i][1] * r * 4 / 3, cx + DIR[i][0] * len,
                    cy + DIR[i][1] * len, t, color);
        }
        break;
    }
    default:
        break;
    }
}
