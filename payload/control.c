/* Native NTOS Control Panel: enumerate real Windows/*.cpl files.
 * DEMO.CPL has a linked CPlApplet PE import. Dynamic third-party CPL
 * loading is deliberately not claimed until LoadLibrary/GetProcAddress exist.
 */
#include "wince_ui.h"

typedef struct {
    DWORD attributes;
    DWORD times[6];
    DWORD size_hi;
    DWORD size_lo;
    DWORD oid;
    WCHAR16 name[260];
} NT_FIND_DATA;
_Static_assert(sizeof(NT_FIND_DATA) == 560, "native FindFirstFileW layout");
NT_IMPORT(void *, FindFirstFileW, (const WCHAR16 *, NT_FIND_DATA *))
NT_IMPORT(int, FindNextFileW, (void *, NT_FIND_DATA *))
NT_IMPORT(int, FindClose, (void *))
NT_IMPORT(DWORD, CPlApplet, (void *, DWORD, DWORD, void *))
NT_DEFINE(FindFirstFileW)
NT_DEFINE(FindNextFileW)
NT_DEFINE(FindClose)
NT_DEFINE(CPlApplet)

static const WCHAR16 class_name[] = {'N', 'T', 'C', 'O', 'N', 'T', 'R', 'O', 'L', 0};
static const WCHAR16 window_title[] = {'C', 'o', 'n', 't', 'r', 'o', 'l',
                                       ' ', 'P', 'a', 'n', 'e', 'l', 0};
static const WCHAR16 cpl_pattern[] = u"\\fls0\\Windows\\*.cpl";
#define CPL_MAX 24
#define CPL_NAME_MAX 40
/* The original CE NEWCPLINFOW wire payload is 468 bytes. */
typedef struct {
    DWORD size, flags, help, data, icon;
    WCHAR16 name[32], info[64], helpfile[128];
} CPL_INFO;
_Static_assert(sizeof(CPL_INFO) == 468, "NEWCPLINFOW layout");
static char entries[CPL_MAX][CPL_NAME_MAX];
static char captions[CPL_MAX][CPL_NAME_MAX];
static int entry_count, selected = -1, menu_open, loading, scanned, scan_failed;
static const DWORD theme_bg = RGB(244, 247, 251);
static const DWORD theme_ink = RGB(31, 43, 62);
static const DWORD theme_blue = RGB(57, 126, 185);

