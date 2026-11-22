/* CMD.EXE: native WinCE/GWES-friendly command shell for the 320x528 CP400.
 * One real HWND, a bounded text console and an optional PEG-like second pane.
 * Execution is deliberately serial: NTOS tears down this process before the
 * requested EXE starts. There is no false concurrent-window claim here. */
#include "wince_ui.h"
#include "nt_cmd_model.h"
#include "nt_tiling.h"

typedef struct {
    DWORD attrs, times[6], size_hi, size_lo, oid;
    WCHAR16 name[260];
} NT_FIND_DATA;

NT_IMPORT(int, coredll_launch, (const char *))
NT_IMPORT(void *, FindFirstFileW, (const WCHAR16 *, NT_FIND_DATA *))
NT_IMPORT(int, FindNextFileW, (void *, NT_FIND_DATA *))
NT_IMPORT(int, FindClose, (void *))
NT_DEFINE(coredll_launch)
NT_DEFINE(FindFirstFileW)
NT_DEFINE(FindNextFileW)
NT_DEFINE(FindClose)

static const WCHAR16 shell_class[] = {'N', 'T', 'O', 'S', 'C', 'm', 'd', 0};
static const WCHAR16 shell_title[] = {'N', 'T', 'O', 'S', ' ', 'C', 'o',
                                      'm', 'm', 'a', 'n', 'd', 0};
static const WCHAR16 files_pattern[] = {'\\', 'f', 'l', 's', '0', '\\', '*', '.', 'E', 'X', 'E', 0};
static NT_TILE_STATE layout;
static char entry[80];
static unsigned entry_len;
static char history[16][48];
static unsigned history_count;
static unsigned keyboard_open, keyboard_numeric;
static void *shell_window;

#define FACE RGB(248, 249, 251)
#define WHITE RGB(255, 255, 255)
#define INK RGB(30, 42, 58)
#define DIM RGB(85, 103, 121)
#define BLUE RGB(38, 100, 174)
#define BAR RGB(223, 233, 245)
#define EDGE RGB(172, 186, 204)

