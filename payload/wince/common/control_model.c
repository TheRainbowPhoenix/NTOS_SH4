/* Small message-driven models; real item ownership, selection and range state.
 * Drawing is provided by the host, keeping GWES/ABI work outside these models. */
#include "../include/ce_controls.h"
#include "../include/ce_items.h"
typedef struct {
    CE_WCHAR *text;
    CE_LPARAM parameter;
    uint32_t state;
    int image, width;
} CE_CC_ITEM;
typedef struct CE_CC_WINDOW {
    struct CE_CC_WINDOW *next;
    unsigned kind;
    CE_HANDLE parent;
    uint32_t style;
    int id, selected, visible;
    int minimum, maximum, position, step;
    CE_RECT bounds;
    CE_DSA *items;
    CE_DSA *columns;
    CE_DPA *tree;
    CE_HANDLE tree_selected;
    void *images[3];
    CE_LPARAM user, procedure;
    CE_WCHAR *text;
} CE_CC_WINDOW;
static CE_CC_WINDOW *ce_cc_windows;
static CE_CC_WINDOW *cc_window(CE_HANDLE h)
{
    for (CE_CC_WINDOW *p = ce_cc_windows; p; p = p->next)
        if (p == h)
            return p;
    return 0;
}
static int cc_add_bound(int a, int b, int lo, int hi)
{
    int64_t n = (int64_t)a + b;
    return n < lo ? lo : n > hi ? hi : (int)n;
}
static int cc_bound(int n, int lo, int hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}
CE_HANDLE CeCreateControl(unsigned kind, CE_HANDLE parent, uint32_t style, int id)
{
    if (kind < CE_CC_LIST || kind > CE_CC_EDIT)
        return 0;
    CE_CC_WINDOW *w = ce_alloc(sizeof(*w));
    if (!w)
        return 0;
    ce_zero(w, sizeof(*w));
    w->items = DSA_Create(sizeof(CE_CC_ITEM), 8);
    w->columns = DSA_Create(sizeof(CE_CC_ITEM), 4);
    if (!w->items || !w->columns) {
        DSA_Destroy(w->items);
        DSA_Destroy(w->columns);
        ce_free(w);
        return 0;
    }
    if (kind == CE_CC_TREE) {
        w->tree = DPA_Create(8);
        if (!w->tree) {
            DSA_Destroy(w->items);
            DSA_Destroy(w->columns);
            ce_free(w);
            return 0;
        }
    }
    w->kind = kind;
    w->parent = parent;
    w->style = style;
    w->id = id;
    w->selected = -1;
    w->visible = 1;
    w->maximum = 100;
    w->step = 10;
    w->bounds.right = 320;
    w->bounds.bottom = 26;
    w->next = ce_cc_windows;
    ce_cc_windows = w;
    return w;
}
static void cc_tree_delete(CE_CC_WINDOW *, CE_HANDLE);
static void cc_clear(CE_DSA *items)
{
    for (int i = 0; i < items->count; ++i) {
        CE_CC_ITEM *p = DSA_GetItemPtr(items, i);
        CE_WCHAR *owned = p->text;
        p->text = 0;
        ce_free(owned);
    }
    DSA_DeleteAllItems(items);
}
int CeDestroyControl(CE_HANDLE h)
{
    CE_CC_WINDOW **link = &ce_cc_windows;
    while (*link && *link != h)
        link = &(*link)->next;
    if (!*link)
        return 0;
    CE_CC_WINDOW *w = *link;
    /* Re-scan because destroying a child can unlink another list node. */
    for (;;) {
        CE_CC_WINDOW *child = ce_cc_windows;
        while (child && child->parent != h)
            child = child->next;
        if (!child)
            break;
        CeDestroyControl(child);
    }
    link = &ce_cc_windows;
    while (*link != w)
        link = &(*link)->next;
    *link = w->next;
    if (w->tree) {
        while (w->tree->count)
            cc_tree_delete(w, w->tree->data[0]);
        DPA_Destroy(w->tree);
    }
    cc_clear(w->items);
    cc_clear(w->columns);
    DSA_Destroy(w->items);
    DSA_Destroy(w->columns);
    ce_free(w->text);
    ce_free(w);
    return 1;
}
void CeControlsReset(void)
{
    while (ce_cc_windows)
        CeDestroyControl(ce_cc_windows);
}
unsigned CeControlKind(CE_HANDLE h)
{
    CE_CC_WINDOW *w = cc_window(h);
    return w ? w->kind : 0;
}
CE_HANDLE CeControlParent(CE_HANDLE h)
{
    CE_CC_WINDOW *w = cc_window(h);
    return w ? w->parent : 0;
}
int CeControlSetBounds(CE_HANDLE h, int x, int y, int width, int height)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w || width < 0 || height < 0 || x > INT_MAX - width || y > INT_MAX - height)
        return 0;
    w->bounds = (CE_RECT){x, y, x + width, y + height};
    return 1;
}
int CeControlGetBounds(CE_HANDLE h, CE_RECT *rect)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w || !rect)
        return 0;
    *rect = w->bounds;
    return 1;
}
CE_LPARAM CeControlGetLong(CE_HANDLE h, int index)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w)
        return 0;
    switch (index) {
    case -21:
        return w->user;
    case -12:
        return w->id;
    case -16:
        return w->style;
    case -4:
        return w->procedure;
    default:
        return 0;
    }
}
CE_LPARAM CeControlSetLong(CE_HANDLE h, int index, CE_LPARAM value)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w)
        return 0;
    CE_LPARAM old = CeControlGetLong(h, index);
    switch (index) {
    case -21:
        w->user = value;
        break;
    case -12:
        w->id = (int)value;
        break;
    case -16:
        w->style = (uint32_t)value;
        break;
    case -4:
        w->procedure = value;
        break;
    default:
        return 0;
    }
    return old;
}
static int cc_insert(CE_CC_WINDOW *w, int index, const CE_WCHAR *text, int image,
                     CE_LPARAM parameter, uint32_t state)
{
    CE_CC_ITEM item;
    ce_zero(&item, sizeof(item));
    if (text && (intptr_t)text != -1 && !Str_SetPtrW(&item.text, text))
        return -1;
    item.image = image;
    item.parameter = parameter;
    item.state = state;
    int n = DSA_InsertItem(w->items, index, &item);
    if (n < 0)
        ce_free(item.text);
    else if (w->selected >= n)
        ++w->selected;
    return n;
}
static int cc_delete(CE_CC_WINDOW *w, int index)
{
    CE_CC_ITEM *item = DSA_GetItemPtr(w->items, index);
    if (!item)
        return 0;
    ce_free(item->text);
    DSA_DeleteItem(w->items, index);
    if (w->selected == index)
        w->selected = -1;
    else if (w->selected > index)
        --w->selected;
    return 1;
}
static CE_RESULT cc_list(CE_CC_WINDOW *w, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_LVITEM *lv = (CE_LVITEM *)lp;
    CE_CC_ITEM *item;
    switch (msg) {
    case 0x1004:
        return w->items->count;
    case 0x104d:
        if (!lv)
            return -1;
        return cc_insert(w, lv->item, (lv->mask & 1) ? lv->text : 0, lv->image, lv->parameter,
                         lv->state & lv->state_mask);
    case 0x104c:
    case 0x104b:
        if (!lv || (item = DSA_GetItemPtr(w->items, lv->item)) == 0)
            return 0;
        if (msg == 0x104c) {
            if ((lv->mask & 1) && !Str_SetPtrW(&item->text, lv->text))
                return 0;
            if (lv->mask & 2)
                item->image = lv->image;
            if (lv->mask & 4)
                item->parameter = lv->parameter;
            if (lv->mask & 8)
                item->state = (item->state & ~lv->state_mask) | (lv->state & lv->state_mask);
        } else {
            if (lv->mask & 1)
                ce_wcopy(lv->text, item->text, lv->text_max > 0 ? (size_t)lv->text_max : 0);
            if (lv->mask & 2)
                lv->image = item->image;
            if (lv->mask & 4)
                lv->parameter = item->parameter;
            if (lv->mask & 8)
                lv->state = item->state & lv->state_mask;
        }
        return 1;
    case 0x1008:
        return cc_delete(w, (int)wp);
    case 0x1009:
        cc_clear(w->items);
        w->selected = -1;
        return 1;
    case 0x102b:
        if (!lv)
            return 0;
        for (int i = 0; i < w->items->count; ++i)
            if ((int)wp == -1 || i == (int)wp) {
                item = DSA_GetItemPtr(w->items, i);
                item->state = (item->state & ~lv->state_mask) | (lv->state & lv->state_mask);
            }
        return 1;
    case 0x102c:
        item = DSA_GetItemPtr(w->items, (int)wp);
        return item ? (item->state & (uint32_t)lp) : 0;
    case 0x1032: {
        int n = 0;
        for (int i = 0; i < w->items->count; ++i) {
            item = DSA_GetItemPtr(w->items, i);
            if (item->state & 2)
                ++n;
        }
        return n;
    }
    case 0x100c:
        for (int i = (int)wp + 1; i < w->items->count; ++i) {
            item = DSA_GetItemPtr(w->items, i);
            if (!(lp & 2) || (item->state & 2))
                return i;
        }
        return -1;
    case 0x1074:
    case 0x1073:
        item = DSA_GetItemPtr(w->items, (int)wp);
        if (!item || !lv)
            return 0;
        if (msg == 0x1074)
            return Str_SetPtrW(&item->text, lv->text);
        ce_wcopy(lv->text, item->text, lv->text_max > 0 ? (size_t)lv->text_max : 0);
        return lv->text_max > 0 && lv->text ? (CE_RESULT)ce_wlen(lv->text) : 0;
    case 0x1002:
        return wp < 3 ? (CE_RESULT)w->images[wp] : 0;
    case 0x1003:
        if (wp < 3) {
            void *old = w->images[wp];
            w->images[wp] = (void *)lp;
            return (CE_RESULT)old;
        }
        return 0;
    case 0x1061: {
        CE_COLUMN *column = (CE_COLUMN *)lp;
        if (!column)
            return -1;
        CE_CC_ITEM c;
        ce_zero(&c, sizeof(c));
        c.width = column->width;
        if ((column->mask & 4) && !Str_SetPtrW(&c.text, column->text))
            return -1;
        int n = DSA_InsertItem(w->columns, (int)wp, &c);
        if (n < 0)
            ce_free(c.text);
        return n;
    }
    case 0x101d:
        item = DSA_GetItemPtr(w->columns, (int)wp);
        return item ? item->width : 0;
    case 0x101e:
        item = DSA_GetItemPtr(w->columns, (int)wp);
        if (!item)
            return 0;
        item->width = (int)lp;
        return 1;
    case 0x1013:
        return (int)wp >= 0 && (int)wp < w->items->count;
    default:
        return 0;
    }
}
static CE_RESULT cc_tab(CE_CC_WINDOW *w, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_TABITEM *tab = (CE_TABITEM *)lp;
    CE_CC_ITEM *item;
    switch (msg) {
    case 0x1304:
        return w->items->count;
    case 0x133e:
        if (!tab)
            return -1;
        return cc_insert(w, (int)wp, (tab->mask & 1) ? tab->text : 0, tab->image, tab->parameter,
                         0);
    case 0x1308:
        return cc_delete(w, (int)wp);
    case 0x1309:
        cc_clear(w->items);
        w->selected = -1;
        return 1;
    case 0x130b:
        return w->selected;
    case 0x130c: {
        int old = w->selected;
        if ((int)wp < -1 || (int)wp >= w->items->count)
            return -1;
        w->selected = (int)wp;
        return old;
    }
    case 0x133c:
    case 0x133d:
        item = DSA_GetItemPtr(w->items, (int)wp);
        if (!item || !tab)
            return 0;
        if (msg == 0x133d) {
            if ((tab->mask & 1) && !Str_SetPtrW(&item->text, tab->text))
                return 0;
            if (tab->mask & 2)
                item->image = tab->image;
            if (tab->mask & 8)
                item->parameter = tab->parameter;
        } else {
            if (tab->mask & 1)
                ce_wcopy(tab->text, item->text, tab->text_max > 0 ? (size_t)tab->text_max : 0);
            if (tab->mask & 2)
                tab->image = item->image;
            if (tab->mask & 8)
                tab->parameter = item->parameter;
        }
        return 1;
    default:
        return 0;
    }
}
#include "control_items.c"
CE_RESULT CeControlMessage(CE_HANDLE h, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w)
        return 0;
    if (msg == 0x000c)
        return Str_SetPtrW(&w->text, (const CE_WCHAR *)lp);
    if (msg == 0x000d) {
        if (!wp || !lp)
            return 0;
        ce_wcopy((CE_WCHAR *)lp, w->text, wp);
        return lp ? (CE_RESULT)ce_wlen((CE_WCHAR *)lp) : 0;
    }
    if (msg == 0x000e)
        return ce_wlen(w->text);
    if (msg == 0x0010 || msg == 0x0002)
        return CeDestroyControl(h);
    if (w->kind == CE_CC_LIST)
        return cc_list(w, msg, wp, lp);
    if (w->kind == CE_CC_TREE)
        return cc_tree(w, msg, wp, lp);
    if (w->kind == CE_CC_HEADER)
        return cc_header(w, msg, wp, lp);
    if (w->kind == CE_CC_TOOLBAR)
        return cc_toolbar(w, msg, wp, lp);
    if (w->kind == CE_CC_UPDOWN) {
        int old = w->position;
        switch (msg) {
        case 0x465:
            w->minimum = (int16_t)((uint32_t)lp >> 16);
            w->maximum = (int16_t)lp;
            return 0;
        case 0x46f:
            w->minimum = (int)wp;
            w->maximum = (int)lp;
            return 0;
        case 0x467:
            w->position = cc_bound((int16_t)lp, w->minimum, w->maximum);
            return old;
        case 0x471:
            w->position = cc_bound((int)lp, w->minimum, w->maximum);
            return old;
        case 0x468:
        case 0x472:
            return w->position;
        case 0x469: {
            void *buddy = w->images[0];
            w->images[0] = (void *)wp;
            return (CE_RESULT)buddy;
        }
        case 0x46a:
            return (CE_RESULT)w->images[0];
        default:
            return 0;
        }
    }
    if (w->kind == CE_CC_TAB)
        return cc_tab(w, msg, wp, lp);
    if (w->kind == CE_CC_PROGRESS) {
        int old = w->position;
        switch (msg) {
        case 0x401:
            w->minimum = (int16_t)lp;
            w->maximum = (int16_t)((uint32_t)lp >> 16);
            break;
        case 0x406:
            w->minimum = (int)wp;
            w->maximum = (int)lp;
            break;
        case 0x402:
            w->position = (int)wp;
            break;
        case 0x403:
            w->position = cc_add_bound(w->position, (int)wp, w->minimum, w->maximum);
            break;
        case 0x404: {
            int s = w->step;
            w->step = (int)wp;
            return s;
        }
        case 0x405:
            w->position = cc_add_bound(w->position, w->step, w->minimum, w->maximum);
            break;
        case 0x408:
            return w->position;
        default:
            return 0;
        }
        if (w->maximum < w->minimum)
            w->maximum = w->minimum;
        w->position = cc_bound(w->position, w->minimum, w->maximum);
        return old;
    }
    if (w->kind == CE_CC_TRACKBAR) {
        switch (msg) {
        case 0x400:
            return w->position;
        case 0x401:
            return w->minimum;
        case 0x402:
            return w->maximum;
        case 0x405:
            w->position = cc_bound((int)lp, w->minimum, w->maximum);
            return 0;
        case 0x406:
            w->minimum = (int16_t)lp;
            w->maximum = (int16_t)((uint32_t)lp >> 16);
            return 0;
        default:
            return 0;
        }
    }
    if (w->kind == CE_CC_STATUS) {
        switch (msg) {
        case 0x40b:
            return Str_SetPtrW(&w->text, (const CE_WCHAR *)lp);
        case 0x40d:
            return ce_wlen(w->text);
        case 0x40c:
            if (lp && w->text)
                ce_wcopy((CE_WCHAR *)lp, w->text, ce_wlen(w->text) + 1);
            return ce_wlen(w->text);
        default:
            return 0;
        }
    }
    return 0;
}
int CeControlPaint(CE_HANDLE h, CE_CONTROL_PAINTER paint, void *ctx)
{
    CE_CC_WINDOW *w = cc_window(h);
    if (!w || !paint)
        return 0;
    paint(ctx, &w->bounds, 0, 0);
    if (w->kind == CE_CC_LIST || w->kind == CE_CC_TAB) {
        CE_RECT r = w->bounds;
        int y = r.top;
        for (int i = 0; i < w->items->count && y < r.bottom; ++i) {
            CE_CC_ITEM *item = DSA_GetItemPtr(w->items, i);
            r.top = y;
            r.bottom = y + 24 < w->bounds.bottom ? y + 24 : w->bounds.bottom;
            paint(ctx, &r, item->text, (item->state & 2) || w->selected == i);
            y += 24;
        }
    } else
        paint(ctx, &w->bounds, w->text, 0);
    return 1;
}
