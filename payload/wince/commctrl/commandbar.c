/* Command bars own their model and child controls; menus remain host resources. */
typedef struct CE_BAR {
    struct CE_BAR *next;
    CE_HANDLE handle, menu;
    CE_DPA *children;
    uint32_t flags;
    int visible, bitmap_count;
} CE_BAR;
static CE_BAR *command_bars;
static CE_BAR *bar_find(CE_HANDLE h)
{
    for (CE_BAR *p = command_bars; p; p = p->next)
        if (p->handle == h)
            return p;
    return 0;
}
CE_HANDLE CommandBar_Create(void *instance, CE_HANDLE parent, int id)
{
    (void)instance;
    CE_BAR *b = ce_alloc(sizeof(*b));
    if (!b)
        return 0;
    ce_zero(b, sizeof(*b));
    b->handle = CeCreateControl(CE_CC_COMMAND, parent, 0, id);
    b->children = DPA_Create(4);
    if (!b->handle || !b->children) {
        if (b->handle)
            CeDestroyControl(b->handle);
        DPA_Destroy(b->children);
        ce_free(b);
        return 0;
    }
    b->visible = 1;
    b->next = command_bars;
    command_bars = b;
    return b->handle;
}
int CommandBar_Show(CE_HANDLE h, int show)
{
    CE_BAR *b = bar_find(h);
    if (!b || !CeControlKind(h))
        return 0;
    b->visible = show != 0;
    return 1;
}
int CommandBar_Height(CE_HANDLE h)
{
    CE_BAR *b = bar_find(h);
    CE_RECT r;
    if (!b || !b->visible || !CeControlGetBounds(h, &r))
        return 0;
    return r.bottom - r.top;
}
int CommandBar_AddAdornments(CE_HANDLE h, uint32_t flags, uint32_t reserved)
{
    (void)reserved;
    CE_BAR *b = bar_find(h);
    if (!b || !CeControlKind(h))
        return 0;
    b->flags = flags;
    return 1;
}
int CommandBar_AlignAdornments(CE_HANDLE h)
{
    return bar_find(h) && CeControlKind(h);
}
int CommandBar_InsertMenubarEx(CE_HANDLE h, void *instance, uintptr_t resource, int index,
                               unsigned flags)
{
    (void)instance;
    (void)index;
    (void)flags;
    CE_BAR *b = bar_find(h);
    if (!b || !resource || !CeControlKind(h))
        return 0; /* Host resource binding is deferred; never return a fabricated HMENU. */
    b->menu = 0;
    return 1;
}
int CommandBar_InsertMenubar(CE_HANDLE h, void *instance, uintptr_t resource, int index)
{
    return CommandBar_InsertMenubarEx(h, instance, resource, index, 0);
}
CE_HANDLE CommandBar_GetMenu(CE_HANDLE h, int index)
{
    (void)index;
    CE_BAR *b = bar_find(h);
    return b ? b->menu : 0;
}
int CommandBar_DrawMenuBar(CE_HANDLE h, int index)
{
    (void)index;
    return bar_find(h) && CeControlKind(h);
}
int CommandBar_AddBitmap(CE_HANDLE h, void *instance, uintptr_t resource, int count, int width,
                         int height)
{
    (void)instance;
    (void)resource;
    (void)width;
    (void)height;
    CE_BAR *b = bar_find(h);
    if (!b || count < 0 || count > INT_MAX - b->bitmap_count)
        return -1;
    int first = b->bitmap_count;
    b->bitmap_count += count;
    return first;
}
CE_HANDLE CommandBar_GetItemWindow(CE_HANDLE h, int index)
{
    CE_BAR *b = bar_find(h);
    return b ? DPA_GetPtr(b->children, index) : 0;
}
int CommandBar_InsertControl(CE_HANDLE h, void *instance, const CE_WCHAR *class_name, void *create,
                             unsigned style, unsigned id, int index, int width)
{
    (void)instance;
    (void)class_name;
    (void)create;
    CE_BAR *b = bar_find(h);
    if (!b)
        return 0;
    CE_HANDLE child = CeCreateControl(CE_CC_COMBO, h, style, (int)id);
    if (!child)
        return 0;
    CeControlSetBounds(child, 0, 0, width > 0 ? width : 80, 26);
    if (DPA_InsertPtr(b->children, index, child) < 0) {
        CeDestroyControl(child);
        return 0;
    }
    return 1;
}
int CommandBar_InsertComboBox(CE_HANDLE h, void *instance, int width, unsigned style, unsigned id,
                              int index)
{
    return CommandBar_InsertControl(h, instance, 0, 0, style, id, index, width);
}
int IsCommandBarMessage(CE_HANDLE h, const void *message)
{
    (void)h;
    (void)message;
    return 0;
}
CE_HANDLE CommandBands_Create(void *instance, CE_HANDLE parent, unsigned id, unsigned style,
                              void *images)
{
    (void)instance;
    (void)images;
    return CeCreateControl(CE_CC_REBAR, parent, style, (int)id);
}
int CommandBands_AddBands(CE_HANDLE bands, void *instance, unsigned count, const void *info)
{
    (void)instance;
    (void)count;
    (void)info;
    (void)bands;
    return 0;
}
CE_HANDLE CommandBands_GetCommandBar(CE_HANDLE bands, unsigned index)
{
    (void)bands;
    (void)index;
    return 0;
}
int CommandBands_AddAdornments(CE_HANDLE bands, void *instance, unsigned flags, void *restore)
{
    (void)instance;
    (void)flags;
    (void)restore;
    return CeControlKind(bands) == CE_CC_REBAR;
}
int CommandBands_Show(CE_HANDLE bands, int show)
{
    (void)show;
    return CeControlKind(bands) == CE_CC_REBAR;
}
int CommandBands_GetRestoreInformation(CE_HANDLE bands, unsigned index, void *info)
{
    (void)bands;
    (void)index;
    (void)info;
    return 0;
}
void CeCommandBarsReset(void)
{
    while (command_bars) {
        CE_BAR *b = command_bars;
        command_bars = b->next;
        CeDestroyControl(b->handle);
        DPA_Destroy(b->children);
        ce_free(b);
    }
}
