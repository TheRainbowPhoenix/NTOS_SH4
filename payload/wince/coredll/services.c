/* Bounded per-module services. Unsupported operations fail with WinCE errors. */
#include "../include/ce_controls.h"
#include <stdarg.h>
void *ce_alloc(size_t n)
{
    return coredll_malloc((u32)n);
}
void ce_free(void *p)
{
    coredll_free(p);
}
#include "../common/containers.c"
#include "../common/control_model.c"
#include "../common/pe_resources.c"
/* Local heap handles are registered, so LocalSize never reads arbitrary pointers. */
typedef struct CE_LOCAL {
    struct CE_LOCAL *next;
    u32 size;
    u32 alignment[2];
} CE_LOCAL;
static CE_LOCAL *ce_locals;
static CE_LOCAL *ce_local(void *p)
{
    for (CE_LOCAL *h = ce_locals; h; h = h->next)
        if ((void *)(h + 1) == p)
            return h;
    return 0;
}
void *LocalAlloc(u32 flags, u32 bytes)
{
    /* CE callers use 0x42: accept the moveable flag with pointer-backed handles. */
    if (bytes > 0x3fffffu) {
        g_last_error = 8;
        return 0;
    }
    CE_LOCAL *h = ce_alloc(sizeof(*h) + bytes);
    if (!h) {
        g_last_error = 8;
        return 0;
    }
    h->size = bytes;
    h->next = ce_locals;
    ce_locals = h;
    if (flags & 0x40)
        ce_zero(h + 1, bytes);
    return h + 1;
}
u32 LocalSize(void *p)
{
    CE_LOCAL *h = ce_local(p);
    if (!h) {
        g_last_error = 6;
        return 0;
    }
    return h->size;
}
u32 LocalFree(void *p)
{
    if (!p)
        return 0;
    CE_LOCAL **link = &ce_locals;
    while (*link && (void *)((*link) + 1) != p)
        link = &(*link)->next;
    if (!*link) {
        g_last_error = 6;
        return (u32)p;
    }
    CE_LOCAL *h = *link;
    *link = h->next;
    ce_free(h);
    return 0;
}
void *LocalReAlloc(void *p, u32 bytes, u32 flags)
{
    if (!p)
        return LocalAlloc(flags, bytes);
    CE_LOCAL *h = ce_local(p);
    if (!h) {
        g_last_error = 6;
        return 0;
    }
    if (!bytes) {
        LocalFree(p);
        return 0;
    }
    void *next = LocalAlloc(flags, bytes);
    if (!next)
        return 0;
    ce_copy(next, p, h->size < bytes ? h->size : bytes);
    LocalFree(p);
    return next;
}
static size_t ce_module_size(const unsigned char *module)
{
    if (!module || ce_le16(module) != 0x5a4d)
        return 0;
    u32 pe = ce_le32(module + 60);
    if (pe > 0x1000 || ce_le32(module + pe) != 0x4550)
        return 0;
    u32 size = ce_le32(module + pe + 24 + 56);
    return size >= 0x200 && size < 0x400000 ? size : 0;
}
/* WinCE additionally supports LoadStringW(module,id,NULL,0): return a resource
 * string pointer, not a character count. Cache BE copies of LE PE strings. */
