/* Exercise actual COREDLL common-control HWNDs, routed messages and SIP. */
#include <assert.h>
#include <stdint.h>
#define GetLastError nt_test_GetLastError
#define SetLastError nt_test_SetLastError
#define CloseHandle nt_test_CloseHandle
#define LocalFree nt_test_LocalFree
#define NTOS_CORE_HOST_TEST
#define main native_controls_dllmain
#include "../payload/coredll.c"
#undef main

const void *ntos_host_table[28];
static unsigned pixels, fills, refreshes, commands, painted;
static u32 last_id;
static void fake_refresh(void)
{
    ++refreshes;
}
static void fake_pixel(u32 x, u32 y, u32 color)
{
    assert(x < 320 && y < 528 && color <= 0xffffu);
    ++pixels;
}
static void fake_fill(u32 l, u32 t, u32 r, u32 b, u32 color)
{
    assert(l <= r && r <= 320 && t <= b && b <= 528 && color <= 0xffffu);
    ++fills;
}
static int wndproc(void *hwnd, u32 msg, u32 wp, u32 lp)
{
    (void)lp;
    if (msg == 0x0111u) {
        ++commands;
        last_id = wp & 0xffffu;
    }
    if (msg == 0x000Fu) {
        ++painted;
        NT_PAINT ps = {0};
        void *dc = BeginPaint(hwnd, &ps);
        NT_RECT bg = {0, 24, 320, 528};
        void *b = CreateSolidBrush(0x00ffffffu);
        assert(dc && FillRect(dc, &bg, b));
        assert(EndPaint(hwnd, &ps));
    }
    return 0;
}
static NT_MSG mouse(u32 type, void *hwnd, int x, int y)
{
    NT_MSG msg = {0};
    msg.hwnd = hwnd;
    msg.message = type;
    msg.pt.x = x;
    msg.pt.y = y;
    msg.lParam = (u32)x | ((u32)y << 16);
    return msg;
}
int main(void)
{
    ntos_host_table[2] = (const void *)fake_refresh;
    ntos_host_table[7] = (const void *)fake_pixel;
    ntos_host_table[9] = (const void *)fake_fill;
    static const u16 klass[] = {'T', 'e', 's', 't', 0};
    static const u16 button[] = {'B', 'U', 'T', 'T', 'O', 'N', 0};
    static const u16 label[] = {'S', 'T', 'A', 'T', 'I', 'C', 0};
    static const u16 progress[] = {'m', 's', 'c', 't', 'l', 's', '_', 'p', 'r',
                                   'o', 'g', 'r', 'e', 's', 's', '3', '2', 0};
    static const u16 list[] = {'L', 'I', 'S', 'T', 'B', 'O', 'X', 0};
    static const u16 combo[] = {'C', 'O', 'M', 'B', 'O', 'B', 'O', 'X', 0};
    static const u16 txt[] = {'T', 'a', 'p', 0};
    static const u16 item[] = {'O', 'n', 'e', 0};
    NT_WNDCLASS wc = {0};
    wc.class_name = klass;
    wc.wndproc = wndproc;
    assert(RegisterClassW(&wc));
    void *parent = CreateWindowExW(0, klass, txt, 0, 0, 0, 320, 528, 0, 0, 0, 0);
    assert(parent);
    void *btn = CreateWindowExW(0, button, txt, 0, 10, 40, 100, 36, parent, (void *)101, 0, 0);
    void *check = CreateWindowExW(0, button, txt, 3, 10, 80, 130, 36, parent, (void *)102, 0, 0);
    void *static_label =
        CreateWindowExW(0, label, txt, 0, 10, 120, 140, 24, parent, (void *)103, 0, 0);
    void *bar = CreateWindowExW(0, progress, 0, 0, 10, 150, 200, 20, parent, (void *)104, 0, 0);
    void *lb = CreateWindowExW(0, list, 0, 0, 10, 181, 200, 90, parent, (void *)105, 0, 0);
    void *cb = CreateWindowExW(0, combo, 0, 0, 10, 280, 200, 35, parent, (void *)106, 0, 0);
    assert(btn && check && static_label && bar && lb && cb);
    assert(btn != check && check != bar && lb != cb);
    assert(!CreateWindowExW(0, button, txt, 0, 10, 10, 20, 20, 0, 0, 0, 0));
    assert(!ShowWindow(btn, 1));
    ShowWindow(check, 1);
    ShowWindow(static_label, 1);
    ShowWindow(bar, 1);
    ShowWindow(lb, 1);
    ShowWindow(cb, 1);
    ShowWindow(parent, 1);
    NT_RECT rc = {0};
    assert(GetClientRect(btn, &rc) && rc.right == 100 && rc.bottom == 36);
    assert(SendMessageW(btn, 0x000Eu, 0, 0) == 3);
    static u16 name[16] = {0};
    assert(SendMessageW(btn, 0x000Du, 16, (u32)(uintptr_t)name) == 3);
    assert(name[0] == 'T' && name[2] == 'p' && name[3] == 0);
    assert(SetWindowTextW(btn, item));
    assert(GetWindowTextW(btn, name, 16) == 3 && name[0] == 'O');
    assert(SendMessageW(check, 0x00F0u, 0, 0) == 0);
    assert(SendMessageW(check, 0x00F1u, 1, 0) == 0);
    assert(SendMessageW(check, 0x00F0u, 0, 0) == 1);
    assert(SendMessageW(bar, 0x0401u, 0, (80u << 16u) | 20u) == 1);
    assert(SendMessageW(bar, 0x0402u, 75, 0) == 20);
    assert(SendMessageW(bar, 0x0402u, 150, 0) == 75);
    assert(nt_ctl_find(bar)->value == 80u);
    assert(SendMessageW(lb, 0x0180u, 0, (u32)(uintptr_t)item) == 0);
    assert(SendMessageW(lb, 0x0180u, 0, (u32)(uintptr_t)txt) == 1);
    assert(SendMessageW(lb, 0x018Bu, 0, 0) == 2);
    assert(SendMessageW(lb, 0x0186u, 1, 0) == 1);
    assert(SendMessageW(lb, 0x0188u, 0, 0) == 1);
    assert(SendMessageW(cb, 0x0143u, 0, (u32)(uintptr_t)txt) == 0);
    assert(SendMessageW(cb, 0x0146u, 0, 0) == 1);
    NT_MSG event = mouse(0x0201u, parent, 14, 52);
    assert(DispatchMessageW(&event));
    event.message = 0x0202u;
    assert(DispatchMessageW(&event));
    assert(commands == 1 && last_id == 101u);
    event = mouse(0x0201u, parent, 14, 90);
    assert(DispatchMessageW(&event));
    event.message = 0x0202u;
    assert(DispatchMessageW(&event));
    assert(commands == 2 && last_id == 102u);
    assert(SendMessageW(check, 0x00F0u, 0, 0) == 0);
    event = mouse(0x0201u, parent, 14, 191);
    assert(DispatchMessageW(&event));
    assert(commands == 3 && last_id == 105u && nt_ctl_find(lb)->selected == 0u);
    assert(UpdateWindow(parent) && painted > 0);
    assert(fills > 10 && pixels > 10 && refreshes > 0);
    assert(SipShowIM(1u));
    unsigned old_refresh = refreshes;
    assert(UpdateWindow(parent));
    assert(refreshes >= old_refresh);
    assert(SipShowIM(0u));
    assert(DestroyWindow(btn));
    assert(!nt_ctl_find(btn) && !DestroyWindow(btn));
    assert(DestroyWindow(parent));
    assert(!nt_ctl_find(check) && !nt_ctl_find(lb) && !nt_ctl_find(cb));
    return 0;
}
