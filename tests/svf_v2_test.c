/* Host-side SVF decoder contract for VEC2 two-tone and integer primitives.
 * Exercises actual payload/phone/svf.c, not a reimplementation. */
#define PHONE_HOST 1
#include "phone.h"
#include <assert.h>
#include <stdio.h>

static unsigned line_calls, primary_calls, half_calls, fill_calls;
static void clip_mock(int a, int b, int c, int d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
static void fill_mock(void *dc, int a, int b, int c, int d, U32 color)
{
    (void)dc;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)color;
    ++fill_calls;
}
static void text_mock(void *dc, int x, int y, const char *text, U32 color, int scale)
{
    (void)dc;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    (void)scale;
}
static void line_mock(void *dc, int x, int y, int xx, int yy, U32 color)
{
    (void)dc;
    (void)x;
    (void)y;
    (void)xx;
    (void)yy;
    ++line_calls;
    if (color == NAV_INK)
        ++primary_calls;
    if (color == RGB_(127, 127, 127))
        ++half_calls;
}
static void ellipse_mock(void *dc, int x, int y, int rx, int ry, U32 color, int filled)
{
    (void)dc;
    (void)x;
    (void)y;
    (void)rx;
    (void)ry;
    (void)color;
    (void)filled;
}
static void polygon_mock(void *dc, const Pt *pt, unsigned n, U32 color)
{
    (void)dc;
    (void)pt;
    (void)n;
    (void)color;
}
static void surface_mock(int n)
{
    (void)n;
}
static const Render render = {PROFILE,   clip_mock,    fill_mock,    text_mock,
                              line_mock, ellipse_mock, polygon_mock, surface_mock};
const Render *render_SpGetThreadContext(void)
{
    return &render;
}
extern int SVFValidate(const void *, unsigned);
extern int SVFDrawTint(void *, const void *, unsigned, int, int, int, int, U32);
extern int SVFDraw(void *, const void *, unsigned, int, int, int, int);
static const unsigned char doc[] = {'V',  'E',  'C',  '2',  0x60, 0,    32,   0,    32,   0x01,
                                    0xff, 0xff, 0x7f, 0xff, 0x31, 0,    2,    0,    2,    0,
                                    30,   0,    30,   2,    0x01, 0xff, 0xff, 0x42, 0x10, 0x22,
                                    0,    16,   0,    16,   0,    10,   0,    8,    2,    0x00};
int main(void)
{
    assert(SVFValidate(doc, sizeof(doc)));
    for (unsigned size = 17; size <= 32; size += size == 17 ? 7 : 8) {
        line_calls = primary_calls = half_calls = fill_calls = 0;
        assert(SVFDrawTint((void *)1, doc, sizeof(doc), 0, 0, (int)size, (int)size, NAV_INK));
        assert(line_calls > 8 && line_calls < 100);
        assert(primary_calls && half_calls);
        assert(SVFDraw((void *)1, doc, sizeof(doc), 0, 0, (int)size, (int)size));
    }
    unsigned char invalid[sizeof(doc)];
    for (unsigned i = 0; i < sizeof(doc); ++i)
        invalid[i] = doc[i];
    invalid[sizeof(doc) - 2] = 0; /* invalid zero-width arc */
    assert(!SVFValidate(invalid, sizeof(invalid)));
    invalid[sizeof(doc) - 2] = 2;
    invalid[sizeof(doc) - 4] = 33; /* arc start sample >=32 */
    assert(!SVFValidate(invalid, sizeof(invalid)));
    puts("PASS SVF VEC1/VEC2 stroke/arc/two-tone at 17,24,32px");
    return 0;
}
