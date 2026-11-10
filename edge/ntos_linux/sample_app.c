/* sample_app.c — first NTOS_SH4_linux payload: validates the whole chain.
 *
 * Written exactly like a device payload (payload/phone style): WinCE-ish
 * window + message loop, drawing through __imp_* IAT slots that the host
 * loader patches. On device the same source compiles with the SH4 toolchain
 * (elf2ntx + COREDLL.DLL imports); here it is built by zig cc into an
 * x86-64 PE DLL. Draws a fake "Edge mobile" chrome: status bar, address
 * bar, three navigation buttons, page area, and reacts to taps + F-keys.
 */
#include <stdint.h>

typedef uint32_t U32;
typedef uint16_t W16;

typedef struct {
    int left, top, right, bottom;
} Rect;
typedef struct {
    int x, y;
} Pt;

#define RGB_(r, g, b) ((U32)((r) | ((g) << 8) | ((b) << 16)))
#define WHITE RGB_(255, 255, 255)
#define EDGE_BLUE RGB_(0x00, 0x78, 0xD7)
#define INK RGB_(29, 42, 58)
#define SURFACE RGB_(242, 247, 251)

#define WM_PAINT_ 15u
#define WM_KEY_ 0x100u
#define WM_CHAR_ 0x102u
#define WM_DOWN_ 0x201u
#define WM_UP_ 0x202u

