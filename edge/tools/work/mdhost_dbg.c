/* mdhost_dbg.c — native debug host for the MARKDOWN engine.
 * Compiles markdown.c directly (MD_STATIC), loads a source file, lays
 * out at 320 and dumps every paint op with coordinates so layout bugs
 * are visible without screenshots. Also renders a PNG preview via the
 * check_shots-compatible ASCII dump. */
#ifndef MD_STATIC
#define MD_STATIC
#endif
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* real device font metrics (same as edge_app.c) */
#include "font_bitmap.h"
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

static void fill(void *dc, int l, int t, int r, int b, unsigned c)
{
    printf("[fill] %d,%d -> %d,%d  c=%06x\n", l, t, r, b, c);
}
static void txt(void *dc, int x, int y, const char *s, int len,
                unsigned c, int h, int flags)
{
    printf("[text] %d,%d h=%d f=%d c=%06x '%.*s'\n", x, y, h, flags, c,
           len > 60 ? 60 : len, s);
}
static int tw(const char *s, int len, int h)
{
    char tmp[110];
    if (len > 105) len = 105;
    for (int i = 0; i < len; ++i) tmp[i] = s[i];
    tmp[len] = 0;
    const PhoneFont *f = pick_font(h);
    int w = 0;
    for (char *p = tmp; *p; ++p) {
        int ch = (unsigned char)*p;
        if (ch < f->first || ch >= f->first + f->count) ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}
static void svfd(void *dc, const unsigned char *data, unsigned len,
                 int x, int y, int w, int h)
{
    printf("[svf ] %d,%d %dx%d len=%u\n", x, y, w, h, len);
}
static void ell(void *dc, int l, int t, int r, int b, unsigned c, int f)
{
    printf("[ell ] %d,%d -> %d,%d c=%06x f=%d\n", l, t, r, b, c, f);
}
static int rf(const char *name, unsigned char *buf, int cap)
{
    FILE *f = fopen(name, "rb");
    if (!f)
        return 0;
    int n = (int)fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static int chakra(const char *s)
{
    printf("[chakra] <<%s>>\n", s);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <md|html|text> file [view_w]\n", argv[0]);
        return 2;
    }
    int dialect = !strcmp(argv[1], "html")  ? MD_SRC_HTML
                  : !strcmp(argv[1], "text") ? MD_SRC_TEXT
                                             : MD_SRC_MD;
    static char src[256 * 1024];
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
        perror(argv[2]);
        return 2;
    }
    int n = (int)fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = 0;

    MdHost host = {0};
    host.fill = fill;
    host.text = txt;
    host.text_w = tw;
    host.svf = svfd;
    host.ellipse = ell;
    host.read_file = rf;
    host.chakra = chakra;
    md_set_host(&host);
    if (!md_load(dialect, src, n)) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    int view_w = argc > 3 ? atoi(argv[3]) : 320;
    md_layout(view_w);
    int w, h;
    md_size(&w, &h);
    printf("== page %dx%d ==\n", w, h);
    int vh = argc > 4 ? atoi(argv[4]) : h;
    md_paint(&host, 0, 0, view_w, vh);
    extern int MDDbgNOps(void);
    extern int MDDbgNImg(void);
    printf("== dbg nops=%d nimg=%d ==\n", MDDbgNOps(), MDDbgNImg());
    int tl;
    const char *t = md_title(&tl);
    if (t)
        printf("== title '%.*s' ==\n", tl, t);
    return 0;
}
