/* Dialog/menu surfaces pending a visual menu manager; correct failure contracts. */
void *LoadMenuW(void *module, const u16 *resource)
{
    (void)module;
    (void)resource;
    g_last_error = 120;
    return 0;
}
void *GetSubMenu(void *menu, int index)
{
    (void)menu;
    (void)index;
    g_last_error = 1401;
    return 0;
}
int DestroyMenu(void *menu)
{
    (void)menu;
    g_last_error = 1401;
    return 0;
}
int TrackPopupMenuEx(void *menu, u32 flags, int x, int y, void *owner, void *params)
{
    (void)menu;
    (void)flags;
    (void)x;
    (void)y;
    (void)owner;
    (void)params;
    g_last_error = 120;
    return 0;
}
int CheckMenuRadioItem(void *menu, u32 first, u32 last, u32 check, u32 flags)
{
    (void)menu;
    (void)first;
    (void)last;
    (void)check;
    (void)flags;
    g_last_error = 120;
    return 0;
}
int MessageBoxW(void *owner, const u16 *text, const u16 *caption, u32 type)
{
    (void)owner;
    (void)type;
    OutputDebugStringW(caption);
    OutputDebugStringW(text);
    g_last_error = 120;
    return 0;
}
/* Image-list metadata: pixels and owned icon copies are a later rendering stage. */
typedef struct CE_IMAGES {
    struct CE_IMAGES *next;
    int width, height;
    u32 flags;
    CE_DPA *icons;
} CE_IMAGES;
static CE_IMAGES *ce_images;
static CE_IMAGES *ce_image_list(void *handle)
{
    for (CE_IMAGES *p = ce_images; p; p = p->next)
        if (p == handle)
            return p;
    return 0;
}
void *ImageList_Create(int width, int height, u32 flags, int initial, int grow)
{
    if (width <= 0 || height <= 0 || initial < 0 || grow < 0) {
        g_last_error = 87;
        return 0;
    }
    CE_IMAGES *p = ce_alloc(sizeof(*p));
    if (!p)
        return 0;
    p->icons = DPA_Create(grow);
    if (!p->icons || !DPA_Grow(p->icons, initial)) {
        DPA_Destroy(p->icons);
        ce_free(p);
        return 0;
    }
    p->width = width;
    p->height = height;
    p->flags = flags;
    p->next = ce_images;
    ce_images = p;
    return p;
}
int ImageList_Destroy(void *handle)
{
    CE_IMAGES **link = &ce_images;
    while (*link && *link != handle)
        link = &(*link)->next;
    if (!*link)
        return 0;
    CE_IMAGES *p = *link;
    *link = p->next;
    DPA_Destroy(p->icons);
    ce_free(p);
    return 1;
}
int ImageList_ReplaceIcon(void *handle, int index, void *icon)
{
    CE_IMAGES *p = ce_image_list(handle);
    if (!p || !icon)
        return -1;
    if (index == -1)
        return DPA_InsertPtr(p->icons, INT_MAX, icon);
    if (index < 0 || index >= p->icons->count)
        return -1;
    p->icons->data[index] = icon;
    return index;
}
int ImageList_GetImageCount(void *handle)
{
    CE_IMAGES *p = ce_image_list(handle);
    return p ? p->icons->count : 0;
}
