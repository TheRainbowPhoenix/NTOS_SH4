/* Public COMMCTRL profile. See port-status.json for per-export scope.
 * Ghidra pseudocode is kept under obj/wince/commctrl-decomp for comparison. */
#include "include/ce_controls.h"
#ifdef CE_HOST
#include <stdlib.h>
void *ce_alloc(size_t n)
{
    return malloc(n);
}
void ce_free(void *p)
{
    free(p);
}
#else
#include "../ntimport.h"
NT_IMPORT(void *, CeNativeAlloc, (uint32_t))
NT_DEFINE(CeNativeAlloc)
NT_IMPORT(void, CeNativeFree, (void *)) NT_DEFINE(CeNativeFree) void *ce_alloc(size_t n)
{
    return NT_CALL(CeNativeAlloc, ((uint32_t)n));
}
void ce_free(void *p)
{
    NT_CALL(CeNativeFree, (p));
}
#define CC_SERVICE(ret, name, params, args)                                                        \
    NT_IMPORT(ret, name, params) NT_DEFINE(name) ret name params                                   \
    {                                                                                              \
        return NT_CALL(name, args);                                                                \
    }
CC_SERVICE(CE_HANDLE, CeCreateControl, (unsigned kind, CE_HANDLE parent, uint32_t style, int id),
           (kind, parent, style, id))
CC_SERVICE(CE_RESULT, CeControlMessage,
           (CE_HANDLE handle, unsigned msg, CE_WPARAM wp, CE_LPARAM lp), (handle, msg, wp, lp))
CC_SERVICE(int, CeDestroyControl, (CE_HANDLE handle), (handle))
CC_SERVICE(unsigned, CeControlKind, (CE_HANDLE handle), (handle))
CC_SERVICE(int, CeControlSetBounds, (CE_HANDLE handle, int x, int y, int width, int height),
           (handle, x, y, width, height))
CC_SERVICE(int, CeControlGetBounds, (CE_HANDLE handle, CE_RECT *rect), (handle, rect))
#endif
#ifndef CE_HOST
CC_SERVICE(int, CeDrawStatus, (void *dc, const CE_RECT *rect, const CE_WCHAR *text, uint32_t flags),
           (dc, rect, text, flags))
#endif
#include "common/containers.c"
#ifdef CE_HOST
#include "common/control_model.c"
#endif
#ifndef CE_HOST
#include "common/msvc_callbacks.c"
#endif
#include "commctrl/commandbar.c"
#include "commctrl/propertysheet.c"
static uint32_t registered_classes;
static uint32_t cc_missing_ordinal;
uint32_t cc_unimplemented(uint32_t ordinal)
{
    cc_missing_ordinal = ordinal;
    return 0;
}
uint32_t CeCommonControlsFailure(void)
{
    return cc_missing_ordinal;
}
uint32_t CeCommonControlsClasses(void)
{
    return registered_classes;
}
int InitCommonControlsEx(const CE_INITCOMMON *info)
{
    if (!info || info->size != sizeof(*info))
        return 0;
    registered_classes |= info->classes;
    return 1;
}
void InitCommonControls(void)
{
    registered_classes |= 0xffff;
}
#define REGISTER_CONTROL(name, bit)                                                                \
    int name(void *instance)                                                                       \
    {                                                                                              \
        (void)instance;                                                                            \
        registered_classes |= (bit);                                                               \
        return 1;                                                                                  \
    }
