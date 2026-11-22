/* GDILAB.EXE: on-device integer-only GDI regression pages.
 * Five categories, multiple variations each: palette, clipping,
 * scanlines, ellipses, font sizes, and viewport/split geometry.
 * This is an interactive display test, not a benchmark or screenshot oracle.
 */
#include "wince_ui.h"
#include "nt_tiling.h"

NT_IMPORT(DWORD, SetPixel, (void *, int, int, DWORD))
NT_IMPORT(int, Rectangle, (void *, int, int, int, int))
NT_IMPORT(int, Ellipse, (void *, int, int, int, int))
NT_IMPORT(int, MoveToEx, (void *, int, int, POINT *))
NT_IMPORT(int, LineTo, (void *, int, int))
NT_DEFINE(SetPixel)
NT_DEFINE(Rectangle)
NT_DEFINE(Ellipse)
NT_DEFINE(MoveToEx)
NT_DEFINE(LineTo)

static const WCHAR16 cls[] = {'N', 'T', 'G', 'D', 'I', 'L', 'A', 'B', 0};
static const WCHAR16 title[] = {'N', 'T', 'O', 'S', ' ', 'G', 'D', 'I', ' ', 'L', 'a', 'b', 0};
static const DWORD palette[12] = {
    RGB(0, 0, 0),       RGB(255, 255, 255), RGB(255, 0, 0),
    RGB(0, 255, 0),     RGB(0, 0, 255),     RGB(255, 255, 0),
    RGB(255, 0, 255),   RGB(0, 255, 255),   RGB(31, 55, 86),
    RGB(184, 208, 234), RGB(233, 240, 246), RGB(98, 74, 44)
};
static int page, split, sideways, swapped;
static void *window;

