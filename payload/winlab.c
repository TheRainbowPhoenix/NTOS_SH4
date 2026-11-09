/* WINLAB.EXE: a WinCE GDI + keypad + stylus + filesystem demo.
 * Standard WinCE API names only; COREDLL bridges them to NTOS.
 *
 * Touch the canvas to stamp multicolor rings. Tap color swatches at top.
 * LEFT/RIGHT changes color; UP/DOWN changes brush size; 0 clears;
 * EXE saves state; 1 loads state; touch SAVE/LOAD buttons; CLEAR exits.
 * Save file: \fls0\NTLAB.DAT (short fixed-size state, not a bitmap).
 */
#include "ntimport.h"

typedef unsigned int u32;
typedef unsigned short WCHAR16;
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
NT_IMPORT(int, TextOutW, (void *, int, int, const WCHAR16 *, int))
NT_IMPORT(u32, GetFileSize, (void *, u32 *))
NT_IMPORT(u32, GetLastError, (void))
NT_IMPORT(int, GetClientRect, (void *, RECT *))

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
NT_DEFINE(TextOutW)
NT_DEFINE(GetFileSize)
NT_DEFINE(GetLastError)
NT_DEFINE(GetClientRect)

#define SCREEN_W 320
#define SCREEN_H 528
#define TOP 148
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

static const WCHAR16 label_save[] = {'S', 'A', 'V', 'E', 0};
static const WCHAR16 label_load[] = {'L', 'O', 'A', 'D', 0};
static const WCHAR16 label_ready[] = {'T', 'A', 'P', ' ', 'S', 'A', 'V',
                                      'E', '/', 'L', 'O', 'A', 'D', 0};
static const WCHAR16 label_saved[] = {'S', 'A', 'V', 'E', 'D', ' ', 'N', 'T',
                                      'L', 'A', 'B', '.', 'D', 'A', 'T', 0};
static const WCHAR16 label_loaded[] = {'L', 'O', 'A', 'D', 'E', 'D', ' ', 'N', 'T',
                                       'L', 'A', 'B', '.', 'D', 'A', 'T', 0};
static const WCHAR16 label_failed[] = {'F', 'I', 'L', 'E', ' ', 'I', '/', 'O',
                                       ' ', 'F', 'A', 'I', 'L', 'E', 'D', 0};

static const WCHAR16 save_path[] = {'\\', 'f', 'l', 's', '0', '\\', 'N', 'T',
                                    'L',  'A', 'B', '.', 'D', 'A',  'T', 0};
static const u32 colors[6] = {0x000000FFu, 0x0000AAFFu, 0x0000FF00u,
                              0x0000FFFFu, 0x00FF00FFu, 0x00FFAA00u};
typedef struct {
    u32 magic;
    u32 color_index;
    u32 brush;
    u32 stamp_count;
    u32 last_x;
    u32 last_y;
} SAVE_STATE;

static SAVE_STATE state = {0x4E544C42u, 0, 8, 0, 160, 270};
static int status = 0; /* 0 idle, 1 write OK, 2 read OK, 3 IO failed */
static u32 last_error = 0;

static void fill(void *dc, int l, int t, int r, int b, u32 color)
{
    RECT rc = {l, t, r, b};
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
    int inner = (radius - 3) * (radius - 3);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int d = x * x + y * y;
            if (d <= r2 && d >= inner)
                dot(dc, cx + x, cy + y, color);
        }
}

static void error_caption(void *dc)
{
    WCHAR16 text[16];
    static const char digits[] = "0123456789ABCDEF";
    text[0] = 'E';
    text[1] = 'R';
    text[2] = 'R';
    text[3] = ' ';
    text[4] = '0';
    text[5] = 'x';
    for (int i = 0; i < 8; ++i)
        text[6 + i] = (WCHAR16)digits[(last_error >> (28 - 4 * i)) & 15u];
    text[14] = 0;
    NT_CALL(TextOutW, (dc, 8, 136, text, 14));
}

static void caption(void *dc, int x, int y, const WCHAR16 *str, int len)
{
    NT_CALL(TextOutW, (dc, x, y, str, len));
}

static void draw_ui(void *dc)
{
    fill(dc, 0, 0, SCREEN_W, TOP, 0x00202024u);
    for (int i = 0; i < 6; ++i) {
        int x = 8 + i * 52;
        fill(dc, x, 10, x + 45, 59, colors[i]);
        if ((u32)i == state.color_index) {
            NT_CALL(MoveToEx, (dc, x, 65, 0));
            NT_CALL(LineTo, (dc, x + 45, 65));
            NT_CALL(Rectangle, (dc, x - 2, 8, x + 47, 61));
        }
    }
    for (u32 i = 0; i < state.brush && i < 24; ++i)
        fill(dc, 10 + (int)i * 12, 76, 18 + (int)i * 12, 86, 0x0000E0FFu);

    /* Real, tappable SAVE/LOAD controls. The status line explicitly reports
     * filesystem success or failure rather than using a tiny status pixel. */
    fill(dc, 8, 100, 154, 133, status == 1 ? 0x00004000u : 0x00604422u);
    fill(dc, 166, 100, 312, 133, status == 2 ? 0x00004000u : 0x00604422u);
    NT_CALL(Rectangle, (dc, 8, 100, 154, 133));
    NT_CALL(Rectangle, (dc, 166, 100, 312, 133));
    caption(dc, 57, 109, label_save, 4);
    caption(dc, 215, 109, label_load, 4);
    if (status == 1)
        caption(dc, 8, 136, label_saved, 15);
    else if (status == 2)
        caption(dc, 8, 136, label_loaded, 16);
    else if (status == 3)
        error_caption(dc);
    else
        caption(dc, 8, 136, label_ready, 13);
}

