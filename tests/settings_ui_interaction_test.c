/* Test native Settings slider drag and pressed-button states without SH4. */
#define PHONE_HOST 1
#include "phone.h"
#include <assert.h>
#include <stdio.h>

static int button_color;
static unsigned tinted_icons;
static U32 last_icon_tint;
static void clip_mock(int a, int b, int c, int d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
static void fill_mock(void *dc, int x0, int y0, int x1, int y1, U32 color)
{
    (void)dc;
    if (x0 >= 12 && x0 <= 13 && y0 >= 439 && y0 <= 440 && x1 >= 101 && x1 <= 102 && y1 >= 470 &&
        y1 <= 471)
        button_color = (int)color;
}
static void text_mock(void *dc, int x, int y, const char *str, U32 color, int size)
{
    (void)dc;
    (void)x;
    (void)y;
    (void)str;
    (void)color;
    (void)size;
}
static void line_mock(void *dc, int x0, int y0, int x1, int y1, U32 color)
{
    (void)dc;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)color;
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
static void polygon_mock(void *dc, const Pt *pts, unsigned count, U32 color)
{
    (void)dc;
    (void)pts;
    (void)count;
    (void)color;
}
static void surface_mock(int height)
{
    (void)height;
}
static const Render render = {PROFILE,   clip_mock,    fill_mock,    text_mock,
                              line_mock, ellipse_mock, polygon_mock, surface_mock};
const Render *render_SpGetThreadContext(void)
{
    return &render;
}
int SipShowIM(U32 show)
{
    (void)show;
    return 1;
}
int svf_SVFDrawTint(void *dc, const void *data, unsigned len, int x, int y, int w, int h, U32 tint)
{
    (void)dc;
    (void)data;
    (void)len;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    ++tinted_icons;
    last_icon_tint = tint;
    return 1;
}
extern const Settings *SettingsGetView(void);
static U32 pos(int x, int y)
{
    return ((U32)(unsigned)y << 16) | (unsigned)x;
}
static void tap(const Settings *s, int x, int y)
{
    assert(s->input(WM_DOWN_, 0, pos(x, y)) == 1);
    assert(s->input(WM_UP_, 0, pos(x, y)) == 1);
}
int main(void)
{
    const Settings *s = SettingsGetView();
    assert(s && s->profile == PROFILE);
    s->reset();
    s->paint((void *)1);
    assert(tinted_icons && last_icon_tint == RGB_(0, 0, 0));
    tap(s, 85, 145); /* System */
    assert(s->page() == 1);
    tap(s, 85, 190); /* Display */
    assert(s->page() == 2);

    assert(s->input(WM_DOWN_, 0, pos(60, 181)) == 1);
    assert(s->input(WM_MOVE_, 0, pos(60, 181)) == 0);
    assert(s->input(WM_MOVE_, 0, pos(61, 181)) == 0);
    assert(s->input(WM_MOVE_, 0, pos(220, 181)) == 1);
    assert(s->input(WM_MOVE_, 0, pos(220, 181)) == 0);
    assert(s->input(WM_UP_, 0, pos(220, 181)) == 0);

    assert(s->input(WM_DOWN_, 0, pos(70, 449)) == 1);
    s->paint((void *)1);
    assert(button_color == (int)RGB_(160, 190, 216));
    assert(s->input(WM_UP_, 0, pos(70, 449)) == 1);
    s->paint((void *)1);
    assert(button_color == (int)RGB_(195, 215, 232));
    /* Scale and layout uses a proper pushed XAML-style dropdown. */
    tap(s, 280, 412);
    tap(s, 88, 278); /* 125% entry; editing is a preview, not real DPI. */
    assert(s->page() == 2);

    assert(s->input(WM_KEY_, 0x1bu, 0) == 1);
    assert(s->page() == 1);
    assert(s->input(WM_KEY_, 0x28u, 0) == 1); /* bring About into view */
    tap(s, 64, 488);
    assert(s->page() == 9); /* CP400/NTOS About, not PC specifications */

    tap(s, 288, 48); /* Search button remains touchable from About. */
    for (const char *p = "display"; *p; ++p)
        assert(s->input(WM_CHAR_, (U32)*p, 0) == 1);
    tap(s, 88, 143); /* Global search: Display from About page. */
    assert(s->page() == 2);
    puts("PASS Settings icons dark, slider moves coalesced, button pressed visibly");
    return 0;
}