static void label(void *dc, int x, int y, const char *s, DWORD ink)
{
    ui_text(dc, x, y, s, ink, 0);
}
static void page_colors(void *dc)
{
    label(dc, 12, 65, "RGB565 solid colours / brush lifetimes", RGB(34, 49, 70));
    for (int i = 0; i < 12; ++i) {
        int x = 12 + (i % 3) * 100, y = 103 + (i / 3) * 84;
        ui_fill(dc, x, y, x + 94, y + 74, palette[i]);
        if (i == 1)
            NT_CALL(Rectangle, (dc, x, y, x + 94, y + 74));
    }
}
static void page_clipping(void *dc)
{
    label(dc, 12, 65, "Clipped fills / invalid rectangles", RGB(34, 49, 70));
    ui_fill(dc, -60, 100, 110, 170, RGB(47, 129, 227));
    ui_fill(dc, 230, 100, 380, 170, RGB(180, 208, 94));
    ui_fill(dc, -50, 220, 360, 270, RGB(240, 190, 105));
    ui_fill(dc, 20, 280, 300, 420, RGB(233, 240, 248));
    for (int i = 0; i < 16; ++i) {
        int x = 20 + i * 18;
        int y = 287 + i * 7;
        NT_CALL(Rectangle, (dc, x - 20, y - 20, x + 22, y + 22));
    }
    for (int i = -4; i < 325; i += 8) {
        NT_CALL(SetPixel, (dc, i, 490, RGB(33, 88, 204)));
        NT_CALL(SetPixel, (dc, i, -1, RGB(33, 88, 204)));
        NT_CALL(SetPixel, (dc, i, 528, RGB(33, 88, 204)));
    }
}
static void page_lines(void *dc)
{
    label(dc, 12, 65, "Lines and one-pixel raster grid", RGB(34, 49, 70));
    for (int i = 0; i <= 18; ++i) {
        int x = 16 + i * 16;
        NT_CALL(MoveToEx, (dc, x, 105, 0));
        NT_CALL(LineTo, (dc, 304 - i * 16, 414));
    }
    for (int i = 0; i < 12; ++i) {
        int y = 430 + i * 5;
        for (int x = 16 + (i & 1); x < 303; x += 3)
            NT_CALL(SetPixel, (dc, x, y, palette[(i + x / 3) % 12]));
    }
}
static void page_ellipses(void *dc)
{
    label(dc, 12, 65, "Integer ellipse / degenerate / clipped", RGB(34, 49, 70));
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            int x = 6 + col * 78, y = 100 + row * 98;
            NT_CALL(Ellipse, (dc, x, y, x + 62, y + 78));
            if (row == col)
                NT_CALL(Ellipse, (dc, x + 10, y + 14, x + 52, y + 64));
        }
    NT_CALL(Ellipse, (dc, -30, 452, 50, 550));
    NT_CALL(Ellipse, (dc, 279, 452, 355, 560));
}
static void page_fonts(void *dc)
{
    static const int sizes[8] = {9, 11, 12, 14, 16, 20, 24, 32};
    static const char *strings[8] = {
        "09 abcdef", "11 ABCDEF", "12 012345", "14 & <> ?!",
        "16 Aa Gg", "20 Wm M", "24 NTOS", "32 GDI"
    };
    label(dc, 12, 65, "Bitmap GDI: 9..32 px, no antialiasing", RGB(34, 49, 70));
    for (int i = 0; i < 8; ++i) {
        static LOGFONTW lf;
        lf.lfHeight = -sizes[i];
        void *font = NT_CALL(CreateFontIndirectW, (&lf));
        if (font)
            NT_CALL(SelectObject, (dc, font));
        NT_CALL(SetBkMode, (dc, TRANSPARENT));
        NT_CALL(SetTextColor, (dc, RGB(27, 54, 88)));
        WCHAR16 w[32];
        int n = 0;
        while (strings[i][n] && n < 31) {
            w[n] = (WCHAR16)(unsigned char)strings[i][n];
            ++n;
        }
        w[n] = 0;
        NT_CALL(TextOutW, (dc, 16, 102 + i * 45, w, n));
    }
    NT_CALL(SelectObject, (dc, ui_smallfont));
}
static void page_panes(void *dc)
{
    NT_TILE_STATE st;
    NT_TILE_RECT r;
    nt_tile_init(&st, 320, 480, 70);
    st.split = (unsigned)split;
    st.sideways = (unsigned)sideways;
    st.swapped = (unsigned)swapped;
    label(dc, 12, 52, "Split / Rotate / Swap / Full", RGB(34, 49, 70));
    for (unsigned n = 0; n < 2; ++n) {
        if (!nt_tile_rect(&st, n, &r))
            continue;
        ui_fill(dc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2,
                n ? RGB(229, 239, 248) : RGB(248, 250, 253));
        ui_fill(dc, r.left + 2, r.top + 2, r.right - 2, r.top + 25, RGB(182, 205, 233));
        label(dc, r.left + 8, r.top + 8, n ? "Pane B" : "Pane A", RGB(22, 46, 75));
        NT_CALL(Rectangle, (dc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2));
    }
    label(dc, 12, 489, "Tap preview: split / swap / rotate", RGB(67, 86, 112));
}
static void draw(void *dc)
{
    const char *names[6] = {"01 PALETTE", "02 CLIPPING", "03 LINES",
                            "04 ELLIPSES", "05 FONT RASTER", "06 TILING"};
    ui_fill(dc, 0, 24, 320, 528, RGB(255, 255, 255));
    ui_fill(dc, 0, 24, 320, 46, RGB(231, 238, 247));
    label(dc, 12, 29, names[page], RGB(25, 45, 72));
    if (page == 0)
        page_colors(dc);
    else if (page == 1)
        page_clipping(dc);
    else if (page == 2)
        page_lines(dc);
    else if (page == 3)
        page_ellipses(dc);
    else if (page == 4)
        page_fonts(dc);
    else
        page_panes(dc);
    ui_fill(dc, 0, 508, 320, 528, RGB(232, 239, 248));
    label(dc, 9, 512, "< prev", RGB(34, 75, 130));
    label(dc, 233, 512, "next >", RGB(34, 75, 130));
}
static int wndproc(void *hwnd, DWORD msg, DWORD wp, DWORD lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            draw(dc);
            NT_CALL(EndPaint, (hwnd, &ps));
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        if (wp == VK_LEFT)
            page = (page + 5) % 6;
        else if (wp == VK_RIGHT || wp == VK_RETURN)
            page = (page + 1) % 6;
        else if (wp == '0')
            split = !split;
        else if (wp == '1')
            swapped = !swapped;
        else if (wp == '2')
            sideways = !sideways;
        else
            return 0;
    } else if (msg == WM_LBUTTONDOWN) {
        int x = (short)(lp & 0xffffu), y = (short)(lp >> 16);
        if (y >= 508) {
            page = x < 160 ? (page + 5) % 6 : (page + 1) % 6;
        } else if (page == 5 && y > 70) {
            if (!split)
                split = 1;
            else if (!swapped)
                swapped = 1;
            else {
                sideways = !sideways;
                swapped = 0;
            }
        } else
            return 0;
    } else
        return 0;
    NT_CALL(InvalidateRect, (hwnd, 0, 0));
    return 0;
}
int main(void)
{
    ui_init();
    return ui_run(cls, title, wndproc);
}