static int save_state(void)
{
    u32 count = 0;
    void *h = NT_CALL(CreateFileW, (save_path, FILE_WRITE, 0, 0, OPEN_ALWAYS, 0, 0));
    if (h == (void *)-1) {
        last_error = NT_CALL(GetLastError, ());
        return 0;
    }
    int ok = NT_CALL(WriteFile, (h, &state, sizeof(state), &count, 0));
    if (!ok || count != sizeof(state))
        last_error = ok ? 87u : NT_CALL(GetLastError, ());
    int closed = NT_CALL(CloseHandle, (h));
    if (!closed)
        last_error = NT_CALL(GetLastError, ());
    return ok && closed && count == sizeof(state);
}

static int load_state(void)
{
    SAVE_STATE next;
    u32 count = 0;
    void *h = NT_CALL(CreateFileW, (save_path, FILE_READ, 0, 0, OPEN_EXISTING, 0, 0));
    if (h == (void *)-1) {
        last_error = NT_CALL(GetLastError, ());
        return 0;
    }
    u32 file_size = NT_CALL(GetFileSize, (h, 0));
    if (file_size != sizeof(next)) {
        last_error = file_size == 0xFFFFFFFFu ? NT_CALL(GetLastError, ()) : 87u;
        NT_CALL(CloseHandle, (h));
        return 0;
    }
    int ok = NT_CALL(ReadFile, (h, &next, sizeof(next), &count, 0));
    if (!ok || count != sizeof(next))
        last_error = ok ? 87u : NT_CALL(GetLastError, ());
    int closed = NT_CALL(CloseHandle, (h));
    if (!closed)
        last_error = NT_CALL(GetLastError, ());
    if (!ok || !closed || count != sizeof(next) || next.magic != 0x4E544C42u ||
        next.color_index >= 6 || next.brush < 3 || next.brush > 24) {
        last_error = 87; /* malformed/unexpected state file */
        return 0;
    }
    /* A whole-struct assignment makes GCC emit ___movmem_i4_even on SH4.
     * Payloads link freestanding (-nostdlib), so copy fields explicitly. */
    state.magic = next.magic;
    state.color_index = next.color_index;
    state.brush = next.brush;
    state.stamp_count = next.stamp_count;
    state.last_x = next.last_x;
    state.last_y = next.last_y;
    return 1;
}

static void paint_screen(void)
{
    PAINTSTRUCT ps = {0};
    void *dc = NT_CALL(BeginPaint, (0, &ps));
    if (!dc)
        return;
    fill(dc, 0, 0, SCREEN_W, SCREEN_H, 0x00100C0Cu);
    /* Grid drawn using genuine WinCE GDI line operations. */
    for (int x = 0; x < SCREEN_W; x += 32) {
        NT_CALL(MoveToEx, (dc, x, TOP, 0));
        NT_CALL(LineTo, (dc, x, SCREEN_H - 1));
    }
    for (int y = TOP; y < SCREEN_H; y += 32) {
        NT_CALL(MoveToEx, (dc, 0, y, 0));
        NT_CALL(LineTo, (dc, SCREEN_W - 1, y));
    }
    draw_ui(dc);
    if (state.stamp_count)
        ring(dc, (int)state.last_x, (int)state.last_y, (int)state.brush, colors[state.color_index]);
    NT_CALL(EndPaint, (0, &ps));
}

static void stamp(int x, int y)
{
    if (x < 0 || x >= SCREEN_W || y < TOP || y >= SCREEN_H)
        return;
    state.last_x = (u32)x;
    state.last_y = (u32)y;
    ++state.stamp_count;
    PAINTSTRUCT ps = {0};
    void *dc = NT_CALL(BeginPaint, (0, &ps));
    if (!dc)
        return;
    ring(dc, x, y, (int)state.brush, colors[state.color_index]);
    NT_CALL(EndPaint, (0, &ps));
}

int main(void)
{
    paint_screen();
    for (;;) {
        MSG msg = {0};
        int rc = NT_CALL(GetMessageW, (&msg, 0, 0, 0));
        if (rc <= 0 || msg.message == WM_QUIT)
            break;
        if (msg.message == WM_LBUTTONDOWN) {
            int x = msg.pt.x, y = msg.pt.y;
            if (x >= 8 && x < 154 && y >= 100 && y < 133) {
                status = save_state() ? 1 : 3;
                paint_screen();
            } else if (x >= 166 && x < 312 && y >= 100 && y < 133) {
                status = load_state() ? 2 : 3;
                paint_screen();
            } else if (y < 70 && x >= 8 && x < 320) {
                u32 idx = (u32)((x - 8) / 52);
                if (idx < 6) {
                    state.color_index = idx;
                    paint_screen();
                }
            } else
                stamp(x, y);
        } else if (msg.message == WM_KEYDOWN) {
            u32 key = msg.wParam;
            if (key == VK_LEFT)
                state.color_index = (state.color_index + 5u) % 6u;
            else if (key == VK_RIGHT)
                state.color_index = (state.color_index + 1u) % 6u;
            else if (key == VK_UP && state.brush < 24u)
                ++state.brush;
            else if (key == VK_DOWN && state.brush > 3u)
                --state.brush;
            else if (key == '0') {
                state.stamp_count = 0;
                status = 0;
            } else if (key == VK_RETURN)
                status = save_state() ? 1 : 3;
            else if (key == '1')
                status = load_state() ? 2 : 3;
            else
                continue;
            paint_screen();
        }
    }
    return 0;
}
