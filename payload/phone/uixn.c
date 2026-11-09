#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
IMP(int, render_SpRenderThreadInit, (void))
IMP(void, render_SpRenderThreadUninit, (void))
IMP(const Keyboard *, kb_UIXGetSchemaFactory, (void))
IMP(void, kb_UIXSchemaUnloaded, (void))
typedef struct {
    void *hwnd;
    U32 message, wParam, lParam, time;
    Pt pt;
} Msg;
typedef struct {
    void *hdc;
    int erase;
    Rect area;
    int restore, inc;
    unsigned char reserved[32];
} Paint;
typedef int (*WndProc)(void *, U32, U32, U32);
typedef struct {
    U32 style;
    WndProc proc;
    int cls_extra, win_extra;
    void *instance, *icon, *cursor, *background;
    const W16 *menu, *name;
} WndClass;
IMP(U32, RegisterClassW, (const WndClass *))
IMP(void *, CreateWindowExW,
    (U32, const W16 *, const W16 *, U32, int, int, int, int, void *, void *, void *, void *))
IMP(int, ShowWindow, (void *, int))
IMP(int, UpdateWindow, (void *))
IMP(int, InvalidateRect, (void *, const Rect *, int))
IMP(int, GetMessageW, (Msg *, void *, U32, U32))
IMP(int, TranslateMessage, (const Msg *))
IMP(int, DispatchMessageW, (const Msg *))
IMP(int, DestroyWindow, (void *))
IMP(void *, BeginPaint, (void *, Paint *))
IMP(int, EndPaint, (void *, const Paint *))
IMP(int, coredll_launch, (const char *))
IMP(void, PostQuitMessage, (int))
IMP(int, SipShowIM, (U32))
IMP(int, SipGetInfo, (SipInfo *))
static const W16 cls[] = {'S', 't', 'a', 'r', 't', 'H', 'o', 's', 't', 0};
static App app;
static void *window;
static void (*error_handler)(int);
static const Keyboard *keyboard;
static int visible(void)
{
    SipInfo s;
    s.size = sizeof(s);
    return CALL(SipGetInfo, (&s)) && (s.flags & 1u);
}
static int proc(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    int height = app.chrome->navheight();
    app.menu->bounds(528 - height);
    keyboard->dock(height);
    if (msg == WM_KEY_ && wp == 0x73) {
        app.chrome->navigation(!height);
        CALL(InvalidateRect, (hwnd, 0, 0));
        CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_PAINT_) {
        Paint p;
        void *dc = CALL(BeginPaint, (hwnd, &p));
        if (dc) {
            const Render *r = CALL(render_SpGetThreadContext, ());
            /* The expanded notifications overlay covers the entire display.
             * Drawing the Start/menu and SIP first wastes thousands of
             * rectangle calls and multiple screen-sized fills. */
            if (!app.chrome->expanded()) {
                r->surface(528 - height);
                app.menu->paint(dc);
                if (visible())
                    keyboard->paint(dc);
            }
            r->surface(528);
            app.chrome->paint(dc);
        }
        CALL(EndPaint, (hwnd, &p));
        return 0;
    }
    if (msg == WM_KEY_ && wp == 0x71) {
        int was_visible = visible();
        if (app.chrome->expanded())
            app.chrome->input(WM_KEY_, 0x1b, 0);
        if (!was_visible)
            app.menu->input(msg, wp, lp);
        CALL(SipShowIM, (was_visible ? 0u : 1u));
        CALL(InvalidateRect, (hwnd, 0, 0));
        CALL(UpdateWindow, (hwnd));
        return 0;
    }
    int changed = app.chrome->input(msg, wp, lp);
    if (changed == 4) {
        if (visible())
            CALL(SipShowIM, (0));
        else
            app.menu->input(WM_KEY_, 0x1b, 0);
        changed = 1;
    } else if (changed == 5) {
        if (CALL(coredll_launch, ("\\fls0\\STARTHOST.EXE")))
            CALL(PostQuitMessage, (0));
        return 0;
    } else if (changed == 6) {
        app.menu->open(0x10000u);
        changed = 1;
    }
    if (changed == 3) {
        CALL(PostQuitMessage, (0));
        return 0;
    }
    if (changed == 2) {
        app.menu->open(APP_SETTINGS_ID);
        changed = 1;
    }
    if (!changed && visible() && msg == WM_DOWN_ && (int)(short)(lp >> 16) >= 271 - height) {
        int key = keyboard->input(msg, wp, lp);
        if (key)
            app.menu->input(key == 8 || key == 13 ? WM_KEY_ : WM_CHAR_, (U32)key, 0);
        changed = 1;
    }
    if (!changed && app.menu)
        changed = app.menu->input(msg, wp, lp);
    if (changed) {
        CALL(InvalidateRect, (hwnd, 0, 0));
        CALL(UpdateWindow, (hwnd));
    }
    return 0;
}
int UIXInitializeApplication(const App *config)
{
    if (!config || config->profile != PROFILE || !config->menu || !config->chrome)
        return (int)0x80070057u;
    if (window)
        return (int)0x8000ffffu;
    app.profile = config->profile;
    app.title = config->title;
    app.menu = config->menu;
    app.chrome = config->chrome;
    keyboard = CALL(kb_UIXGetSchemaFactory, ());
    if (!keyboard || keyboard->profile != PROFILE)
        return NOTIMPL;
    return CALL(render_SpRenderThreadInit, ());
}
int UIXGetServices(const Render **out)
{
    if (!out)
        return (int)0x80070057u;
    *out = CALL(render_SpGetThreadContext, ());
    return 0;
}
int UIXLoadMarkup(const W16 *uri, void **out)
{
    /* This compact build ships compiled C records, not a UIB parser.
     * Reject unknown resources rather than claim to support arbitrary markup. */
    static const W16 root[] = {'r', 'e', 's', ':', '/', '/', 'S', 't', 'a', 'r', 't',
                               'M', 'e', 'n', 'u', '!', 'M', 'a', 'i', 'n', '.', 'u',
                               'i', 'x', '#', 'F', 'r', 'a', 'm', 'e', 0};
    if (!out)
        return (int)0x80070057u;
    *out = 0;
    if (!uri)
        return NOTIMPL;
    unsigned i = 0;
    while (root[i] && root[i] == uri[i])
        ++i;
    if (root[i] || uri[i])
        return NOTIMPL;
    *out = (void *)app.menu;
    return 0;
}
int UIXRunApplication(void)
{
    if (app.profile != PROFILE)
        return (int)0x8000ffffu;
    static WndClass wc;
    Msg msg;
    wc.proc = proc;
    wc.name = cls;
    if (!CALL(RegisterClassW, (&wc)))
        return -1;
    window = CALL(CreateWindowExW, (0, cls, app.title, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    if (!window)
        return -1;
    CALL(ShowWindow, (window, 1));
    CALL(UpdateWindow, (window));
    int rc;
    while ((rc = CALL(GetMessageW, (&msg, window, 0, 0))) > 0) {
        CALL(TranslateMessage, (&msg));
        CALL(DispatchMessageW, (&msg));
    }
    CALL(DestroyWindow, (window));
    window = 0;
    return rc < 0 ? -1 : 0;
}
void UIXShutdownApplication(void)
{
    if (window) {
        CALL(DestroyWindow, (window));
        window = 0;
    }
    CALL(SipShowIM, (0));
    CALL(kb_UIXSchemaUnloaded, ());
    CALL(render_SpRenderThreadUninit, ());
    app.profile = 0;
    app.menu = 0;
    app.chrome = 0;
}
void UIXSetErrorReportHandler(void (*handler)(int))
{
    error_handler = handler;
}
int UIXSynchronizeView(void)
{
    if (!window)
        return -1;
    return CALL(UpdateWindow, (window));
}
void UIXWatchDogFailFast(void)
{
    if (error_handler)
        error_handler((int)0x8000ffffu);
}
int phone_unsupported(void)
{
    return NOTIMPL;
}
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
