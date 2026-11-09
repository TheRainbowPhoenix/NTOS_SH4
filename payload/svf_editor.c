/* NTOS native WinCE API SVF editor.
 * Lightweight 32x32 integer-only authoring canvas; .SVF is saved under
 * \fls0\ICON.SVF via real COREDLL file APIs, not ad-hoc host services.
 * VEC2 uses the same opcodes as SVF.DLL (including 0x31 thick line,
 * and 0x22 32-step circular arc). No floating point, CRT or heap. */
#include "wince_ui.h"
NT_IMPORT(int, svf_SVFDrawTint, (void *, const void *, DWORD, int, int, int, int, DWORD))
NT_IMPORT(void *, CreateFileW, (const WCHAR16 *, DWORD, DWORD, void *, DWORD, DWORD, void *))
NT_IMPORT(int, WriteFile, (void *, const void *, DWORD, DWORD *, void *))
NT_IMPORT(int, ReadFile, (void *, void *, DWORD, DWORD *, void *))
NT_IMPORT(int, CloseHandle, (void *))
NT_DEFINE(CreateFileW)
NT_DEFINE(WriteFile)
NT_DEFINE(ReadFile)
NT_DEFINE(CloseHandle)
/* Keep all COREDLL IAT entries contiguous; external SVF follows last. */
NT_IMPORT(int, svf_SVFValidate, (const void *, DWORD))
NT_DEFINE(svf_SVFDrawTint)
NT_DEFINE(svf_SVFValidate)

static const WCHAR16 clsname[] = {'S', 'V', 'F', 'E', 'D', 'I', 'T', 'O', 'R', 0};
static const WCHAR16 title[] = {'S', 'V', 'F', ' ', 'E', 'd', 'i', 't', 'o', 'r', 0};
static const WCHAR16 file_name[] = {'\\', 'f', 'l', 's', '0', '\\', 'I', 'C',
                                    'O',  'N', '.', 'S', 'V', 'F',  0};
