/* mdhost.c — native test host for MARKDOWN.DLL: renders a source file to a
 * PPM using the real device bitmap fonts, so layout can be iterated on
 * without the full NTOS e2e. Build:
 *   gcc -O1 -std=c11 -I payload/phone -o /tmp/mdhost tools/mdhost.c \
 *       payload/phone/markdown.c -DMDHOST_NATIVE
 * The engine exports md_* directly when compiled natively (no DLL). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "markdown.h"
#include "font_bitmap.h"

#define FB_W 480
#define FB_H 1000
static unsigned char fb[FB_W * FB_H * 3];

static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[0];
    int bd = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        int d = f->height - h;
        if (d < 0) d = -d;
        if (d < bd) { best = f; bd = d; }
    }
    return best;
}
static int tw(const char *s, int len, int h)
{
    const PhoneFont *f = pick_font(h);
    int w = 0;
    for (int i = 0; i < len; ++i) {
        int ch = (unsigned char)s[i];
        if (ch < f->first || ch >= f->first + f->count) ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}
static void px(int x, int y, unsigned c)
{
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    unsigned char *p = fb + (y * FB_W + x) * 3;
    p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255;
}
static void h_fill(void *dc, int l, int t, int r, int b, unsigned c)
{
    (void)dc;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    for (int y = t; y < b; ++y)
        for (int x = l; x < r; ++x)
            px(x, y, c);
}
/* glyph rasterization copied 1:1 from edge_app.c draw_text1 so the
 * test host is pixel-equivalent with the device renderer */
static void h_text(void *dc, int x, int y, const char *s, int len,
                   unsigned c, int h, int flags)
{
    (void)dc;
    const PhoneFont *f = pick_font(h);
    for (int pass = 0; pass < (flags & MDTF_BOLD ? 2 : 1); ++pass) {
        int cx = x + pass;
        for (int i = 0; i < len; ++i) {
            int ch = (unsigned char)s[i];
            if (ch < f->first || ch >= f->first + f->count)
                ch = '?';
            const PhoneGlyph *g = &f->glyphs[ch - f->first];
            unsigned stride = ((unsigned)g->width + 7u) >> 3;
            for (int yy = 0; yy < f->height; ++yy) {
                const unsigned char *row =
                    &f->bits[g->offset + (unsigned)yy * stride];
                int run = -1;
                for (int xx = 0; xx <= g->width; ++xx) {
                    int bit = xx < g->width &&
                              ((row[(unsigned)xx >> 3] >> (xx & 7)) & 1u);
                    if (bit && run < 0)
                        run = xx;
                    else if (!bit && run >= 0) {
                        for (int q = x + run; q < x + xx; ++q)
                            px(cx + q - x, y + yy, c);
                        run = -1;
                    }
                }
            }
            cx += g->advance + 1;
        }
    }
}
static void h_svf(void *dc, const unsigned char *data, unsigned len,
                  int x, int y, int w, int h)
{
    (void)dc; (void)data; (void)len;
    h_fill(0, x, y, x + w, y + h, 0x00AA55); /* placeholder green */
}
static void h_ellipse(void *dc, int l, int t, int r, int b, unsigned c,
                      int filled)
{
    (void)dc;
    int cx = (l + r) / 2, cy = (t + b) / 2;
    int rx = (r - l) / 2, ry = (b - t) / 2;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int y = t; y < b; ++y)
        for (int x = l; x < r; ++x) {
            int dx = (x - cx) * 100 / rx, dy = (y - cy) * 100 / ry;
            int d = dx * dx + dy * dy;
            if (filled ? d <= 10000 : (d <= 10000 && d > 6000))
                px(x, y, c);
        }
}
static int h_read(const char *name, unsigned char *buf, int cap)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", getenv("MDHOST_DIR") ? getenv("MDHOST_DIR") : ".", name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = (int)fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static int h_chakra(const char *script)
{
    if (getenv("MDHOST_VERBOSE"))
        fprintf(stderr, "[chakra] %s\n", script);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: mdhost <md|html|text> <src> <out.ppm> [view_w] [reader] [sx] [sy]\n");
        return 2;
    }
    int dialect = !strcmp(argv[1], "html") ? MD_SRC_HTML
                : !strcmp(argv[1], "text") ? MD_SRC_TEXT : MD_SRC_MD;
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 2; }
    static char src[65536];
    int n = (int)fread(src, 1, sizeof src - 1, f);
    fclose(f);
    src[n] = 0;

    int view_w = argc > 4 ? atoi(argv[4]) : 320;
    int reader = argc > 5 ? atoi(argv[5]) : 0;
    int sx = argc > 6 ? atoi(argv[6]) : 0;
    int sy = argc > 7 ? atoi(argv[7]) : 0;

    MdHost host;
    memset(&host, 0, sizeof host);
    host.dc = 0;
    host.fill = h_fill;
    host.text = h_text;
    host.text_w = tw;
    host.svf = h_svf;
    host.ellipse = h_ellipse;
    host.read_file = h_read;
    host.chakra = h_chakra;
    md_set_host(&host);

    if (!md_load(dialect, src, n)) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    md_reader(reader);
    md_layout(view_w);
    int pw, ph;
    md_size(&pw, &ph);
    const char *title;
    int tl;
    title = md_title(&tl);
    fprintf(stderr, "title=%.*s page=%dx%d ops=%d\n", tl ? tl : 0,
            title ? title : "", pw, ph, 0);
    memset(fb, 0xFF, sizeof fb);
    md_paint(&host, sx, sy, FB_W, FB_H);
    /* link probes */
    for (int y = 0; y < ph && y < FB_H; y += 8)
        for (int x = 0; x < pw && x < FB_W; x += 8) {
            const char *href;
            int hl;
            if (md_link_at(x, y, &href, &hl) && getenv("MDHOST_VERBOSE"))
                fprintf(stderr, "link@%d,%d -> %.*s\n", x, y, hl, href);
            if (md_link_at(x, y, &href, &hl)) { x += 8; y += 8; }
        }
    int fm[8 * 3];
    int nmatch = md_find("exam", fm, fm + 8, fm + 16, 8);
    (void)nmatch;

    FILE *o = fopen(argv[3], "wb");
    fprintf(o, "P6\n%d %d\n255\n", FB_W, FB_H);
    fwrite(fb, 1, sizeof fb, o);
    fclose(o);
    return 0;
}
