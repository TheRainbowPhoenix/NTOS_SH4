/* CONTROL.EXE - native WinCE-style Control Panel for the flat NTOS build.
 * This is a C reconstruction of the shell, not the converted Microsoft CE PE.
 * All Win32-style imports resolve through the same native COREDLL.DLL.
 */
#include "wince_ui.h"

static const WCHAR16 class_name[] = {'N', 'T', 'C', 'O', 'N', 'T', 'R', 'O', 'L', 0};
static const WCHAR16 window_title[] = {'C', 'o', 'n', 't', 'r', 'o', 'l',
                                       ' ', 'P', 'a', 'n', 'e', 'l', 0};
static const char *const items[] = {
    "Bluetooth", "Certificates", "Date/Time", "Dialing", "Display",  "Input Panel",
    "Keyboard",  "Mouse",        "Network",   "Owner",   "Password", "PC Connection",
    "Power",     "Regional",     "Stylus",    "System",  "Volume"};
static const char *const symbols[] = {"B", "C", "D",  "T", "D", "I", "K",   "M", "N",
                                      "O", "P", "PC", "P", "R", "S", "SYS", "V"};
static int selected = -1;
static int menu_open;

static void draw(void *dc)
{
    const DWORD face = RGB(234, 234, 234);
    ui_fill(dc, 0, 0, 320, 528, face);
    /* The shared COREDLL paints the title bar and its close button. */
    ui_fill(dc, 0, 24, 320, 49, RGB(192, 192, 192));
    ui_text(dc, 10, 30, "File", RGB(0, 0, 0), 0);
    ui_text(dc, 66, 30, "View", RGB(0, 0, 0), 0);
    for (int i = 0; i < 17; ++i) {
        int col = i % 4;
        int row = i / 4;
        int x = col * 80 + 3;
        int y = 60 + row * 84;
        ui_fill(dc, x + 21, y, x + 57, y + 34,
                i == selected ? RGB(110, 155, 224) : RGB(196, 207, 225));
        ui_text(dc, x + 26, y + 7, symbols[i], RGB(32, 50, 87), 0);
        ui_text(dc, x + 3, y + 39, items[i], RGB(15, 15, 15), 0);
    }
    if (selected >= 0) {
        ui_fill(dc, 6, 486, 314, 504, RGB(219, 225, 234));
        ui_text(dc, 12, 488, items[selected], RGB(0, 0, 0), 0);
    }
    if (menu_open) {
        int x = menu_open == 1 ? 5 : 60;
        ui_fill(dc, x, 49, x + 126, 96, RGB(110, 110, 110));
        ui_fill(dc, x + 1, 50, x + 125, 95, RGB(247, 247, 247));
        ui_text(dc, x + 9, 55, menu_open == 1 ? "Close" : "Large Icons", RGB(0, 0, 0), 0);
        ui_text(dc, x + 9, 77, menu_open == 1 ? "Back to Start" : "Refresh", RGB(0, 0, 0), 0);
    }
}

static int window_proc(void *hwnd, DWORD message, DWORD wp, DWORD lp)
{
    if (message == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            draw(dc);
            NT_CALL(EndPaint, (hwnd, &ps));
        }
        return 0;
    }
    if (message == WM_KEYDOWN) {
        if (wp == 0x1bu || wp == 0x80u) {
            NT_CALL(PostQuitMessage, (0));
            return 0;
        }
    }
    if (message == WM_LBUTTONDOWN) {
        int x = (short)(lp & 0xffffu);
        int y = (short)(lp >> 16);
        if (menu_open) {
            int prev = menu_open;
            menu_open = 0;
            if (prev == 1 && y >= 49 && y < 96) {
                NT_CALL(PostQuitMessage, (0));
                return 0;
            }
        } else if (y >= 24 && y < 49) {
            menu_open = x < 60 ? 1 : x < 120 ? 2 : 0;
        } else if (y >= 60 && y < 480 && x >= 0 && x < 320) {
            int col = x / 80;
            int row = (y - 60) / 84;
            int idx = row * 4 + col;
            if (idx >= 0 && idx < 17)
                selected = idx;
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 1));
    }
    return 0;
}

int main(void)
{
    ui_init();
    return ui_run(class_name, window_title, window_proc);
}