static unsigned char bytes[1024];
static unsigned used, shapes, shape_undo[51];
static int tool = 0, tone, down_x, down_y, touching, pressed = -1, saved = -1;
static void put8(unsigned char c)
{
    if (used < sizeof(bytes))
        bytes[used++] = c;
}
static void put16(int v)
{
    put8((unsigned char)((unsigned)v >> 8));
    put8((unsigned char)v);
}
static void pos(int x, int y, int *ox, int *oy)
{
    x = x < 19 ? 19 : x > 299 ? 299 : x;
    y = y < 116 ? 116 : y > 395 ? 395 : y;
    *ox = (x - 19) * 32 / 281;
    *oy = (y - 116) * 32 / 280;
    if (*ox > 31)
        *ox = 31;
    if (*oy > 31)
        *oy = 31;
}
static void reset_doc(void)
{
    used = 0;
    put8('V');
    put8('E');
    put8('C');
    put8('2');
    put8(0x60);
    put16(32);
    put16(32);
    shapes = 0;
    saved = -1;
}
static void append_shape(int ax, int ay, int bx, int by)
{
    if (used > sizeof(bytes) - 32 || shapes >= 50)
        return;
    /* Explicit two-tone model: 0x7fff primary / 0x4210 half-black.
     * Fills are transparent except rectangles, so shapes stay editable. */
    shape_undo[shapes] = used;
    put8(1);
    put16(tool == 0 ? (tone ? 0x4210 : 0x7fff) : 0xffff);
    put16(tone ? 0x4210 : 0x7fff);
    if (tool == 0) {
        put8(0x11);
        put16(ax < bx ? ax : bx);
        put16(ay < by ? ay : by);
        put16(ax > bx ? ax : bx);
        put16(ay > by ? ay : by);
        put8(2);
    } else if (tool == 1) {
        put8(0x31);
        put16(ax);
        put16(ay);
        put16(bx);
        put16(by);
        put8(2);
    } else if (tool == 2) {
        int dx = bx - ax, dy = by - ay;
        int radius = dx < 0 ? -dx : dx, tmp = dy < 0 ? -dy : dy;
        if (tmp > radius)
            radius = tmp;
        if (!radius)
            radius = 1;
        put8(0x20);
        put16(ax);
        put16(ay);
        put16(radius);
    } else {
        int dx = bx - ax, dy = by - ay;
        int radius = dx < 0 ? -dx : dx, tmp = dy < 0 ? -dy : dy;
        if (tmp > radius)
            radius = tmp;
        if (!radius)
            radius = 1;
        put8(0x22);
        put16(ax);
        put16(ay);
        put16(radius);
        put8(20);
        put8(24);
        put8(2);
    }
    ++shapes;
    saved = -1;
}
static void save_doc(void)
{
    void *handle = NT_CALL(CreateFileW, (file_name, 0x40000000u, 0, 0, 2, 0, 0));
    if (!handle || handle == (void *)-1) {
        saved = 0;
        return;
    }
    unsigned char final[1025];
    for (unsigned i = 0; i < used; ++i)
        final[i] = bytes[i];
    final[used] = 0;
    DWORD written = 0;
    saved = NT_CALL(WriteFile, (handle, final, used + 1, &written, 0)) && written == used + 1;
    NT_CALL(CloseHandle, (handle));
}
static void load_doc(void)
{
    void *handle = NT_CALL(CreateFileW, (file_name, 0x80000000u, 0, 0, 3, 0, 0));
    if (!handle || handle == (void *)-1) {
        saved = 0;
        return;
    }
    unsigned char loaded[1024];
    DWORD count = 0;
    int ok = NT_CALL(ReadFile, (handle, loaded, sizeof(loaded), &count, 0));
    NT_CALL(CloseHandle, (handle));
    if (!ok || count < 10 || count >= sizeof(bytes) || !NT_CALL(svf_SVFValidate, (loaded, count)) ||
        loaded[count - 1] != 0) {
        saved = 0;
        return;
    }
    for (DWORD i = 0; i + 1 < count; ++i)
        bytes[i] = loaded[i];
    used = count - 1;
    shapes = 0; /* Loaded layers are preserved; Undo affects new strokes only. */
    saved = 2;
}
static void undo_shape(void)
{
    if (shapes) {
        used = shape_undo[--shapes];
        saved = -1;
    }
}
static void frame(void *dc)
{
    static const char *tools[] = {"Rect", "Line", "Circle", "Arc"};
    const DWORD ink = RGB(22, 27, 34), weak = RGB(120, 127, 136);
    ui_fill(dc, 0, 24, 320, 488, RGB(245, 249, 252));
    ui_fill(dc, 0, 24, 320, 55, RGB(223, 233, 243));
    ui_text(dc, 11, 31, "File    Undo      SVF Editor", ink, 0);
    for (int i = 0; i < 4; ++i) {
        int x = 8 + 77 * i;
        ui_fill(dc, x + (pressed == i), 64 + (pressed == i), x + 73 - (pressed == i),
                96 - (pressed == i), tool == i ? RGB(165, 195, 221) : RGB(210, 223, 235));
        ui_text(dc, x + 10, 73, tools[i], ink, 0);
    }
    ui_fill(dc, 16, 111, 304, 403, RGB(164, 179, 193));
    ui_fill(dc, 19, 114, 301, 400, RGB(255, 255, 255));
    for (int x = 0; x <= 32; x += 8)
        ui_fill(dc, 19 + x * 281 / 32, 114, 20 + x * 281 / 32, 400, RGB(232, 236, 239));
    for (int y = 0; y <= 32; y += 8)
        ui_fill(dc, 19, 114 + y * 284 / 32, 301, 115 + y * 284 / 32, RGB(232, 236, 239));
    unsigned char document[1025];
    for (unsigned i = 0; i < used; ++i)
        document[i] = bytes[i];
    document[used] = 0;
    NT_CALL(svf_SVFDrawTint, (dc, document, used + 1, 23, 118, 274, 278, ink));
    ui_fill(dc, 8, 411, 100, 443, tone ? RGB(193, 201, 213) : RGB(157, 173, 191));
    ui_text(dc, 13, 418, tone ? "50% ink" : "100% ink", ink, 0);
    ui_fill(dc, 109, 411, 205, 443, RGB(208, 222, 237));
    ui_text(dc, 115, 418, "Clear", ink, 0);
    ui_fill(dc, 214, 411, 309, 443, pressed == 6 ? RGB(158, 182, 205) : RGB(195, 215, 232));
    ui_text(dc, 229, 418, "Save", ink, 0);
    ui_fill(dc, 109, 449, 205, 478, RGB(210, 223, 235));
    ui_text(dc, 123, 455, "Load", ink, 0);
    ui_text(dc, 12, 456,
            saved < 0    ? "Draw / save"
            : saved == 1 ? "Saved"
            : saved == 2 ? "Loaded"
                         : "File error",
            saved == 0 ? RGB(175, 52, 42) : weak, 0);
}
static int procedure(void *hwnd, DWORD msg, DWORD wp, DWORD lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc)
            frame(dc);
        NT_CALL(EndPaint, (hwnd, &ps));
        return 0;
    }
    if (msg == WM_KEYDOWN && (wp == 0x1bu || wp == 0x80u)) {
        NT_CALL(PostQuitMessage, (0));
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == 0x0202u) {
        int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
        if (msg == WM_LBUTTONDOWN) {
            pressed = -1;
            if (y >= 24 && y < 55 && x >= 62 && x < 120)
                pressed = 8;
            else if (y >= 449 && y < 478 && x >= 109 && x < 205)
                pressed = 7;
            else if (y >= 64 && y < 96)
                pressed = (x - 8) / 77;
            else if (y >= 411 && y < 443)
                pressed = x < 105 ? 4 : x < 211 ? 5 : 6;
            if (y >= 114 && y < 400) {
                down_x = x;
                down_y = y;
                touching = 1;
            }
        } else {
            if (touching && y >= 114 && y < 400) {
                int ax, ay, bx, by;
                pos(down_x, down_y, &ax, &ay);
                pos(x, y, &bx, &by);
                append_shape(ax, ay, bx, by);
            }
            touching = 0;
            if (pressed >= 0 && pressed <= 3)
                tool = pressed;
            else if (pressed == 4)
                tone = !tone;
            else if (pressed == 5)
                reset_doc();
            else if (pressed == 6)
                save_doc();
            else if (pressed == 7)
                load_doc();
            else if (pressed == 8)
                undo_shape();
            pressed = -1;
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 1));
        return 0;
    }
    return 0;
}
int main(void)
{
    WNDCLASSW wc = {0};
    MSG msg = {0};
    ui_init();
    reset_doc();
    wc.wndproc = procedure;
    wc.class_name = clsname;
    if (!NT_CALL(RegisterClassW, (&wc)))
        return 1;
    void *hwnd = NT_CALL(CreateWindowExW, (0, clsname, title, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    if (!hwnd)
        return 2;
    NT_CALL(ShowWindow, (hwnd, 1));
    NT_CALL(UpdateWindow, (hwnd));
    while (NT_CALL(GetMessageW, (&msg, hwnd, 0, 0)) > 0) {
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    NT_CALL(DestroyWindow, (hwnd));
    return 0;
}
