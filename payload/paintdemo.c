/* PAINT.EXE: WinCE GDI/message-loop demo hosted by NTOS COREDLL.
 * Uses real WinCE entry points, with a minimal virtual DC/message adapter.
 * Tap to draw circles, press CLEAR (WM_QUIT) to return to NTOS. */
#include "ntimport.h"
typedef unsigned int u32;
typedef struct {
    int left, top, right, bottom;
} RECT;
typedef struct {
    int x, y;
} POINT;
typedef struct {
    void *hwnd;
    u32 message, wParam, lParam, time;
    POINT pt;
} MSG;
typedef struct {
    void *hdc;
    int erase;
    RECT rcPaint;
    int restore, incUpdate;
    unsigned char reserved[32];
} PAINTSTRUCT;

NT_IMPORT(void *, BeginPaint, (void *hwnd, PAINTSTRUCT *paint))
NT_IMPORT(int, EndPaint, (void *hwnd, const PAINTSTRUCT *paint))
NT_IMPORT(u32, SetPixel, (void *hdc, int x, int y, u32 color))
NT_IMPORT(void *, CreateSolidBrush, (u32 color))
NT_IMPORT(int, FillRect, (void *hdc, const RECT *rect, void *brush))
NT_IMPORT(int, DeleteObject, (void *object))
NT_IMPORT(int, GetMessageW, (MSG * msg, void *hwnd, u32 min, u32 max))

NT_DEFINE(BeginPaint)
NT_DEFINE(EndPaint)
NT_DEFINE(SetPixel)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(GetMessageW)

int main(void)
{
    static const u32 palette[6] = {0x000000FFu, 0x0000FF00u, 0x00FF0000u,
                                   0x0000FFFFu, 0x00FF00FFu, 0x00FFFF00u};
    PAINTSTRUCT ps = {0};
    void *hdc = NT_CALL(BeginPaint, (0, &ps));
    if (!hdc)
        return 1;
    {
        RECT bg = {0, 0, 320, 528};
        void *brush = NT_CALL(CreateSolidBrush, (0x00201010u));
        NT_CALL(FillRect, (hdc, &bg, brush));
        NT_CALL(DeleteObject, (brush));
    }
    for (u32 i = 0; i < 6; ++i) {
        RECT r = {12 + (int)i * 48, 30, 50 + (int)i * 48, 80};
        void *brush = NT_CALL(CreateSolidBrush, (palette[i]));
        NT_CALL(FillRect, (hdc, &r, brush));
        NT_CALL(DeleteObject, (brush));
    }
    NT_CALL(EndPaint, (0, &ps));
    /* GetMessageW converts NTOS touch into WM_LBUTTONDOWN and CLEAR to
     * WM_QUIT. Avoid private coredll_* imports in this demo. */
    for (;;) {
        MSG msg = {0};
        int result = NT_CALL(GetMessageW, (&msg, 0, 0, 0));
        if (result <= 0 || msg.message == 0x0012u)
            break;
        if (msg.message == 0x0201u) {
            int cx = msg.pt.x, cy = msg.pt.y;
            u32 col = palette[((u32)cx / 50u) % 6u];
            PAINTSTRUCT paint = {0};
            void *dc = NT_CALL(BeginPaint, (0, &paint));
            if (!dc)
                break;
            for (int dy = -7; dy <= 7; ++dy)
                for (int dx = -7; dx <= 7; ++dx)
                    if (dx * dx + dy * dy <= 49 && cx + dx >= 0 && cy + dy >= 0 && cx + dx < 320 &&
                        cy + dy < 528)
                        NT_CALL(SetPixel, (dc, cx + dx, cy + dy, col));
            NT_CALL(EndPaint, (0, &paint));
        }
    }
    return 0;
}