typedef struct CE_RESOURCE_STRING {
    struct CE_RESOURCE_STRING *next;
    void *module;
    u32 id;
    u16 text[];
} CE_RESOURCE_STRING;
static CE_RESOURCE_STRING *ce_resource_strings;
int LoadStringW(void *module, u32 id, u16 *dest, int count)
{
    if (!module)
        module = (void *)0x10000;
    if (dest)
        return CeLoadString(module, ce_module_size(module), id, dest, count);
    for (CE_RESOURCE_STRING *p = ce_resource_strings; p; p = p->next)
        if (p->module == module && p->id == id)
            return (int)(uintptr_t)p->text;
    size_t bytes = 0;
    const unsigned char *block =
        CeFindResource(module, ce_module_size(module), 6, (id >> 4) + 1, &bytes);
    if (!block) {
        g_last_error = 1814;
        return 0;
    }
    size_t offset = 0;
    for (unsigned i = 0; i <= (id & 15); ++i) {
        if (!ce_span(offset, 2, bytes)) {
            g_last_error = 1814;
            return 0;
        }
        unsigned n = ce_le16(block + offset);
        offset += 2;
        if (!ce_span(offset, (size_t)n * 2, bytes) || n > 4096) {
            g_last_error = 1814;
            return 0;
        }
        if (i == (id & 15)) {
            if (!n)
                return 0;
            CE_RESOURCE_STRING *p = ce_alloc(sizeof(*p) + (n + 1) * sizeof(u16));
            if (!p) {
                g_last_error = 8;
                return 0;
            }
            p->module = module;
            p->id = id;
            for (unsigned j = 0; j < n; ++j)
                p->text[j] = ce_le16(block + offset + 2 * j);
            p->text[n] = 0;
            p->next = ce_resource_strings;
            ce_resource_strings = p;
            return (int)(uintptr_t)p->text;
        }
        offset += (size_t)n * 2;
    }
    return 0;
}
typedef struct {
    u32 kind, id;
} CE_STOCK;
static CE_STOCK ce_arrow = {2, 32512};
void *LoadCursorW(void *instance, const u16 *name)
{
    (void)instance;
    if ((uintptr_t)name == 32512 || (uintptr_t)name == 0x7f00)
        return &ce_arrow;
    g_last_error = 1814;
    return 0;
}
void *LoadImageW(void *module, const u16 *name, u32 type, int cx, int cy, u32 flags)
{
    (void)cx;
    (void)cy;
    (void)flags;
    if (type == 2)
        return LoadCursorW(module, name);
    g_last_error = 120;
    return 0;
}
int DestroyIcon(void *icon)
{
    (void)icon;
    g_last_error = 120;
    return 0;
}
int SetForegroundWindow(void *h)
{
    return is_hwnd(h) || CeControlKind(h);
}
static void *ce_focus;
void *SetFocus(void *h)
{
    if (h && !is_hwnd(h) && !CeControlKind(h)) {
        g_last_error = 1400;
        return 0;
    }
    void *old = ce_focus;
    ce_focus = h;
    return old;
}
int IsWindow(void *h)
{
    return is_hwnd(h) || CeControlKind(h);
}
void *GetParent(void *h)
{
    return CeControlParent(h);
}
static CE_LPARAM ce_main_user;
CE_LPARAM GetWindowLongW(void *h, int index)
{
    if (CeControlKind(h))
        return CeControlGetLong(h, index);
    if (!is_hwnd(h))
        return 0;
    return (index == -21 || index == 0) ? ce_main_user
           : index == -4                ? (CE_LPARAM)ce_app_window_proc
                                        : 0;
}
CE_LPARAM SetWindowLongW(void *h, int index, CE_LPARAM value)
{
    if (CeControlKind(h))
        return CeControlSetLong(h, index, value);
    CE_LPARAM old = GetWindowLongW(h, index);
    if (!is_hwnd(h))
        return 0;
    if (index == -21 || index == 0)
        ce_main_user = value;
    else if (index == -4) {
        ce_app_window_proc = (void *)value;
        g_window.proc = ce_msvc_window_proc;
    }
    return old;
}
CE_RESULT SendMessageW(void *h, u32 msg, u32 wp, u32 lp)
{
    if (CeControlKind(h)) {
        /* Resolve callback text through LVN_GETDISPINFOW before storing the row. */
        CE_LVITEM *item = (CE_LVITEM *)lp;
        if (msg == 0x104d && item && (intptr_t)item->text == -1 && g_window.proc) {
            struct {
                void *from;
                u32 id, code;
                CE_LVITEM item;
            } notification;
            u16 text[128] = {0};
            notification.from = h;
            notification.id = 0;
            notification.code = (u32)-177;
            notification.item = *item;
            notification.item.text = text;
            notification.item.text_max = 128;
            g_window.proc(&g_window, 0x4e, 0, (u32)&notification);
            CE_LVITEM resolved = *item;
            resolved.text = text;
            return CeControlMessage(h, msg, wp, (CE_LPARAM)&resolved);
        }
        return CeControlMessage(h, msg, wp, lp);
    }
    if (is_hwnd(h) && g_window.proc)
        return g_window.proc(h, msg, wp, lp);
    g_last_error = 1400;
    return 0;
}
int SetWindowPos(void *h, void *after, int x, int y, int cx, int cy, u32 flags)
{
    (void)after;
    CE_RECT r;
    if (CeControlGetBounds(h, &r)) {
        if (flags & 2) {
            x = r.left;
            y = r.top;
        }
        if (flags & 1) {
            cx = r.right - r.left;
            cy = r.bottom - r.top;
        }
        return CeControlSetBounds(h, x, y, cx, cy);
    }
    if (!is_hwnd(h))
        return 0;
    if (flags & 2) {
        x = g_window.bounds.left;
        y = g_window.bounds.top;
    }
    if (flags & 1) {
        cx = g_window.bounds.right - g_window.bounds.left;
        cy = g_window.bounds.bottom - g_window.bounds.top;
    }
    g_window.bounds = (NT_RECT){x, y, x + cx, y + cy};
    return 1;
}
int MapWindowPoints(void *from, void *to, NT_POINT *points, u32 n)
{
    if (!points && n)
        return 0;
    CE_RECT a = {0}, b = {0};
    CeControlGetBounds(from, &a);
    CeControlGetBounds(to, &b);
    int dx = a.left - b.left, dy = a.top - b.top;
    for (u32 i = 0; i < n; ++i) {
        points[i].x += dx;
        points[i].y += dy;
    }
    return (int)(((u32)(uint16_t)dy << 16) | (uint16_t)dx);
}
int GetKeyState(int key)
{
    (void)key;
    return 0;
}
u32 GetMessagePos(void)
{
    return 0;
}
int SystemParametersInfoW(u32 action, u32 value, void *out, u32 flags)
{
    (void)value;
    (void)flags;
    if (action == 48 && out) {
        *(NT_RECT *)out = (NT_RECT){0, 0, 320, 528};
        return 1;
    }
    g_last_error = 120;
    return 0;
}
u32 RegSetValueExW(void *key, const u16 *name, u32 reserved, u32 type, const void *data, u32 bytes)
{
    (void)key;
    (void)name;
    (void)reserved;
    (void)type;
    (void)data;
    (void)bytes;
    return 5;
}
int SHCreateShortcut(const u16 *shortcut, const u16 *target)
{
    (void)shortcut;
    (void)target;
    g_last_error = 120;
    return 0;
}
int SHGetSpecialFolderPath(void *owner, u16 *path, int folder, int create)
{
    (void)owner;
    (void)folder;
    (void)create;
    if (path)
        path[0] = 0;
    g_last_error = 120;
    return 0;
}
int ShellExecuteEx(void *info)
{
    (void)info;
    g_last_error = 120;
    return 0;
}
int lstrcmpiW(const u16 *a, const u16 *b)
{
    return (int)_wcsicmp(a, b);
}
#include "cpl_loader.c"
int CreateProcessW(const u16 *image, u16 *args, void *sa, void *ta, int inherit, u32 flags,
                   void *env, const u16 *cwd, void *startup, void *info)
{
    (void)image;
    (void)args;
    (void)sa;
    (void)ta;
    (void)inherit;
    (void)flags;
    (void)env;
    (void)cwd;
    (void)startup;
    (void)info;
    g_last_error = 120;
    return 0;
}
int _XcptFilter(u32 code, void *pointers)
{
    (void)code;
    (void)pointers;
    return 0;
}
int __C_specific_handler(void *record, void *frame, void *context, void *dispatcher)
{
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatcher;
    return 1; /* ExceptionContinueSearch */
}
void __report_gsfailure(void)
{
    CeUnsupported(1876);
}