/* ---- NTOS IAT import machinery (same shape as payload/ntimport.h) ---- */
#ifdef NTOS_LINUX_BUILD
/* Host build: the loader discovers slots via PE exports, so dllexport them. */
#define NT_SLOT_EXPORT __attribute__((dllexport))
#else
#define NT_SLOT_EXPORT
#endif
#define NT_IMPORT(ret, name, params) \
    typedef ret(*__ntx_fn_t_##name) params; \
    extern NT_SLOT_EXPORT const void *volatile __imp_##name; \
    static __inline __ntx_fn_t_##name __ntx_ref_##name(void) \
    { return (__ntx_fn_t_##name)(uintptr_t)__imp_##name; }
#define NT_DEFINE(name) \
    NT_SLOT_EXPORT const void *volatile __imp_##name __attribute__((section(".idata"))) = (const void *)0;
#define NT_CALL(name, args) __ntx_ref_##name() args

/* ---- imports (resolved by the host loader) ---- */
NT_IMPORT(U32, RegisterClassW, (const void *))
NT_DEFINE(RegisterClassW)
NT_IMPORT(void *, CreateWindowExW, (U32, const W16 *, const W16 *, U32, int, int, int, int, void *, void *, void *, void *))
NT_DEFINE(CreateWindowExW)
NT_IMPORT(int, ShowWindow, (void *, int))
NT_DEFINE(ShowWindow)
NT_IMPORT(int, UpdateWindow, (void *))
NT_DEFINE(UpdateWindow)
NT_IMPORT(int, InvalidateRect, (void *, const Rect *, int))
NT_DEFINE(InvalidateRect)
NT_IMPORT(int, GetMessageW, (void *, void *, U32, U32))
NT_DEFINE(GetMessageW)
NT_IMPORT(int, TranslateMessage, (const void *))
NT_DEFINE(TranslateMessage)
NT_IMPORT(int, DispatchMessageW, (const void *))
NT_DEFINE(DispatchMessageW)
NT_IMPORT(void *, BeginPaint, (void *, void *))
NT_DEFINE(BeginPaint)
NT_IMPORT(int, EndPaint, (void *, const void *))
NT_DEFINE(EndPaint)
NT_IMPORT(void *, CreateSolidBrush, (U32))
NT_DEFINE(CreateSolidBrush)
NT_IMPORT(int, FillRect, (void *, const Rect *, void *))
NT_DEFINE(FillRect)
NT_IMPORT(int, DeleteObject, (void *))
NT_DEFINE(DeleteObject)
NT_IMPORT(void, PostQuitMessage, (int))
NT_DEFINE(PostQuitMessage)
NT_IMPORT(void, coredll_puts, (const char *))
NT_DEFINE(coredll_puts)

/* ---- app state ---- */
typedef int (*WndProc)(void *, U32, U32, U32);

typedef struct {
    U32 style;
    WndProc proc;
    int cls_extra, win_extra;
    void *instance, *icon, *cursor, *background;
    const W16 *menu, *name;
} WndClass;

typedef struct {
    void *hwnd;
    U32 message, wparam, lparam;
    U32 time;
    Pt pt;
    void *next;
} Msg;

typedef struct {
    void *hdc;
    int erase;
    Rect area;
    int restore, inc;
    unsigned char reserved[32];
} Paint;

static const W16 cls_name[] = {'E', 'd', 'g', 'e', 'S', 'a', 'm', 'p', 'l', 'e', 0};
static void *g_wnd;
static int g_nav_selected = 1; /* 0 back 1 address 2 forward */
static int g_tap_count;

static void fill(void *dc, int x, int y, int w, int h, U32 color)
{
    Rect r = {x, y, x + w, y + h};
    void *b = NT_CALL(CreateSolidBrush, (color));
    NT_CALL(FillRect, (dc, &r, b));
    NT_CALL(DeleteObject, (b));
}

static void draw_chrome(void *dc)
{
    /* status bar */
    fill(dc, 0, 0, 320, 24, EDGE_BLUE);
    fill(dc, 0, 24, 320, 44, SURFACE);
    /* address bar pill */
    fill(dc, 12, 44, 296, 36, WHITE);
    fill(dc, 14, 46, 292, 32, SURFACE);
    /* nav row */
    int base = 528 - 48;
    fill(dc, 0, base, 320, 48, SURFACE);
    for (int i = 0; i < 3; ++i) {
        int x = 8 + i * 104;
        U32 c = (i == g_nav_selected) ? EDGE_BLUE : INK;
        fill(dc, x, base + 8, 96, 32, c);
    }
    /* page area: selected "site" */
    fill(dc, 0, 84, 320, base - 84, WHITE);
    fill(dc, 20, 100, 280, 16, EDGE_BLUE);
    fill(dc, 20, 124, 240, 10, INK);
    fill(dc, 20, 140, 260, 10, INK);
    fill(dc, 20, 156, 200, 10, INK);
    static char report[64];
    report[0] = 0;
    /* taps shown as small squares */
    for (int i = 0; i < g_tap_count && i < 40; ++i)
        fill(dc, 150 + (i % 10) * 8, 200 + (i / 10) * 8, 6, 6, EDGE_BLUE);
}

static int wndproc(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    if (msg == WM_PAINT_) {
        Paint ps;
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc)
            draw_chrome(dc);
        NT_CALL(EndPaint, (hwnd, &ps));
        return 0;
    }
    if (msg == WM_KEY_ && wp == 0x1b) {
        NT_CALL(PostQuitMessage, (0));
        return 0;
    }
    if (msg == WM_KEY_ && (wp == 0x70 || wp == 0x71 || wp == 0x72)) {
        g_nav_selected = (int)(wp - 0x70);
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        (void)x;
        if (y >= 528 - 48)
            g_nav_selected = x / 104;
        ++g_tap_count;
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

__attribute__((dllexport)) int RunApplication(void)
{
    NT_CALL(coredll_puts, ("sample_app: RunApplication"));
    WndClass wc;
    wc.style = 0;
    wc.proc = wndproc;
    wc.cls_extra = wc.win_extra = 0;
    wc.instance = wc.icon = wc.cursor = wc.background = 0;
    wc.menu = 0;
    wc.name = cls_name;
    NT_CALL(RegisterClassW, (&wc));
    g_wnd = NT_CALL(CreateWindowExW, (0, cls_name, cls_name, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    NT_CALL(ShowWindow, (g_wnd, 1));
    NT_CALL(UpdateWindow, (g_wnd));
    Msg msg;
    int rc;
    while ((rc = NT_CALL(GetMessageW, (&msg, g_wnd, 0, 0))) > 0) {
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    return 0;
}

int DllMainCRTStartup_impl(void *h, U32 r, void *r2);
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