static void copy_name(char *out, const WCHAR16 *input)
{
    unsigned i = 0;
    for (; i + 1 < CPL_NAME_MAX && input[i]; ++i) {
        unsigned ch = input[i];
        out[i] = ch < 127u ? (char)ch : '?';
    }
    out[i] = 0;
}
static int is_demo(const char *name)
{
    static const char demo[] = "demo.cpl";
    unsigned i = 0;
    for (; demo[i] && name[i]; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != demo[i])
            return 0;
    }
    return demo[i] == 0 && name[i] == 0;
}
static void rescan(void)
{
    NT_FIND_DATA file = {0};
    void *handle;
    entry_count = 0;
    selected = -1;
    scan_failed = 0;
    handle = NT_CALL(FindFirstFileW, (cpl_pattern, &file));
    if (handle == (void *)-1 || !handle) {
        scan_failed = 1;
        scanned = 1;
        return;
    }
    do {
        if (entry_count == CPL_MAX)
            break;
        if (!(file.attributes & 0x10u)) {
            int index = entry_count++;
            copy_name(entries[index], file.name);
            copy_name(captions[index], file.name);
            if (is_demo(entries[index])) {
                CPL_INFO info = {0};
                /* Query the installed DLL's real applet metadata over PE IAT. */
                if (NT_CALL(CPlApplet, (0, 1, 0, 0)) && NT_CALL(CPlApplet, (0, 2, 0, 0))) {
                    NT_CALL(CPlApplet, (0, 8, 0, &info)); /* CPL_NEWINQUIRE */
                    if (info.name[0])
                        copy_name(captions[index], info.name);
                }
                NT_CALL(CPlApplet, (0, 7, 0, 0));
            }
        }
    } while (NT_CALL(FindNextFileW, (handle, &file)));
    NT_CALL(FindClose, (handle));
    scanned = 1;
}
static void refresh_with_popup(void *hwnd)
{
    loading = 1;
    menu_open = 0;
    NT_CALL(InvalidateRect, (hwnd, 0, 1));
    NT_CALL(UpdateWindow, (hwnd)); /* visible before potentially slow storage calls */
    rescan();
    loading = 0;
    NT_CALL(InvalidateRect, (hwnd, 0, 1));
    NT_CALL(UpdateWindow, (hwnd));
}
static void open_selected(void *hwnd)
{
    if (selected < 0 || selected >= entry_count)
        return;
    if (is_demo(entries[selected])) {
        /* The linked PE import is genuinely resolved from DEMO.CPL.
         * No unsafe on-the-fly DLL loader is required. */
        NT_CALL(CPlApplet, (hwnd, 1, 0, 0));     /* CPL_INIT */
        if (NT_CALL(CPlApplet, (hwnd, 2, 0, 0))) /* CPL_GETCOUNT */
            NT_CALL(CPlApplet, (hwnd, 5, 0, 0)); /* CPL_DBLCLK */
        NT_CALL(CPlApplet, (hwnd, 7, 0, 0));     /* CPL_EXIT */
    } else {
        /* Enumerated but not executable until typed LoadLibrary is implemented. */
        scan_failed = 2;
    }
}
static void draw(void *dc)
{
    ui_fill(dc, 0, 0, 320, 528, theme_bg);
    ui_fill(dc, 0, 24, 320, 51, RGB(225, 235, 245));
    ui_text(dc, 10, 30, "File", theme_ink, 0);
    ui_text(dc, 66, 30, "View", theme_ink, 0);
    ui_text(dc, 252, 31, "Refresh", theme_blue, 0);
    for (int i = 0; i < entry_count; ++i) {
        int col = i % 3;
        int row = i / 3;
        int x = 4 + col * 105;
        int y = 61 + row * 64;
        if (y >= 480)
            break;
        ui_fill(dc, x + 26, y, x + 73, y + 33,
                i == selected ? RGB(166, 203, 239) : RGB(210, 226, 241));
        ui_text(dc, x + 44, y + 8, "C", theme_blue, 0);
        ui_text(dc, x + 6, y + 36, captions[i], theme_ink, 0);
    }
    if (!scanned)
        ui_text(dc, 16, 120, "Loading Control Panel...", theme_ink, 0);
    else if (!entry_count)
        ui_text(dc, 12, 110, "No Windows/*.cpl modules found", theme_ink, 0);
    if (scan_failed == 2)
        ui_text(dc, 8, 473, "This CPL needs a dynamic loader", theme_ink, 0);
    else if (scan_failed == 1)
        ui_text(dc, 8, 473, "Windows folder unavailable", theme_ink, 0);
    if (menu_open) {
        int left = menu_open == 1 ? 4 : 60;
        ui_fill(dc, left, 51, left + 126, 99, RGB(221, 234, 246));
        ui_text(dc, left + 8, 59, menu_open == 1 ? "Close" : "Refresh", theme_ink, 0);
        ui_text(dc, left + 8, 79, menu_open == 1 ? "Return to Start" : "Rescan CPLs", theme_ink, 0);
    }
    if (loading) {
        ui_fill(dc, 29, 189, 291, 279, RGB(185, 199, 214));
        ui_fill(dc, 31, 191, 289, 277, RGB(248, 251, 254));
        ui_fill(dc, 31, 191, 289, 215, RGB(205, 227, 243));
        ui_text(dc, 45, 196, "Control Panel", theme_ink, 0);
        ui_text(dc, 47, 228, "Scanning Windows/*.cpl", theme_ink, 0);
        ui_text(dc, 47, 251, "Please wait...", theme_blue, 0);
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
        if (wp == 0x1bu || wp == 0x80u)
            NT_CALL(PostQuitMessage, (0));
        else if (wp == VK_RETURN)
            open_selected(hwnd);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        int x = (short)(lp & 0xffffu);
        int y = (short)(lp >> 16);
        if (menu_open) {
            int choice = menu_open;
            menu_open = 0;
            if (y >= 51 && y < 99) {
                if (choice == 1)
                    NT_CALL(PostQuitMessage, (0));
                else
                    refresh_with_popup(hwnd);
            }
        } else if (y >= 24 && y < 51) {
            if (x >= 235)
                refresh_with_popup(hwnd);
            else
                menu_open = x < 60 ? 1 : x < 122 ? 2 : 0;
        } else if (y >= 61 && y < 465) {
            int col = x / 105;
            int row = (y - 61) / 64;
            int idx = row * 3 + col;
            if (col < 3 && idx >= 0 && idx < entry_count) {
                if (selected == idx)
                    open_selected(hwnd);
                else
                    selected = idx;
            }
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 1));
    }
    return 0;
}
int main(void)
{
    WNDCLASSW cls = {0};
    MSG msg = {0};
    void *hwnd;
    ui_init();
    cls.wndproc = window_proc;
    cls.class_name = class_name;
    if (!NT_CALL(RegisterClassW, (&cls)))
        return 1;
    hwnd = NT_CALL(CreateWindowExW, (0, class_name, window_title, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    if (!hwnd)
        return 2;
    NT_CALL(ShowWindow, (hwnd, 1));
    refresh_with_popup(hwnd);
    while (NT_CALL(GetMessageW, (&msg, hwnd, 0, 0)) > 0) {
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    NT_CALL(DestroyWindow, (hwnd));
    return 0;
}