REGISTER_CONTROL(Header_Init, 0x1)
REGISTER_CONTROL(ListView_Init, 0x1)
REGISTER_CONTROL(TV_Init, 0x2)
REGISTER_CONTROL(Tab_Init, 0x8)
REGISTER_CONTROL(InitToolbarClass, 0x4)
REGISTER_CONTROL(InitStatusClass, 0x4)
REGISTER_CONTROL(InitToolTipsClass, 0x8)
REGISTER_CONTROL(InitTrackBar, 0x4)
REGISTER_CONTROL(InitUpDownClass, 0x10)
REGISTER_CONTROL(InitProgressClass, 0x20)
REGISTER_CONTROL(InitDateClasses, 0x100)
REGISTER_CONTROL(InitReBarClass, 0x400)
REGISTER_CONTROL(InitCapEdit, 0x2000)
REGISTER_CONTROL(InitSBEdit, 0x2000)
REGISTER_CONTROL(InitTTButton, 0x2000)
REGISTER_CONTROL(InitTTStatic, 0x2000)
int ListView_SetItemSpacing(CE_HANDLE list, int x, int y)
{
    (void)x;
    (void)y;
    return CeControlKind(list) == CE_CC_LIST;
}
int CenterWindow(CE_HANDLE window, CE_HANDLE parent)
{
    CE_RECT child, outer = {0, 0, 320, 528};
    if (!CeControlGetBounds(window, &child))
        return 0;
    if (parent && !CeControlGetBounds(parent, &outer))
        return 0;
    int w = child.right - child.left, h = child.bottom - child.top;
    return CeControlSetBounds(window, outer.left + (outer.right - outer.left - w) / 2,
                              outer.top + (outer.bottom - outer.top - h) / 2, w, h);
}
CE_HANDLE CreateStatusWindowW(uint32_t style, const CE_WCHAR *text, CE_HANDLE parent, unsigned id)
{
    CE_HANDLE h = CeCreateControl(CE_CC_STATUS, parent, style, (int)id);
    if (h)
        CeControlMessage(h, 0xc, 0, (CE_LPARAM)text);
    return h;
}
void DrawStatusTextW(void *dc, CE_RECT *rect, const CE_WCHAR *text, unsigned flags)
{
#ifdef CE_HOST
    (void)dc;
    (void)rect;
    (void)text;
    (void)flags;
#else
    CeDrawStatus(dc, rect, text, flags);
#endif
}
int InvertRect(void *dc, const CE_RECT *rect)
{
    (void)dc;
    (void)rect;
    cc_missing_ordinal = 22;
    return 0;
}
CE_HANDLE CreateUpDownControl(uint32_t style, int x, int y, int width, int height, CE_HANDLE parent,
                              int id, void *instance, CE_HANDLE buddy, int maximum, int minimum,
                              int position)
{
    (void)instance;
    CE_HANDLE h = CeCreateControl(CE_CC_UPDOWN, parent, style, id);
    if (!h)
        return 0;
    CeControlSetBounds(h, x, y, width, height);
    CeControlMessage(h, 0x465, 0, ((uint32_t)(uint16_t)minimum << 16) | (uint16_t)maximum);
    CeControlMessage(h, 0x469, (CE_WPARAM)buddy, 0);
    CeControlMessage(h, 0x46f, (CE_WPARAM)minimum, maximum);
    CeControlMessage(h, 0x471, 0, position);
    return h;
}
CE_HANDLE CreateToolbarEx(CE_HANDLE parent, uint32_t style, unsigned id, int count, void *instance,
                          uintptr_t bitmap, const void *buttons, int n, int dx, int dy, int bx,
                          int by, unsigned size)
{
    (void)count;
    (void)instance;
    (void)bitmap;
    (void)buttons;
    (void)n;
    (void)dx;
    (void)dy;
    (void)bx;
    (void)by;
    (void)size;
    return CeCreateControl(CE_CC_TOOLBAR, parent, style, (int)id);
}
CE_HANDLE CreateToolbar(CE_HANDLE parent, uint32_t style, unsigned id, int count, void *instance,
                        uintptr_t bitmap, const void *buttons, int n)
{
    return CreateToolbarEx(parent, style, id, count, instance, bitmap, buttons, n, 0, 0, 0, 0, 0);
}
#ifndef CE_HOST
int main(void *instance, uint32_t reason, void *reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == 0) {
        CeCommandBarsReset();
        CePropertyPagesReset();
        registered_classes = 0;
        cc_missing_ordinal = 0;
        return 1;
    }
    InitCommonControls();
    return 1;
}
#endif
