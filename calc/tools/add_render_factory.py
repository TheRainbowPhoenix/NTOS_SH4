#!/usr/bin/env python3
"""add_render_factory.py — export render_SpGetThreadContext from calc.dll.

SHELLCHROME's hosted nav bar paints through the HOSTING APP's renderer
(that is how the strip gets themed per app), so every hosted app must
export the render ABI. Mirrors settings_app.c's r_* wrappers.
"""
SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

if 'render_SpGetThreadContext(void)' in d:
    print('factory already present')
    raise SystemExit(0)

ANCHOR = '/* ---- app modes / sheets ---- */'
assert ANCHOR in d

BLOCK = r'''/* ---- hosted-render ABI export (SHELLCHROME paints through us) ---- */
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
    if (rx < 1 || ry < 1 || rx > 100 || ry > 100)
        return;
    int ix = rx - thick, iy = ry - thick;
    for (int y = -ry; y <= ry; ++y) {
        int run = -999;
        for (int x = -rx; x <= rx; ++x) {
            int inside = (x * x * 100) / (rx * rx) + (y * y * 100) / (ry * ry) <= 100;
            int inner = ix > 0 && iy > 0 &&
                        (x * x * 100) / (ix * ix) + (y * y * 100) / (iy * iy) <= 100;
            if (inside && !inner && run == -999)
                run = x;
            else if ((!inside || inner) && run != -999) {
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

static const U32 calc_theme[] = {
    RGB_(0xF2, 0xF2, 0xF2), /* 0 SURFACE   keyboard backdrop (light) */
    RGB_(0xFF, 0xFF, 0xFF), /* 1 KEY        normal key               */
    RGB_(0xE6, 0xE6, 0xE6), /* 2 KEY_ALT    shift/&123               */
    RGB_(0x00, 0x00, 0x00), /* 3 INK        key text                 */
    RGB_(0x39, 0x65, 0xFF), /* 4 KEY_GO     enter key fill (accent)  */
    RGB_(0xFF, 0xFF, 0xFF), /* 5 GO_INK     glyph on the go key      */
    RGB_(0x39, 0x65, 0xFF), /* 6 ACCENT     checked/caret/links      */
    RGB_(0xCE, 0xCF, 0xCE), /* 7 CTRL_BG    button fill              */
    RGB_(0x00, 0x00, 0x00), /* 8 CTRL_INK   button label             */
    RGB_(0x66, 0x66, 0x66), /* 9 CTRL_EDGE  unchecked border         */
    RGB_(0x76, 0x76, 0x76), /* 10 FIELD_EDGE input border/hint      */
    RGB_(0xCE, 0xCF, 0xCE), /* 11 NAV_BG    nav bar strip (light)    */
    RGB_(0x31, 0x30, 0x31), /* 12 NAV_INK   nav bar glyphs           */
};

static const CalcRender calc_renderer = {
    0x55495831u, r_clip, r_fill, r_text, r_line,
    r_ellipse, r_polygon, r_surface, calc_theme,
};

__attribute__((dllexport)) const CalcRender *render_SpGetThreadContext(void)
{
    return &calc_renderer;
}

'''

d = d.replace(ANCHOR, BLOCK + ANCHOR, 1)
open(SRC, 'w').write(d)
print('render factory added')