static void line_history(const char *s)
{
    if (history_count == 16) {
        for (unsigned n = 0; n < 15; ++n)
            for (unsigned j = 0; j < 48; ++j)
                history[n][j] = history[n + 1][j];
    } else {
        ++history_count;
    }
    unsigned i = history_count - 1;
    for (unsigned j = 0; j < 47; ++j) {
        history[i][j] = s[j];
        if (!s[j])
            return;
    }
    history[i][47] = 0;
}
static void redraw(void)
{
    if (shell_window)
        NT_CALL(InvalidateRect, (shell_window, 0, 0));
}
static void add_input(int ch)
{
    if (ch == 8) {
        if (entry_len)
            entry[--entry_len] = 0;
    } else if (ch >= 32 && ch <= 126 && entry_len + 1 < sizeof(entry)) {
        entry[entry_len++] = (char)ch;
        entry[entry_len] = 0;
    }
    redraw();
}
static void show_wide_name(const WCHAR16 *name)
{
    char tmp[48];
    unsigned i = 0;
    while (name[i] && i < 47) {
        unsigned c = name[i];
        tmp[i] = c < 127 && c >= 32 ? (char)c : '?';
        ++i;
    }
    tmp[i] = 0;
    line_history(tmp);
}
static void command_dir(void)
{
    NT_FIND_DATA found;
    void *find = NT_CALL(FindFirstFileW, (files_pattern, &found));
    if (find == (void *)-1 || !find) {
        line_history("No EXE files or flash unavailable");
        return;
    }
    unsigned count = 0;
    do {
        if (count < 12)
            show_wide_name(found.name);
        ++count;
    } while (count < 256 && NT_CALL(FindNextFileW, (find, &found)));
    NT_CALL(FindClose, (find));
    if (count > 12)
        line_history("More entries: use File Explorer");
}
static void command_execute(const char *line)
{
    NT_CMD_ACTION a;
    nt_cmd_parse(line, &a);
    switch (a.kind) {
        case NT_CMD_EMPTY:
            break;
        case NT_CMD_HELP:
            line_history("HELP CLS DIR RUN EXIT");
            line_history("SPLIT FULL SWAP ROTATE KB");
            line_history("RUN app.exe or Apps\\name\\app.exe");
            break;
        case NT_CMD_CLS:
            history_count = 0;
            break;
        case NT_CMD_DIR:
            command_dir();
            break;
        case NT_CMD_SPLIT:
            nt_tile_split(&layout);
            break;
        case NT_CMD_FULL:
            nt_tile_maximize(&layout, 0);
            break;
        case NT_CMD_SWAP:
            nt_tile_swap(&layout);
            if (!layout.split)
                layout.split = 1;
            break;
        case NT_CMD_ROTATE:
            nt_tile_rotate(&layout);
            break;
        case NT_CMD_KB:
            keyboard_open = !keyboard_open;
            break;
        case NT_CMD_RUN:
            line_history("Launching EXE (serial handoff)");
            /* API returns 1 only if the file can be opened by the kernel.
             * The current process must exit to let the loader start it. */
            if (NT_CALL(coredll_launch, (a.path)))
                NT_CALL(PostQuitMessage, (0));
            else
                line_history("Cannot open EXE");
            break;
        case NT_CMD_EXIT:
            NT_CALL(PostQuitMessage, (0));
            break;
        case NT_CMD_BAD_PATH:
            line_history("Unsafe or missing EXE path");
            break;
        default:
            line_history("Unknown command; try HELP");
            break;
    }
}
static void enter_input(void)
{
    char prompt[48];
    unsigned n = 0, i = 0;
    prompt[n++] = '>';
    prompt[n++] = ' ';
    while (entry[i] && n < sizeof(prompt) - 1)
        prompt[n++] = entry[i++];
    prompt[n] = 0;
    line_history(prompt);
    command_execute(entry);
    entry[0] = 0;
    entry_len = 0;
    redraw();
}
static void text_clipped(void *dc, int x, int y, const char *s, int width, DWORD ink)
{
    char out[48];
    int max_chars = (width - 8) / 10;
    int i = 0;
    if (max_chars < 0)
        return;
    if (max_chars > 47)
        max_chars = 47;
    while (i < max_chars && s[i]) {
        out[i] = s[i];
        ++i;
    }
    out[i] = 0;
    ui_text(dc, x, y, out, ink, 0);
}
static void pane_paint(void *dc, unsigned pane)
{
    NT_TILE_RECT r, c;
    if (!nt_tile_rect(&layout, pane, &r) || !nt_tile_client(&layout, pane, &c))
        return;
    ui_fill(dc, r.left, r.top, r.right, r.bottom, WHITE);
    ui_fill(dc, r.left, r.top, r.right, r.top + 22, BAR);
    ui_fill(dc, r.left, r.bottom - 1, r.right, r.bottom, EDGE);
    ui_fill(dc, r.right - 1, r.top, r.right, r.bottom, EDGE);
    text_clipped(dc, r.left + 6, r.top + 3, pane ? "Tools / second pane" : "Command Prompt",
                 r.right - r.left - 27, INK);
    ui_fill(dc, r.right - 22, r.top + 2, r.right - 2, r.top + 20, BLUE);
    ui_text(dc, r.right - 19, r.top + 3, layout.split ? "^" : "v", WHITE, 0);
    if (pane == 0) {
        int baseline = c.bottom - 25;
        ui_fill(dc, c.left + 3, baseline - 3, c.right - 3, baseline + 19, FACE);
        text_clipped(dc, c.left + 7, baseline, "> ", c.right - c.left - 10, BLUE);
        text_clipped(dc, c.left + 23, baseline, entry, c.right - c.left - 30, INK);
        int available = (baseline - c.top - 6) / 18;
        if (available > 15)
            available = 15;
        if (available < 0)
            available = 0;
        unsigned shown = history_count < (unsigned)available ? history_count : (unsigned)available;
        for (unsigned n = 0; n < shown; ++n) {
            unsigned idx = history_count - shown + n;
            text_clipped(dc, c.left + 6, c.top + 3 + (int)n * 18, history[idx],
                         c.right - c.left - 12, DIM);
        }
    } else {
        const char *items[] = {"HELP", "DIR", "SPLIT", "SWAP", "ROTATE", "KB"};
        for (int i = 0; i < 6; ++i) {
            int y = c.top + 4 + i * 28;
            if (y + 26 > c.bottom)
                break;
            ui_fill(dc, c.left + 5, y, c.right - 5, y + 24, FACE);
            text_clipped(dc, c.left + 12, y + 5, items[i], c.right - c.left - 20, BLUE);
        }
    }
}
static void key_button(void *dc, int l, int t, int r, int b, const char *s)
{
    ui_fill(dc, l, t, r, b, WHITE);
    ui_fill(dc, l, b - 1, r, b, EDGE);
    ui_fill(dc, r - 1, t, r, b, EDGE);
    text_clipped(dc, l + 5, t + 9, s, r - l - 7, INK);
}
/* The native profile does not yet render the phone FingerKB DLL as a
 * COREDLL SIP. This small fallback makes the real command line usable. */
