/* CALC.EXE - integer-only pocket calculator using WinCE GDI and input.
 * No floating point operations, CRT or private NTOS imports. */
#include "wince_ui.h"
static const WCHAR16 cls[] = {'N', 'T', 'C', 'A', 'L', 'C', 0};
static const WCHAR16 title[] = {'N', 'T', 'O', 'S', ' ', 'C', 'a', 'l', 'c', 0};
static const char labels[16][3] = {"7", "8", "9", "/", "4", "5", "6", "*",
                                   "1", "2", "3", "-", "C", "0", "=", "+"};
static int current = 0, acc = 0, op = 0, new_number = 0, error = 0;
static int menu_open = 0, popup_mode = 0;
static unsigned int udiv32(unsigned int n, unsigned int d)
{
    unsigned int q = 0, rem = 0;
    if (!d)
        return 0;
    for (int i = 31; i >= 0; --i) {
        unsigned int hi = (rem >> 31);
        rem = (rem << 1) | ((n >> (unsigned int)i) & 1u);
        if (hi || rem >= d) {
            rem -= d;
            q |= 1u << (unsigned int)i;
        }
    }
    return q;
}
static void as_text(int n, char *buf)
{
    char rev[14];
    unsigned int v;
    int len = 0, i = 0;
    if (n < 0) {
        buf[i++] = '-';
        v = (unsigned int)(-(n + 1)) + 1u;
    } else
        v = (unsigned int)n;
    do {
        unsigned int q = udiv32(v, 10u);
        rev[len++] = (char)('0' + (v - q * 10u));
        v = q;
    } while (v && len < 12);
    while (len)
        buf[i++] = rev[--len];
    buf[i] = 0;
}
static void apply(void)
{
    int a = acc, b = current;
    unsigned int abs_a, abs_b, result;
    int sign = 0;
    if (!op) {
        acc = b;
        return;
    }
    if (op == '+') {
        if (a > 0 && b > 99999999 - a) {
            error = 1;
            return;
        }
        if (a < 0 && b < -99999999 - a) {
            error = 1;
            return;
        }
        acc = a + b;
    } else if (op == '-') {
        if (a < 0 && b > 99999999 + a) {
            error = 1;
            return;
        }
        if (a > 0 && b < a - 99999999) {
            error = 1;
            return;
        }
        acc = a - b;
    } else if (op == '*' || op == '/') {
        sign = (a < 0) ^ (b < 0);
        abs_a = (unsigned int)(a < 0 ? -a : a);
        abs_b = (unsigned int)(b < 0 ? -b : b);
        if (op == '/') {
            if (!abs_b) {
                error = 2;
                return;
            }
            result = udiv32(abs_a, abs_b);
        } else {
            if (abs_b && abs_a > udiv32(99999999u, abs_b)) {
                error = 1;
                return;
            }
            result = abs_a * abs_b;
        }
        acc = sign ? -(int)result : (int)result;
    }
    current = acc;
}
static void press(char key)
{
    if (key == 'C') {
        current = 0;
        acc = 0;
        op = 0;
        new_number = 0;
        error = 0;
        return;
    }
    if (error)
        return;
    if (key >= '0' && key <= '9') {
        if (new_number) {
            current = 0;
            new_number = 0;
        }
        if (current < 9999999)
            current = current * 10 + (key - '0');
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (op && !new_number)
            apply();
        else
            acc = current;
        op = key;
        new_number = 1;
    } else if (key == '=') {
        apply();
        op = 0;
        new_number = 1;
    }
}
static void draw(void *dc)
{
    char result[16];
    const DWORD bg = RGB(235, 238, 243), ink = RGB(30, 37, 53);
    ui_fill(dc, 0, 0, 320, 528, bg);
    ui_fill(dc, 0, 0, 320, 29, RGB(0, 0, 128));
    ui_text(dc, 8, 7, "Calculator", RGB(255, 255, 255), 0);
    ui_classic_button(dc, 284, 3, 317, 26, "X");
    /* WinCE classic command strip, modeled after the original Calc.exe.
     * The commands are functional; no resource-menu API is assumed. */
    ui_fill(dc, 0, 29, 320, 53, RGB(192, 192, 192));
    ui_text(dc, 9, 34, "File", RGB(0, 0, 0), 0);
    ui_text(dc, 71, 34, "Edit", RGB(0, 0, 0), 0);
    ui_text(dc, 134, 34, "View", RGB(0, 0, 0), 0);
    ui_classic_button(dc, 9, 58, 119, 85, popup_mode ? "Pop-Up" : "Standard");
    ui_classic_button(dc, 123, 58, 227, 85, popup_mode ? "Standard" : "Pop-Up");
    ui_fill(dc, 14, 98, 306, 141, RGB(255, 255, 255));
    ui_fill(dc, 14, 140, 306, 143, RGB(191, 197, 206));
    if (error)
        ui_text(dc, 28, 113, error == 2 ? "DIVIDE BY ZERO" : "OVERFLOW", RGB(190, 35, 49), 1);
    else {
        as_text(current, result);
        int n = 0;
        while (result[n])
            ++n;
        ui_text(dc, 296 - n * 16, 112, result, ink, 1);
    }
    for (int i = 0; i < 16; ++i) {
        int c = i % 4, r = i / 4;
        int l = 14 + c * 75, t = 159 + r * 73;
        DWORD color = i == 14   ? RGB(12, 116, 194)
                      : i == 12 ? RGB(215, 221, 229)
                      : c == 3  ? RGB(195, 211, 228)
                                : RGB(255, 255, 255);
        ui_fill(dc, l, t, l + 67, t + 65, RGB(160, 169, 181));
        ui_button(dc, l + 1, t + 1, l + 66, t + 63, labels[i], color,
                  i == 14 ? RGB(255, 255, 255) : ink);
    }
    ui_text(dc, 15, 473, "Touch digits or use keypad", RGB(63, 70, 81), 0);
    if (menu_open) {
        int left = menu_open == 1 ? 4 : menu_open == 2 ? 65 : 129;
        ui_fill(dc, left, 53, left + 129, 111, RGB(96, 96, 96));
        ui_fill(dc, left + 1, 54, left + 128, 110, RGB(235, 235, 235));
        ui_text(dc, left + 10, 62,
                menu_open == 1   ? "Exit"
                : menu_open == 2 ? "Clear"
                                 : "Standard",
                RGB(0, 0, 0), 0);
        ui_text(dc, left + 10, 87,
                menu_open == 1   ? "Return to Start"
                : menu_open == 2 ? "Reset calculator"
                                 : "Pop-Up",
                RGB(0, 0, 0), 0);
    }
}
static int wndproc(void *hwnd, DWORD msg, DWORD wp, DWORD lp)
{
    char key = 0;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            draw(dc);
            NT_CALL(EndPaint, (hwnd, &ps));
        }
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        int x = (short)(lp & 65535u), y = (short)(lp >> 16);
        if (menu_open) {
            int left = menu_open == 1 ? 4 : menu_open == 2 ? 65 : 129;
            int selected = menu_open;
            menu_open = 0;
            if (x >= left && x < left + 129 && y >= 53 && y < 111) {
                if (selected == 1) {
                    NT_CALL(PostQuitMessage, (0));
                    return 0;
                }
                if (selected == 2)
                    press('C');
                if (selected == 3)
                    popup_mode = y >= 84;
            }
            NT_CALL(InvalidateRect, (hwnd, 0, 1));
            return 0;
        }
        if (x >= 284 && y < 29) {
            NT_CALL(PostQuitMessage, (0));
            return 0;
        }
        if (y >= 29 && y < 53) {
            menu_open = x < 65 ? 1 : x < 129 ? 2 : x < 200 ? 3 : 0;
            NT_CALL(InvalidateRect, (hwnd, 0, 1));
            return 0;
        }
        if (y >= 58 && y < 85) {
            if (x >= 9 && x < 119)
                popup_mode = 0;
            if (x >= 123 && x < 227)
                popup_mode = 1;
            NT_CALL(InvalidateRect, (hwnd, 0, 1));
            return 0;
        }
        if (x >= 14 && x < 314 && y >= 159 && y < 451) {
            int col = (x - 14) / 75, row = (y - 159) / 73;
            if (col < 4 && row < 4)
                key = labels[row * 4 + col][0];
        }
    } else if (msg == WM_KEYDOWN) {
        if (wp >= 0x30u && wp <= 0x39u)
            key = (char)wp;
        else if (wp == VK_RETURN)
            key = '=';
        else if (wp == '+' || wp == '-' || wp == '*' || wp == '/')
            key = (char)wp;
        else if (wp == 0x1bu || wp == 0x80u) {
            NT_CALL(PostQuitMessage, (0));
            return 0;
        } else if (wp == 0x97u || wp == 'C')
            key = 'C';
    } else
        return 0;
    if (key) {
        press(key);
        NT_CALL(InvalidateRect, (hwnd, 0, 1));
    }
    return 0;
}
int main(void)
{
    ui_init();
    return ui_run(cls, title, wndproc);
}
