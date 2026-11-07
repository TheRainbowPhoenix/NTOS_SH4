/* PAINT.EXE: WinCE-style COREDLL drawing and touch demonstration.
 * Uses an artificial desktop DC provided by NTOS (not a real WinCE window).
 * Touch the display to paint. CLEAR returns to the NTOS picker. */
#include "ntimport.h"

typedef unsigned int u32;
typedef struct { int left, top, right, bottom; } RECT;

NT_IMPORT(void *, GetDC, (void *hwnd))
NT_IMPORT(int, ReleaseDC, (void *hwnd, void *hdc))
NT_IMPORT(u32, SetPixel, (void *hdc, int x, int y, u32 color))
NT_IMPORT(void *, CreateSolidBrush, (u32 color))
NT_IMPORT(int, FillRect, (void *hdc, const RECT *rect, void *brush))
NT_IMPORT(int, DeleteObject, (void *object))
NT_IMPORT(void, coredll_clear, (u32 color))
NT_IMPORT(void, coredll_line, (u32 x1, u32 y1, u32 x2, u32 y2, u32 color))
NT_IMPORT(void, coredll_refresh, (void))
NT_IMPORT(u32, coredll_touch, (u32 *x, u32 *y))

NT_DEFINE(GetDC)
NT_DEFINE(ReleaseDC)
NT_DEFINE(SetPixel)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(coredll_clear)
NT_DEFINE(coredll_line)
NT_DEFINE(coredll_refresh)
NT_DEFINE(coredll_touch)

int main(void)
{
    static const u32 colors[] = {
        0x000000FFu, 0x0000FF00u, 0x00FF0000u,
        0x0000FFFFu, 0x00FF00FFu, 0x00FFFF00u
    };
    void *dc = NT_CALL(GetDC, (0));
    if (!dc) return 1;
    NT_CALL(coredll_clear, (0x00001020u));
    for (u32 i = 0; i < 6; i++) {
        RECT rect;
        rect.left = 12 + (int)(i * 46);
        rect.top = 38;
        rect.right = rect.left + 37;
        rect.bottom = 83;
        void *brush = NT_CALL(CreateSolidBrush, (colors[i]));
        NT_CALL(FillRect, (dc, &rect, brush));
        NT_CALL(DeleteObject, (brush));
    }
    NT_CALL(coredll_line, (10, 100, 300, 100, 0x00FFFFFFu));
    NT_CALL(coredll_refresh, ());
    for (;;) {
        u32 x = 0, y = 0;
        u32 type = NT_CALL(coredll_touch, (&x, &y));
        if (type == 2) break;
        if (type != 1 || x >= 320 || y >= 528) continue;
        u32 color = colors[(x / 50) % 6];
        for (int dy = -7; dy <= 7; ++dy)
            for (int dx = -7; dx <= 7; ++dx)
                if (dx*dx + dy*dy <= 49 && (int)x + dx >= 0 &&
                    (int)y + dy >= 0 && x + dx < 320 && y + dy < 528)
                    NT_CALL(SetPixel, (dc, (int)x + dx, (int)y + dy, color));
        NT_CALL(coredll_refresh, ());
    }
    NT_CALL(ReleaseDC, (0, dc));
    return 0;
}
