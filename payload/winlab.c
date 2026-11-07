/* WINLAB.EXE: a WinCE GDI + keypad + stylus + filesystem demo.
 * Standard WinCE API names only; COREDLL bridges them to NTOS.
 *
 * Touch the canvas to stamp multicolor rings. Tap color swatches at top.
 * LEFT/RIGHT changes color; UP/DOWN changes brush size; 0 clears;
 * EXE saves state; 1 loads state; CLEAR exits to the NTOS picker.
 * Save file: \fls0\NTLAB.DAT (short fixed-size state, not a bitmap).
 */
#include "ntimport.h"

typedef unsigned int u32;
typedef unsigned short WCHAR16;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x, y; } POINT;
typedef struct {
    void *hwnd; u32 message, wParam, lParam, time; POINT pt;
} MSG;
typedef struct {
    void *hdc; int erase; RECT rcPaint; int restore, incUpdate;
    unsigned char reserved[32];
} PAINTSTRUCT;

NT_IMPORT(void *, BeginPaint, (void *, PAINTSTRUCT *))
NT_IMPORT(int, EndPaint, (void *, const PAINTSTRUCT *))
NT_IMPORT(u32, SetPixel, (void *, int, int, u32))
NT_IMPORT(void *, CreateSolidBrush, (u32))
NT_IMPORT(int, FillRect, (void *, const RECT *, void *))
NT_IMPORT(int, DeleteObject, (void *))
NT_IMPORT(int, MoveToEx, (void *, int, int, POINT *))
NT_IMPORT(int, LineTo, (void *, int, int))
NT_IMPORT(int, Rectangle, (void *, int, int, int, int))
NT_IMPORT(int, GetMessageW, (MSG *, void *, u32, u32))
NT_IMPORT(void *, CreateFileW, (const WCHAR16 *, u32, u32, void *, u32, u32, void *))
NT_IMPORT(int, ReadFile, (void *, void *, u32, u32 *, void *))
NT_IMPORT(int, WriteFile, (void *, const void *, u32, u32 *, void *))
NT_IMPORT(int, CloseHandle, (void *))

NT_DEFINE(BeginPaint)
NT_DEFINE(EndPaint)
NT_DEFINE(SetPixel)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(MoveToEx)
NT_DEFINE(LineTo)
NT_DEFINE(Rectangle)
NT_DEFINE(GetMessageW)
NT_DEFINE(CreateFileW)
NT_DEFINE(ReadFile)
NT_DEFINE(WriteFile)
NT_DEFINE(CloseHandle)

#define SCREEN_W 320
#define SCREEN_H 528
#define TOP 98
#define FILE_READ 0x80000000u
#define FILE_WRITE 0x40000000u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define WM_KEYDOWN 0x0100u
#define WM_LBUTTONDOWN 0x0201u
#define WM_QUIT 0x0012u
#define VK_LEFT 0x25u
#define VK_UP 0x26u
#define VK_RIGHT 0x27u
#define VK_DOWN 0x28u
#define VK_RETURN 0x0Du

static const WCHAR16 save_path[] = {
    '\\','f','l','s','0','\\','N','T','L','A','B','.','D','A','T',0
};
static const u32 colors[6] = {
    0x000000FFu, 0x0000AAFFu, 0x0000FF00u,
    0x0000FFFFu, 0x00FF00FFu, 0x00FFAA00u
};
typedef struct {
    u32 magic;
    u32 color_index;
    u32 brush;
    u32 stamp_count;
    u32 last_x;
    u32 last_y;
} SAVE_STATE;

static SAVE_STATE state = { 0x4E544C42u, 0, 8, 0, 160, 270 };
static int status = 0; /* 0 idle, 1 write OK, 2 read OK, 3 IO failed */

static void fill(void *dc, int l, int t, int r, int b, u32 color)
{
    RECT rc = { l, t, r, b };
    void *brush = NT_CALL(CreateSolidBrush, (color));
    if (brush) {
        NT_CALL(FillRect, (dc, &rc, brush));
        NT_CALL(DeleteObject, (brush));
    }
}

static void dot(void *dc, int x, int y, u32 color)
{
    if (x >= 0 && y >= TOP && x < SCREEN_W && y < SCREEN_H)
        NT_CALL(SetPixel, (dc, x, y, color));
}

static void ring(void *dc, int cx, int cy, int radius, u32 color)
{
    int r2 = radius * radius;
    int inner = (radius-3) * (radius-3);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int d = x*x + y*y;
            if (d <= r2 && d >= inner)
                dot(dc, cx+x, cy+y, color);
        }
}

