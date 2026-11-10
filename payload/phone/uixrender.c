/* WinCE GDI backend. No bitmap assets, XML, heap or framebuffer allocation. */
#include "phone.h"
#include "font_bitmap.h"
IMP(void *, CreateSolidBrush, (U32))
IMP(int, FillRect, (void *, const Rect *, void *))
IMP(int, DeleteObject, (void *))
static int surface_bottom = 528;
static void surface(int b)
{
    surface_bottom = clampi(b, 0, 528);
}
static Rect clipping = {0, 0, 320, 528};
/* COREDLL's CreateSolidBrush is a synthetic RGB565 handle. A shared
 * 1-entry cache avoids re-creating/deleting it on every glyph pixel run.
 * The renderer owns this handle and releases it on shutdown. */
static U32 cached_color;
static void *cached_brush;
static void clip(int l, int t, int r, int b)
{
    clipping.left = clampi(l, 0, 320);
    clipping.top = clampi(t, 0, surface_bottom);
    clipping.right = clampi(r, l, 320);
    clipping.bottom = clampi(b, clipping.top, surface_bottom);
}
static void fill(void *dc, int l, int t, int r, int b, U32 c)
{
    Rect v = {l > clipping.left ? l : clipping.left, t > clipping.top ? t : clipping.top,
              r < clipping.right ? r : clipping.right, b < clipping.bottom ? b : clipping.bottom};
    if (!dc || v.left >= v.right || v.top >= v.bottom)
        return;
    if (!cached_brush || c != cached_color) {
        if (cached_brush)
            CALL(DeleteObject, (cached_brush));
        cached_brush = CALL(CreateSolidBrush, (c));
        cached_color = c;
    }
    if (cached_brush)
        CALL(FillRect, (dc, &v, cached_brush));
}
static void text(void *dc, int x, int y, const char *s, U32 c, int h)
{
    if (!s || h < 6 || h > 56)
        return;
    for (unsigned n = 0; *s && n < 96; ++s, ++n) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 0xc2 && ch < 0xe0 && s[1] && ((unsigned char)s[1] & 0xc0) == 0x80) {
            ch = ((ch & 31u) << 6) | ((unsigned char)s[1] & 63u);
            ++s;
        } else if (ch >= 0xe0 && ch < 0xf0 && s[1] && s[2] &&
                   ((unsigned char)s[1] & 0xc0) == 0x80 && ((unsigned char)s[2] & 0xc0) == 0x80) {
            ch = ((ch & 15u) << 12) | (((unsigned char)s[1] & 63u) << 6) |
                 ((unsigned char)s[2] & 63u);
            s += 2;
        }
        if (ch < 32)
            ch = '?';
        const PhoneFont *font = &phone_fonts[3];
        int best = 999;
        for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
            const PhoneFont *f = &phone_fonts[i];
            int delta = f->height - h;
            if (delta < 0)
                delta = -delta;
            if ((int)ch >= f->first && (int)ch < f->first + f->count && delta < best) {
                font = f;
                best = delta;
            }
        }
        if (best == 999) {
            ch = '?';
            font = &phone_fonts[3];
        }
        const PhoneGlyph *g = &font->glyphs[ch - (unsigned)font->first];
        int w = g->width;
        unsigned stride = (g->width + 7u) >> 3;
        for (int yy = 0; yy < font->height; ++yy) {
            const unsigned char *row = &font->bits[g->offset + (unsigned)yy * stride];
            int run = -1;
            for (int xx = 0; xx <= w; ++xx) {
                int bit = xx < w && ((row[(unsigned)xx >> 3] >> (xx & 7)) & 1u);
                if (bit && run < 0)
                    run = xx;
                if (!bit && run >= 0) {
                    fill(dc, x + run, y + yy, x + xx, y + yy + 1, c);
                    run = -1;
                }
            }
        }
        x += g->advance;
    }
}
static void line(void *dc, int x, int y, int xx, int yy, U32 c)
{
    /* Common VEC grid/outline strokes can be one hardware rectangle,
     * not one GDI fill per pixel of a horizontal/vertical line. */
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
static void ellipse(void *dc, int cx, int cy, int rx, int ry, U32 c, int thick)
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
static void polygon(void *dc, const Pt *p, unsigned n, U32 c)
{
    if (!p || n < 3 || n > 255)
        return;
    int ymin = p[0].y, ymax = ymin;
    for (unsigned i = 1; i < n; ++i) {
        if (p[i].y < ymin)
            ymin = p[i].y;
        if (p[i].y > ymax)
            ymax = p[i].y;
    }
    ymin = clampi(ymin, clipping.top, clipping.bottom);
    ymax = clampi(ymax, clipping.top, clipping.bottom);
    for (int y = ymin; y < ymax; ++y) {
        int xs[255];
        unsigned used = 0;
        for (unsigned i = 0, j = n - 1; i < n; j = i++)
            if ((p[i].y <= y && p[j].y > y) || (p[j].y <= y && p[i].y > y))
                xs[used++] = p[i].x + div_((y - p[i].y) * (p[j].x - p[i].x), p[j].y - p[i].y);
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
static const U32 theme_light[UIX_TH_COUNT] = {
    SURFACE,              /* UIX_TH_SURFACE   */
    CARD,                 /* UIX_TH_KEY       */
    PANEL,                /* UIX_TH_KEY_ALT   */
    INK,                  /* UIX_TH_INK       */
    CARD,                 /* UIX_TH_KEY_GO    */
    INK,                  /* UIX_TH_GO_INK    */
    BLUE,                 /* UIX_TH_ACCENT    */
    RGB_(195, 215, 232),  /* UIX_TH_CTRL_BG   */
    INK,                  /* UIX_TH_CTRL_INK  */
    RGB_(124, 142, 158),  /* UIX_TH_CTRL_EDGE */
};
static const Render renderer = {PROFILE, clip, fill, text, line, ellipse,
                                polygon, surface, theme_light};
int SpRenderThreadInit(void)
{
    return 0;
}
void SpRenderThreadUninit(void)
{
    clip(0, 0, 320, 528);
    if (cached_brush) {
        CALL(DeleteObject, (cached_brush));
        cached_brush = 0;
    }
}
const Render *SpGetThreadContext(void)
{
    return &renderer;
}
const Render *SpGetThreadContextOrNull(void)
{
    return &renderer;
}
int phone_unsupported(void)
{
    return NOTIMPL;
}
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
