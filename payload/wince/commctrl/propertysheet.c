/* Page ownership is implemented. Modal dialog execution awaits the host dialog engine. */
typedef struct CE_PAGE {
    struct CE_PAGE *next;
    CE_PROPPAGE descriptor;
    CE_WCHAR *title;
} CE_PAGE;
static CE_PAGE *property_pages;
CE_HANDLE CreatePropertySheetPageW(const CE_PROPPAGE *descriptor)
{
    if (!descriptor || descriptor->size < sizeof(CE_PROPPAGE))
        return 0;
    CE_PAGE *page = ce_alloc(sizeof(*page));
    if (!page)
        return 0;
    ce_zero(page, sizeof(*page));
    page->descriptor = *descriptor;
    if (descriptor->title && (uintptr_t)descriptor->title > 65535) {
        if (!Str_SetPtrW(&page->title, descriptor->title)) {
            ce_free(page);
            return 0;
        }
        page->descriptor.title = page->title;
    }
    page->next = property_pages;
    property_pages = page;
    return page;
}
int DestroyPropertySheetPage(CE_HANDLE handle)
{
    CE_PAGE **link = &property_pages;
    while (*link && *link != handle)
        link = &(*link)->next;
    if (!*link)
        return 0;
    CE_PAGE *page = *link;
    *link = page->next;
    ce_free(page->title);
    ce_free(page);
    return 1;
}
CE_RESULT PropertySheetW(const void *header)
{
    (void)header;
    return -1;
}
void CePropertyPagesReset(void)
{
    while (property_pages)
        DestroyPropertySheetPage(property_pages);
}