static void keyboard_paint(void *dc)
{
    const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm.\\/"};
    ui_fill(dc, 0, 328, 320, 528, BAR);
    for (int row = 0; row < 3; ++row) {
        int x0 = row == 1 ? 18 : 3;
        int count = row == 1 ? 9 : 10;
        int y = 331 + row * 45;
        for (int col = 0; col < count; ++col) {
            char label[2] = {keyboard_numeric && row == 0 ? "1234567890"[col] : rows[row][col], 0};
            int x = x0 + col * 31;
            key_button(dc, x, y, x + 29, y + 41, label);
        }
    }
    key_button(dc, 3, 468, 62, 522, keyboard_numeric ? "ABC" : "123");
    key_button(dc, 66, 468, 158, 522, "SPACE");
    key_button(dc, 162, 468, 238, 522, "DEL");
    key_button(dc, 242, 468, 317, 522, "ENTER");
}
static int keyboard_touch(int x, int y)
{
    if (y < 328 || x < 0 || x >= 320)
        return 0;
    if (y >= 468) {
        if (x < 64)
            keyboard_numeric = !keyboard_numeric;
        else if (x < 160)
            add_input(' ');
        else if (x < 240)
            add_input(8);
        else
            enter_input();
        return 1;
    }
    int row = (y - 331) / 45;
    if (row < 0 || row >= 3 || y >= 331 + (row + 1) * 45 - 4)
        return 1;
    int x0 = row == 1 ? 18 : 3;
    int col = (x - x0) / 31;
    int count = row == 1 ? 9 : 10;
    if (x < x0 || col < 0 || col >= count || (x - x0) % 31 >= 29)
        return 1;
    const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm.\\/"};
    add_input(keyboard_numeric && row == 0 ? "1234567890"[col] : rows[row][col]);
    return 1;
}
static void paint(void *dc)
{
    nt_tile_work_bottom(&layout, keyboard_open ? 328 : 528);
    ui_fill(dc, 0, 24, 320, 528, FACE);
    pane_paint(dc, 0);
    pane_paint(dc, 1);
    if (keyboard_open)
        keyboard_paint(dc);
}
static void choose_tool(int index)
{
    const char *cmd[] = {"help", "dir", "split", "swap", "rotate", "kb"};
    if (index >= 0 && index < 6)
        command_execute(cmd[index]);
    redraw();
}
static void touch(int x, int y)
{
    if (keyboard_open && keyboard_touch(x, y)) {
        redraw();
        return;
    }
    int pane = nt_tile_hit(&layout, x, y);
    if (pane < 0)
        return;
    NT_TILE_RECT r, c;
    if (!nt_tile_rect(&layout, (unsigned)pane, &r) || !nt_tile_client(&layout, (unsigned)pane, &c))
        return;
    if (y < r.top + 22 && x >= r.right - 26) {
        if (layout.split)
            nt_tile_maximize(&layout, (unsigned)pane);
        else
            nt_tile_split(&layout);
    } else if (pane == 0 && y >= c.bottom - 28) {
        keyboard_open = 1;
    } else if (pane == 1 && y >= c.top + 4) {
        choose_tool((y - c.top - 4) / 28);
    }
    redraw();
}
static int shell_proc(void *hwnd, DWORD msg, DWORD wp, DWORD lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            paint(dc);
            NT_CALL(EndPaint, (hwnd, &ps));
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN)
            enter_input();
        else if (wp == 8 || wp == 0x2eu)
            add_input(8);
        else if (wp == 0x71u)
            keyboard_open = !keyboard_open;
        else if (wp == 27u && keyboard_open)
            keyboard_open = 0;
        else if (wp >= 32u && wp <= 126u)
            add_input((int)wp);
        redraw();
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        touch((short)(lp & 0xffffu), (short)((lp >> 16) & 0xffffu));
        return 0;
    }
    return 0;
}
int main(void)
{
    ui_init();
    nt_tile_init(&layout, 320, 528, 24);
    history_count = 0;
    line_history("NTOS/SH4 Command Processor");
    line_history("Type HELP, RUN name.exe or DIR");
    line_history("Tap > to type / F2 keyboard");
    WNDCLASSW wc = {0};
    MSG msg = {0};
    wc.wndproc = shell_proc;
    wc.class_name = shell_class;
    if (!NT_CALL(RegisterClassW, (&wc)))
        return 1;
    shell_window =
        NT_CALL(CreateWindowExW, (0, shell_class, shell_title, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    if (!shell_window)
        return 2;
    NT_CALL(ShowWindow, (shell_window, 1));
    NT_CALL(UpdateWindow, (shell_window));
    for (;;) {
        int rc = NT_CALL(GetMessageW, (&msg, shell_window, 0, 0));
        if (rc <= 0)
            break;
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    NT_CALL(DestroyWindow, (shell_window));
    shell_window = 0;
    return 0;
}
