/* Tree item handles stay stable when the backing pointer array grows. */
typedef struct CE_TREE_NODE {
    CE_HANDLE parent;
    CE_WCHAR *text;
    CE_LPARAM parameter;
    uint32_t state;
    int image, selected_image, children;
} CE_TREE_NODE;
static CE_TREE_NODE *cc_node(CE_CC_WINDOW *w, CE_HANDLE h)
{
    return w->tree && DPA_GetPtrIndex(w->tree, h) >= 0 ? h : 0;
}
static void cc_tree_delete(CE_CC_WINDOW *w, CE_HANDLE h)
{
    CE_HANDLE current = h;
    while (cc_node(w, current)) {
        CE_TREE_NODE *child = 0;
        for (int i = 0; i < w->tree->count; ++i) {
            CE_TREE_NODE *p = w->tree->data[i];
            if (p->parent == current) {
                child = p;
                break;
            }
        }
        if (child) {
            current = child;
            continue;
        }
        CE_TREE_NODE *node = cc_node(w, current);
        CE_HANDLE parent = node->parent;
        int index = DPA_GetPtrIndex(w->tree, current);
        DPA_DeletePtr(w->tree, index);
        if (w->tree_selected == current)
            w->tree_selected = 0;
        int done = current == h;
        ce_free(node->text);
        ce_free(node);
        if (done)
            break;
        current = parent;
    }
}
static CE_RESULT cc_tree(CE_CC_WINDOW *w, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_TREE_NODE *node;
    switch (msg) {
    case 0x1132: {
        CE_TVINSERT *in = (CE_TVINSERT *)lp;
        if (!in)
            return 0;
        CE_HANDLE parent = in->parent;
        if (parent == (CE_HANDLE)(intptr_t)-65536)
            parent = 0;
        if (parent && !cc_node(w, parent))
            return 0;
        node = ce_alloc(sizeof(*node));
        if (!node)
            return 0;
        ce_zero(node, sizeof(*node));
        node->parent = parent;
        node->image = in->item.image;
        node->selected_image = in->item.selected_image;
        node->parameter = in->item.parameter;
        node->state = in->item.state & in->item.state_mask;
        if ((in->item.mask & 1) && !Str_SetPtrW(&node->text, in->item.text)) {
            ce_free(node);
            return 0;
        }
        int index = w->tree->count;
        if (in->after == (CE_HANDLE)(intptr_t)-65535)
            index = 0;
        else {
            int previous = DPA_GetPtrIndex(w->tree, in->after);
            if (previous >= 0)
                index = previous + 1;
        }
        if (DPA_InsertPtr(w->tree, index, node) < 0) {
            ce_free(node->text);
            ce_free(node);
            return 0;
        }
        return (CE_RESULT)node;
    }
    case 0x1105:
        return w->tree->count;
    case 0x1101:
        if ((CE_HANDLE)lp == (CE_HANDLE)(intptr_t)-65536 || !lp) {
            while (w->tree->count)
                cc_tree_delete(w, w->tree->data[0]);
            return 1;
        }
        if (!cc_node(w, (CE_HANDLE)lp))
            return 0;
        cc_tree_delete(w, (CE_HANDLE)lp);
        return 1;
    case 0x110a: {
        CE_HANDLE h = (CE_HANDLE)lp;
        if (wp == 9)
            return (CE_RESULT)w->tree_selected;
        if (wp == 0)
            h = 0;
        else if (wp == 4) {
            if (!cc_node(w, h))
                return 0;
        } else {
            node = cc_node(w, h);
            if (!node)
                return 0;
            if (wp == 3)
                return (CE_RESULT)node->parent;
        }
        if (wp == 0 || wp == 4) {
            for (int i = 0; i < w->tree->count; ++i) {
                node = w->tree->data[i];
                if (node->parent == h)
                    return (CE_RESULT)node;
            }
            return 0;
        }
        int index = DPA_GetPtrIndex(w->tree, h), step = wp == 2 ? -1 : 1;
        node = cc_node(w, h);
        for (int i = index + step; i >= 0 && i < w->tree->count; i += step) {
            CE_TREE_NODE *p = w->tree->data[i];
            if (p->parent == node->parent)
                return (CE_RESULT)p;
        }
        return 0;
    }
    case 0x110b:
        if (wp != 9)
            return 0;
        if (lp && !cc_node(w, (CE_HANDLE)lp))
            return 0;
        w->tree_selected = (CE_HANDLE)lp;
        return 1;
    case 0x113e:
    case 0x113f: {
        CE_TVITEM *tv = (CE_TVITEM *)lp;
        if (!tv || (node = cc_node(w, tv->item)) == 0)
            return 0;
        if (msg == 0x113f) {
            if ((tv->mask & 1) && !Str_SetPtrW(&node->text, tv->text))
                return 0;
            if (tv->mask & 2)
                node->image = tv->image;
            if (tv->mask & 4)
                node->parameter = tv->parameter;
            if (tv->mask & 8)
                node->state = (node->state & ~tv->state_mask) | (tv->state & tv->state_mask);
        } else {
            if (tv->mask & 1)
                ce_wcopy(tv->text, node->text, tv->text_max > 0 ? (size_t)tv->text_max : 0);
            if (tv->mask & 2)
                tv->image = node->image;
            if (tv->mask & 4)
                tv->parameter = node->parameter;
            if (tv->mask & 8)
                tv->state = node->state & tv->state_mask;
        }
        return 1;
    }
    case 0x1102:
        node = cc_node(w, (CE_HANDLE)lp);
        if (!node)
            return 0;
        if (wp & 2)
            node->state |= 0x20;
        else
            node->state &= ~0x20u;
        return 1;
    default:
        return 0;
    }
}
static CE_RESULT cc_header(CE_CC_WINDOW *w, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_HDITEM *hd = (CE_HDITEM *)lp;
    CE_CC_ITEM *item;
    switch (msg) {
    case 0x1200:
        return w->items->count;
    case 0x1202:
        return cc_delete(w, (int)wp);
    case 0x120a:
        if (!hd)
            return -1;
        {
            int i =
                cc_insert(w, (int)wp, (hd->mask & 2) ? hd->text : 0, hd->image, hd->parameter, 0);
            item = DSA_GetItemPtr(w->items, i);
            if (item)
                item->width = hd->width;
            return i;
        }
    case 0x120b:
    case 0x120c:
        item = DSA_GetItemPtr(w->items, (int)wp);
        if (!item || !hd)
            return 0;
        if (msg == 0x120c) {
            if ((hd->mask & 2) && !Str_SetPtrW(&item->text, hd->text))
                return 0;
            if (hd->mask & 1)
                item->width = hd->width;
            if (hd->mask & 8)
                item->parameter = hd->parameter;
        } else {
            if (hd->mask & 2)
                ce_wcopy(hd->text, item->text, hd->text_max > 0 ? (size_t)hd->text_max : 0);
            if (hd->mask & 1)
                hd->width = item->width;
            if (hd->mask & 8)
                hd->parameter = item->parameter;
        }
        return 1;
    default:
        return 0;
    }
}
static CE_RESULT cc_toolbar(CE_CC_WINDOW *w, unsigned msg, CE_WPARAM wp, CE_LPARAM lp)
{
    CE_CC_ITEM *item;
    switch (msg) {
    case 0x414: {
        const CE_TBBUTTON *buttons = (const CE_TBBUTTON *)lp;
        if (!buttons && wp)
            return 0;
        int old = w->items->count;
        for (size_t i = 0; i < wp; ++i) {
            const CE_TBBUTTON *b = &buttons[i];
            int index = cc_insert(
                w, w->items->count,
                (uintptr_t)b->string > 65535 && b->string != -1 ? (CE_WCHAR *)b->string : 0,
                b->bitmap, (CE_LPARAM)b->data, b->state);
            if (index < 0) {
                while (w->items->count > old)
                    cc_delete(w, w->items->count - 1);
                return 0;
            }
            item = DSA_GetItemPtr(w->items, index);
            item->width = b->command;
        }
        return 1;
    }
    case 0x418:
        return w->items->count;
    case 0x416:
        return cc_delete(w, (int)wp);
    case 0x419:
        for (int i = 0; i < w->items->count; ++i) {
            item = DSA_GetItemPtr(w->items, i);
            if (item->width == (int)wp)
                return i;
        }
        return -1;
    case 0x417: {
        CE_TBBUTTON *b = (CE_TBBUTTON *)lp;
        item = DSA_GetItemPtr(w->items, (int)wp);
        if (!item || !b)
            return 0;
        ce_zero(b, sizeof(*b));
        b->bitmap = item->image;
        b->command = item->width;
        b->state = (uint8_t)item->state;
        b->data = (CE_WPARAM)item->parameter;
        b->string = (CE_LPARAM)item->text;
        return 1;
    }
    case 0x401:
    case 0x402:
    case 0x409:
    case 0x40a:
        for (int i = 0; i < w->items->count; ++i) {
            item = DSA_GetItemPtr(w->items, i);
            if (item->width == (int)wp) {
                unsigned flag = (msg == 0x401 || msg == 0x409) ? 4 : 1;
                if (msg >= 0x409)
                    return (item->state & flag) != 0;
                if (lp)
                    item->state |= flag;
                else
                    item->state &= ~flag;
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}
