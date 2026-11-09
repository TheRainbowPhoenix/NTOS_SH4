extern uint32_t nt_msvc_call_typed(const void *, const uint32_t *, uint32_t);
static int ce_msvc_window_proc(void *h, u32 message, u32 wp, u32 lp)
{
    uint32_t words[8] = {(uint32_t)h, message, wp, lp};
    return ce_app_window_proc ? (int)nt_msvc_call_typed(ce_app_window_proc, words, 4)
                              : DefWindowProcW(h, message, wp, lp);
}
u32 RegisterClassW(const NT_WNDCLASS *wc)
{
    if (!wc || !wc->wndproc)
        return 0;
    NT_WNDCLASS native = *wc;
    ce_app_window_proc = (void *)wc->wndproc;
    native.wndproc = ce_msvc_window_proc;
    return ntos_RegisterClassW(&native);
}
/* Adapt the existing one-top-level-window core to shared child control models. */
static unsigned ce_class_kind(const u16 *name)
{
    if (!name || (uintptr_t)name < 65536)
        return 0;
    static const struct {
        const char *name;
        unsigned kind;
    } names[] = {{"SysListView32", CE_CC_LIST},
                 {"SysTreeView32", CE_CC_TREE},
                 {"SysHeader32", CE_CC_HEADER},
                 {"SysTabControl32", CE_CC_TAB},
                 {"ToolbarWindow32", CE_CC_TOOLBAR},
                 {"msctls_statusbar32", CE_CC_STATUS},
                 {"msctls_progress32", CE_CC_PROGRESS},
                 {"msctls_trackbar32", CE_CC_TRACKBAR},
                 {"msctls_updown32", CE_CC_UPDOWN},
                 {"tooltips_class32", CE_CC_TOOLTIP},
                 {"ReBarWindow32", CE_CC_REBAR},
                 {"ComboBox", CE_CC_COMBO},
                 {"Edit", CE_CC_EDIT}};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const char *s = names[i].name;
        unsigned j = 0;
        while (name[j] && s[j]) {
            unsigned a = name[j], b = (unsigned char)s[j];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (a != b)
                break;
            ++j;
        }
        if (!name[j] && !s[j])
            return names[i].kind;
    }
    return 0;
}
void *CreateWindowExW(u32 exstyle, const u16 *class_name, const u16 *title, u32 style, int x, int y,
                      int width, int height, void *parent, void *menu, void *instance, void *param)
{
    unsigned kind = ce_class_kind(class_name);
    if (!kind)
        return ntos_CreateWindowExW(exstyle, class_name, title, style, x, y, width, height, parent,
                                    menu, instance, param);
    CE_HANDLE h = CeCreateControl(kind, parent, style, (int)(uintptr_t)menu);
    if (!h)
        return 0;
    if (!CeControlSetBounds(h, x < 0 ? 0 : x, y < 0 ? 0 : y, width > 0 ? width : 320,
                            height > 0 ? height : 26)) {
        CeDestroyControl(h);
        return 0;
    }
    CeControlMessage(h, 0xc, 0, (CE_LPARAM)title);
    return h;
}
int DestroyWindow(void *h)
{
    return CeControlKind(h) ? CeDestroyControl(h) : ntos_DestroyWindow(h);
}
int ShowWindow(void *h, int command)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w)
        return ntos_ShowWindow(h, command);
    int old = w->visible;
    w->visible = command != 0;
    return old;
}
int GetClientRect(void *h, NT_RECT *rect)
{
    if (!rect)
        return 0;
    CE_RECT bounds;
    if (CeControlGetBounds(h, &bounds)) {
        *rect = (NT_RECT){0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
        return 1;
    }
    return ntos_GetClientRect(h, rect);
}
typedef struct {
    void *dc;
    unsigned kind;
} CE_PAINT_CONTEXT;
static void ce_paint_control(void *context, const CE_RECT *r, const u16 *text, int selected)
{
    CE_PAINT_CONTEXT *paint = context;
    void *dc = paint->dc;
    NT_RECT rect = {r->left, r->top, r->right, r->bottom};
    unsigned chrome =
        paint->kind == CE_CC_COMMAND || paint->kind == CE_CC_TOOLBAR || paint->kind == CE_CC_STATUS;
    void *brush = CreateSolidBrush(selected ? 0x00c06000 : chrome ? 0x00c0c0c0 : 0x00ffffff);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    u32 previous = SetTextColor(dc, selected ? 0x00ffffff : 0);
    if (text)
        TextOutW(dc, r->left + 3, r->top + 4, text, (int)ce_wlen(text));
    SetTextColor(dc, previous);
}
static void ce_paint_children(void *parent)
{
    for (CE_CC_WINDOW *w = ce_cc_windows; w; w = w->next) {
        if (w->parent != parent || !w->visible)
            continue;
        CE_PAINT_CONTEXT paint = {GetDC(w), w->kind};
        CeControlPaint(w, ce_paint_control, &paint);
        ce_paint_children(w);
    }
}
int UpdateWindow(void *h)
{
    if (CeControlKind(h)) {
        CE_PAINT_CONTEXT paint = {GetDC(h), CeControlKind(h)};
        int result = CeControlPaint(h, ce_paint_control, &paint);
        ce_paint_children(h);
        coredll_refresh();
        return result;
    }
    if (!is_hwnd(h))
        return 0;
    // A top-level paint owns its background; loader diagnostics must not remain.
    void *dc = GetDC(h);
    NT_RECT client = {0, 0, 320, 528};
    void *background = CreateSolidBrush(0x00ffffff);
    FillRect(dc, &client, background);
    DeleteObject(background);
    int result = ntos_UpdateWindow(h);
    ce_paint_children(h);
    // Minimal WinCE title chrome, using the actual resource-derived window title.
    if (g_window.title[0]) {
        NT_RECT title = {0, 0, 320, 24};
        void *brush = CreateSolidBrush(0x00800000);
        FillRect(dc, &title, brush);
        DeleteObject(brush);
        u32 old = SetTextColor(dc, 0x00ffffff);
        TextOutW(dc, 5, 5, g_window.title, (int)ce_wlen(g_window.title));
        SetTextColor(dc, old);
        NT_RECT close_rect = {288, 0, 320, 24};
        brush = CreateSolidBrush(0x003030a0);
        FillRect(dc, &close_rect, brush);
        DeleteObject(brush);
        static const u16 close_text[] = {'X', 0};
        old = SetTextColor(dc, 0x00ffffff);
        TextOutW(dc, 300, 5, close_text, 1);
        SetTextColor(dc, old);
    }
    coredll_refresh();
    return result;
}
int InvalidateRect(void *h, const NT_RECT *rect, int erase)
{
    if (!CeControlKind(h))
        return ntos_InvalidateRect(h, rect, erase);
    return enqueue(h, 15, 0, 0);
}
int PostMessageW(void *h, u32 msg, u32 wp, u32 lp)
{
    if (h && !IsWindow(h))
        return 0;
    return enqueue(h, msg, wp, lp);
}
int DispatchMessageW(const NT_MSG *msg)
{
    if (!msg)
        return 0;
    static int close_pressed;
    int over_close = msg->pt.x >= 288 && msg->pt.x < 320 && msg->pt.y >= 0 && msg->pt.y < 24;
    if (msg->message == 0x201)
        close_pressed = over_close;
    if (msg->message == 0x202) {
        int close = close_pressed && over_close;
        close_pressed = 0;
        if (close) {
            DestroyWindow(&g_window);
            PostQuitMessage(0);
            return 0;
        }
    }
    if (CeControlKind(msg->hwnd)) {
        if (msg->message == 15)
            return UpdateWindow(msg->hwnd);
        return (int)CeControlMessage(msg->hwnd, msg->message, msg->wParam, msg->lParam);
    }
    return ntos_DispatchMessageW(msg);
}
int GetSystemMetrics(int index)
{
    static const int metrics[] = {320, 528, 16, 16, 24, 1,  1,   2,   2, 16,
                                  16,  32,  32, 16, 16, 24, 320, 504, 0, 1};
    if (index >= 0 && (unsigned)index < sizeof(metrics) / sizeof(metrics[0]))
        return metrics[index];
    return ntos_GetSystemMetrics(index);
}
int CeDrawStatus(void *dc, const CE_RECT *bounds, const u16 *text, u32 flags)
{
    if (!dc || !bounds || !text)
        return 0;
    NT_RECT r = {bounds->left, bounds->top, bounds->right, bounds->bottom};
    void *brush = CreateSolidBrush(0x00c0c0c0);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    return DrawTextW(dc, text, -1, &r, flags & 0x7ff);
}
