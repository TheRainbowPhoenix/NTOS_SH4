/* Host regression of the *shipping* native COREDLL pixel/shape raster.
 * The fake NTOS HAL refuses every out-of-bounds coordinate: tests therefore
 * catch unsafe GDI calls before they reach an SH7305 framebuffer. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#define GetLastError nt_test_GetLastError
#define SetLastError nt_test_SetLastError
#define CloseHandle nt_test_CloseHandle
#define LocalFree nt_test_LocalFree
#define NTOS_CORE_HOST_TEST
#define main native_gdi_dll_entry
#include "../payload/coredll.c"
#undef main

const void *ntos_host_table[28];
static unsigned short pixels[528][320];
static unsigned pixel_calls, fill_calls, refresh_calls, line_calls;

static void fake_pixel(u32 x, u32 y, u32 rgb)
{
    assert(x < 320 && y < 528 && rgb <= 65535u);
    pixels[y][x] = (unsigned short)rgb;
    ++pixel_calls;
}
static void fake_fill(u32 l, u32 t, u32 r, u32 b, u32 rgb)
{
    assert(l < r && t < b && r <= 320 && b <= 528 && rgb <= 65535u);
    ++fill_calls;
    for (u32 y = t; y < b; ++y)
        for (u32 x = l; x < r; ++x)
            pixels[y][x] = (unsigned short)rgb;
}
static void fake_line(u32 x0, u32 y0, u32 x1, u32 y1, u32 rgb)
{
    assert(x0 < 320 && x1 < 320 && y0 < 528 && y1 < 528 && rgb <= 65535u);
    ++line_calls;
}
static void fake_refresh(void)
{
    ++refresh_calls;
}
static void reset(void)
{
    memset(pixels, 0, sizeof(pixels));
    pixel_calls = fill_calls = refresh_calls = line_calls = 0;
}
static unsigned count_color(unsigned short color)
{
    unsigned found = 0;
    for (unsigned y = 0; y < 528; ++y)
        for (unsigned x = 0; x < 320; ++x)
            found += pixels[y][x] == color;
    return found;
}
static void test_pixel(void)
{
    reset();
    assert(SetPixel((void *)1, 0, 0, 0x000000ffu) == 0x000000ffu);
    assert(SetPixel((void *)1, 319, 527, 0x0000ff00u) == 0x0000ff00u);
    assert(pixels[0][0] == 0xf800u && pixels[527][319] == 0x07e0u);
    assert(SetPixel((void *)1, -1, 0, 0) == 0);
    SetPixel((void *)1, 320, 0, 0);
    SetPixel((void *)1, 0, 528, 0);
    SetPixel((void *)1, 0, -1, 0);
    SetPixel(0, 4, 4, 0);
    assert(pixel_calls == 2);
}
static void test_rectangles(void)
{
    reset();
    void *red = CreateSolidBrush(0x000000ffu);
    NT_RECT r = {-200, -200, 20, 30};
    assert(FillRect((void *)1, &r, red));
    assert(fill_calls == 1 && count_color(0xf800u) == 600);
    assert(!FillRect((void *)1, &r, 0));
    NT_RECT outside = {325, 0, 355, 15};
    assert(!FillRect((void *)1, &outside, red));
    NT_RECT inverted = {20, 20, 10, 10};
    assert(!FillRect((void *)1, &inverted, red));
    NT_RECT clipped = {310, 520, 500, 800};
    assert(FillRect((void *)1, &clipped, red));
    assert(pixels[527][319] == 0xf800u);
    assert(count_color(0xf800u) == 680);
    assert(Rectangle((void *)1, 15, 15, 25, 25));
    assert(pixels[15][15] == 0xffffu && pixels[24][24] == 0xffffu);
    assert(pixels[20][20] == 0);
    assert(Rectangle((void *)1, -3, -3, 3, 3));
    assert(pixels[0][2] == 0xffffu);
    assert(!Rectangle((void *)1, 500, 500, 520, 520));
    assert(DeleteObject(red));
}
static void test_lines(void)
{
    reset();
    NT_POINT old = {0, 0};
    assert(MoveToEx((void *)1, 10, 10, &old));
    assert(LineTo((void *)1, 100, 100));
    assert(line_calls == 1);
    assert(MoveToEx((void *)1, -3, 30, &old));
    assert(old.x == 100 && old.y == 100);
    assert(LineTo((void *)1, 6, 30));
    assert(count_color(0xffffu) == 7);
    assert(pixels[30][0] == 0xffffu && pixels[30][6] == 0xffffu);
    assert(LineTo((void *)1, INT_MAX, INT_MAX));
    NT_POINT path[3] = {{5, 5}, {12, 5}, {12, 12}};
    assert(MoveToEx((void *)1, 100, 100, 0));
    assert(Polyline((void *)1, path, 3));
    assert(MoveToEx((void *)1, 0, 0, &old));
    assert(old.x == 100 && old.y == 100);
    assert(line_calls == 3);
    assert(!Polyline((void *)1, 0, 3));
    assert(!Polyline((void *)1, path, 1));
}
static void test_ellipses(void)
{
    reset();
    assert(Ellipse((void *)1, 50, 50, 151, 151));
    assert(pixels[50][93] == 0xffffu && pixels[50][107] == 0xffffu);
    assert(pixels[150][93] == 0xffffu && pixels[150][107] == 0xffffu);
    assert(pixels[100][50] == 0xffffu);
    assert(pixels[100][150] == 0xffffu);
    assert(pixel_calls >= 180 && pixel_calls <= 250);
    assert(pixels[100][100] == 0);
    reset();
    assert(Ellipse((void *)1, -50, -25, 30, 40));
    assert(pixel_calls > 0);
    assert(Ellipse((void *)1, 300, 500, 355, 559));
    assert(pixel_calls > 0);
    assert(!Ellipse((void *)1, 0, 0, 0, 50));
    assert(!Ellipse((void *)1, 0, 0, 5000, 1));
    assert(!Ellipse((void *)1, -50000, 0, 20, 200));
    assert(!Ellipse((void *)1, INT_MIN, 0, INT_MAX, 200));
}
static void test_text(void)
{
    reset();
    static const u16 label[] = {'N', 'T', 'O', 'S'};
    NT_LOGFONTW lf = {0};
    lf.lfHeight = -16;
    void *font = CreateFontIndirectW(&lf);
    assert(font);
    assert(SelectObject((void *)1, font));
    assert(SetBkMode((void *)1, 1) == 1);
    assert(SetTextColor((void *)1, 0x000000ffu) == 0);
    assert(TextOutW((void *)1, 30, 40, label, 4));
    assert(pixel_calls > 40);
    assert(count_color(0xf800u) > 40);
    NT_RECT rr = {0, 0, 320, 26};
    assert(DrawTextW((void *)1, label, -1, &rr, 1u | 4u) > 0);
    assert(DrawTextW((void *)1, label, -1, &rr, 0x400u) > 0);
    assert(rr.right > rr.left);
}
int main(void)
{
    ntos_host_table[2] = (const void *)fake_refresh;
    ntos_host_table[7] = (const void *)fake_pixel;
    ntos_host_table[8] = (const void *)fake_line;
    ntos_host_table[9] = (const void *)fake_fill;
    test_pixel();
    test_rectangles();
    test_lines();
    test_ellipses();
    test_text();
    return 0;
}