static void draw_ui(void *dc)
{
    fill(dc, 0, 0, SCREEN_W, TOP, 0x00202024u);
    for (int i = 0; i < 6; ++i) {
        int x = 8 + i*52;
        fill(dc, x, 10, x+45, 59, colors[i]);
        if ((u32)i == state.color_index) {
            NT_CALL(MoveToEx, (dc, x, 65, 0));
            NT_CALL(LineTo, (dc, x+45, 65));
            NT_CALL(Rectangle, (dc, x-2, 8, x+47, 61));
        }
    }
    /* Brush-size ruler (one bar for each radius increment). */
    for (u32 i = 0; i < state.brush && i < 24; ++i)
        fill(dc, 10+(int)i*12, 76, 18+(int)i*12, 86, 0x0000E0FFu);
    u32 col = status == 1 ? 0x0000FF00u :
              status == 2 ? 0x00FFAA00u :
              status == 3 ? 0x000000FFu : 0x00606060u;
    fill(dc, 301, 76, 316, 91, col);
}

static int save_state(void)
{
    u32 count = 0;
    void *h = NT_CALL(CreateFileW, (save_path, FILE_WRITE, 0, 0,
                                   OPEN_ALWAYS, 0, 0));
    if (h == (void *)-1) return 0;
    int ok = NT_CALL(WriteFile, (h, &state, sizeof(state), &count, 0));
    int closed = NT_CALL(CloseHandle, (h));
    return ok && closed && count == sizeof(state);
}

static int load_state(void)
{
    SAVE_STATE next;
    u32 count = 0;
    void *h = NT_CALL(CreateFileW, (save_path, FILE_READ, 0, 0,
                                   OPEN_EXISTING, 0, 0));
    if (h == (void *)-1) return 0;
    int ok = NT_CALL(ReadFile, (h, &next, sizeof(next), &count, 0));
    int closed = NT_CALL(CloseHandle, (h));
    if (!ok || !closed || count != sizeof(next) ||
        next.magic != 0x4E544C42u || next.color_index >= 6 ||
        next.brush < 3 || next.brush > 24) return 0;
    state = next;
    return 1;
}

static void paint_screen(void)
{
    PAINTSTRUCT ps = {0};
    void *dc = NT_CALL(BeginPaint, (0, &ps));
    if (!dc) return;
    fill(dc, 0, 0, SCREEN_W, SCREEN_H, 0x00100C0Cu);
    /* Grid drawn using genuine WinCE GDI line operations. */
    for (int x = 0; x < SCREEN_W; x += 32) {
        NT_CALL(MoveToEx, (dc, x, TOP, 0));
        NT_CALL(LineTo, (dc, x, SCREEN_H-1));
    }
    for (int y = TOP; y < SCREEN_H; y += 32) {
        NT_CALL(MoveToEx, (dc, 0, y, 0));
        NT_CALL(LineTo, (dc, SCREEN_W-1, y));
    }
    draw_ui(dc);
    if (state.stamp_count)
        ring(dc, (int)state.last_x, (int)state.last_y,
             (int)state.brush, colors[state.color_index]);
    NT_CALL(EndPaint, (0, &ps));
}

static void stamp(int x, int y)
{
    if (x < 0 || x >= SCREEN_W || y < TOP || y >= SCREEN_H) return;
    state.last_x = (u32)x;
    state.last_y = (u32)y;
    ++state.stamp_count;
    PAINTSTRUCT ps = {0};
    void *dc = NT_CALL(BeginPaint, (0, &ps));
    if (!dc) return;
    ring(dc, x, y, (int)state.brush, colors[state.color_index]);
    NT_CALL(EndPaint, (0, &ps));
}

int main(void)
{
    paint_screen();
    for (;;) {
        MSG msg = {0};
        int rc = NT_CALL(GetMessageW, (&msg, 0, 0, 0));
        if (rc <= 0 || msg.message == WM_QUIT) break;
        if (msg.message == WM_LBUTTONDOWN) {
            int x = msg.pt.x, y = msg.pt.y;
            if (y < TOP && x >= 8 && x < 320 && y < 70) {
                u32 idx = (u32)((x-8)/52);
                if (idx < 6) {
                    state.color_index = idx;
                    paint_screen();
                }
            } else stamp(x, y);
        } else if (msg.message == WM_KEYDOWN) {
            u32 key = msg.wParam;
            if (key == VK_LEFT)
                state.color_index = (state.color_index+5u)%6u;
            else if (key == VK_RIGHT)
                state.color_index = (state.color_index+1u)%6u;
            else if (key == VK_UP && state.brush < 24u) ++state.brush;
            else if (key == VK_DOWN && state.brush > 3u) --state.brush;
            else if (key == '0') { state.stamp_count=0; status=0; }
            else if (key == VK_RETURN) status = save_state() ? 1 : 3;
            else if (key == '1') status = load_state() ? 2 : 3;
            else continue;
            paint_screen();
        }
    }
    return 0;
}
