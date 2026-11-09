#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
IMP(const Catalog *, catalog_CatalogGet, (void))
IMP(int, catalog_CatalogReload, (void))
IMP(int, catalog_CatalogPin, (unsigned, unsigned))
IMP(void, catalog_CatalogDrawIcon, (void *, unsigned, int, int, int))
IMP(void, catalog_CatalogDrawIconTint, (void *, unsigned, int, int, int, U32))
IMP(const Settings *, settings_SettingsGetView, (void))
IMP(int, coredll_launch, (const char *))
IMP(void, PostQuitMessage, (int))
IMP(int, DeleteFileW, (const W16 *))
IMP(int, SipShowIM, (U32))
IMP(int, SipGetInfo, (SipInfo *))
#include "phone_io.h"
static const Render *r;
static const Catalog *cat;
static const Settings *settings;
static int mode, offset, list_offset, jump, search, selected = -1, opened = -1, query_len,
                                                    save_failed;
static int drag_y, drag_x, drag_scroll, drag_mode, drag_valid, tap = -1, editing, dragged;
static int view_bottom = 528;
static void bounds(int b)
{
    view_bottom = b;
    settings->bounds(b);
}
static char query[48];
static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static int match(const char *s)
{
    if (!query_len)
        return 1;
    for (unsigned j = 0; s[j]; ++j) {
        int i = 0;
        while (i < query_len && s[j + i] &&
               lower((unsigned char)s[j + i]) == lower((unsigned char)query[i]))
            ++i;
        if (i == query_len)
            return 1;
    }
    return 0;
}
static int sip_on(void)
{
    SipInfo info;
    info.size = sizeof(info);
    return CALL(SipGetInfo, (&info)) && (info.flags & 1u);
}
static int bottom(void)
{
    return sip_on() ? view_bottom - 257 : view_bottom;
}
static int index_id(unsigned id)
{
    for (unsigned i = 0; i < cat->count; ++i)
        if (cat->apps[i].id == id)
            return (int)i;
    return -1;
}
static int pin_size(unsigned id)
{
    for (unsigned i = 0; i < cat->pin_count; ++i)
        if (cat->pins[i].id == id)
            return cat->pins[i].size;
    return 0;
}
static int home_end(void)
{
    int row = 0;
    for (unsigned i = 0; i < cat->pin_count; ++i) {
        int last = cat->pins[i].y + cat->pins[i].size;
        if (last > row)
            row = last;
    }
    return 28 + row * 52 + 28;
}
static void icon(void *dc, unsigned i, int x, int y, int size)
{
    CALL(catalog_CatalogDrawIcon, (dc, i, x, y, size));
}
static void icon_on_light(void *dc, unsigned i, int x, int y, int size)
{
    CALL(catalog_CatalogDrawIconTint, (dc, i, x, y, size, INK));
}
static void arrow(void *dc, int x, int y, int back)
{
    int d = back ? -1 : 1;
    r->line(dc, x - 9 * d, y, x + 9 * d, y, GRAY);
    r->line(dc, x + 9 * d, y, x + 2 * d, y - 7, GRAY);
    r->line(dc, x + 9 * d, y, x + 2 * d, y + 7, GRAY);
}
static void home(void *dc)
{
    for (unsigned i = 0; i < cat->pin_count; ++i) {
        const Pin *p = &cat->pins[i];
        int idx = index_id(p->id);
        if (idx < 0)
            continue;
        const AppEntry *a = &cat->apps[idx];
        int x = 6 + p->x * 52, y = 28 + p->y * 52 - offset, size = p->size * 52 - 4;
        if (y + size <= 24 || y >= view_bottom)
            continue;
        r->clip(x, y < 24 ? 24 : y, x + size, y + size > view_bottom ? view_bottom : y + size);
        r->fill(dc, x, y, x + size, y + size, a->color);
        if ((a->flags & 1u) && p->size == 2) {
            r->text(dc, x + 7, y + 9, "Latest news", INK, 14);
            r->text(dc, x + 7, y + 32, "Hello from", INK, 12);
            r->text(dc, x + 7, y + 49, "your phone", INK, 12);
        } else {
            int s = p->size == 1 ? 27 : 36;
            icon_on_light(dc, (unsigned)idx, x + ((size - s) >> 1),
                          y + ((size - s) >> 1) - (p->size == 2 ? 5 : 0), s);
        }
        if (p->size == 2)
            r->text(dc, x + 7, y + size - 20, a->name, INK, 12);
        if (selected == idx) {
            r->fill(dc, x, y, x + size, y + 2, WHITE);
            r->fill(dc, x, y, x + 2, y + size, WHITE);
        }
    }
    r->clip(0, 24, 320, view_bottom);
    int y = home_end() - offset;
    r->text(dc, 8, y - 12, "Refresh", GRAY, 12);
    r->text(dc, 232, y - 12, "All apps", INK, 14);
    arrow(dc, 306, y - 4, 0);
    if (!cat->count)
        r->text(dc, 12, 80, "No apps in Apps folder", GRAY, 16);
}
static int rows(void *dc, int hx, int hy)
{
    int y = 80 - list_offset, last = 0, hit = -1, found = 0, limit = bottom();
    for (unsigned i = 0; i < cat->count; ++i) {
        const AppEntry *a = &cat->apps[i];
        if (!match(a->name))
            continue;
        ++found;
        int c = a->name[0];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != last) {
            last = c;
            if (dc) {
                char s[2] = {(char)c, 0};
                r->text(dc, 6, y + 4, s, GRAY, 22);
            }
            if (hx >= 0 && hy >= y && hy < y + 40)
                hit = -2;
            y += 40;
        }
        if (dc && y + 44 > 76 && y < limit) {
            if (selected == (int)i)
                r->fill(dc, 2, y - 1, 313, y + 43, CARD);
            r->fill(dc, 6, y, 44, y + 40, a->color);
            icon_on_light(dc, i, 10, y + 4, 30);
            r->text(dc, 57, y + 8, a->name, INK, 18);
        }
        if (hx >= 0 && hy >= y && hy < y + 44)
            hit = (int)i;
        y += 44;
    }
    if (dc && !found) {
        r->text(dc, 6, 92, "No results", GRAY, 14);
        r->text(dc, 6, 137, "Search Store", INK, 14);
    }
    return hx < 0 ? y + list_offset : hit;
}
static void list(void *dc)
{
    int b = bottom();
    r->clip(0, 76, 314, b);
    rows(dc, -1, -1);
    r->clip(0, 24, 320, b);
    r->fill(dc, 6, 29, 314, 61, search ? BLUE : GRAY);
    r->fill(dc, 8, 31, 312, 59, search ? WHITE : CARD);
    r->text(dc, 17, 36, query_len ? query : (search ? "" : "Search"), search ? INK : MUTED, 14);
    if (search) {
        int caret = 17 + query_len * 7;
        if (caret > 280)
            caret = 280;
        r->fill(dc, caret, 36, caret + 1, 51, INK);
    }
    r->line(dc, 293, 42, 301, 50, search ? 0 : GRAY);
    r->line(dc, 301, 42, 293, 50, search ? 0 : GRAY);
}
static int group_exists(int c)
{
    for (unsigned i = 0; i < cat->count; ++i)
        if (lower(cat->apps[i].name[0]) == lower(c))
            return 1;
    return 0;
}
static void jump_paint(void *dc)
{
    r->fill(dc, 0, 24, 320, 528, SURFACE);
    for (int i = 0; i < 28; ++i) {
        int x = 48 + (i % 4) * 64, y = 54 + div_(i, 4) * 64;
        char s[2] = {i == 0 ? '#' : i == 27 ? '@' : (char)('A' + i - 1), 0};
        r->text(dc, x, y, s, group_exists(s[0]) ? INK : MUTED, 28);
    }
}
static void app_paint(void *dc)
{
    const AppEntry *a = &cat->apps[opened];
    r->fill(dc, 0, 24, 320, 72, PANEL);
    icon_on_light(dc, (unsigned)opened, 14, 36, 26);
    r->text(dc, 49, 42, a->name, INK, 14);
    r->text(dc, 14, 101, a->name, INK, 24);
    if (a->flags & 1u)
        r->text(dc, 14, 161, "Welcome to your phone.", GRAY, 14);
    if (!editing)
        return;
    r->fill(dc, 14, 352, 306, 392, PANEL);
    r->text(dc, 27, 363, pin_size(a->id) ? "Unpin from Start" : "Pin to Start", INK, 14);
    r->fill(dc, 14, 404, 306, 444, PANEL);
    const char *tile_label = pin_size(a->id) == 1 ? "Tile size: small" : "Tile size: medium";
    r->text(dc, 27, 415, tile_label, INK, 14);
    if (save_failed)
        r->text(dc, 14, 470, "Could not save preferences", GRAY, 14);
}
#include "fileview.h"
#include "excelview.h"
static void paint(void *dc)
{
    if (!r || !cat)
        return;
    r->clip(0, 0, 320, 528);
    r->fill(dc, 0, 0, 320, 528, SURFACE);
    r->clip(0, 24, 320, 528);
    if (opened >= 0 && cat->apps[opened].id == APP_SETTINGS_ID)
        settings->paint(dc);
    else if (opened >= 0 && cat->apps[opened].id == 6)
        excel_paint(dc);
    else if (opened >= 0 && cat->apps[opened].id == 7)
        files_paint(dc);
    else if (opened >= 0)
        app_paint(dc);
    else if (jump)
        jump_paint(dc);
    else if (mode)
        list(dc);
    else
        home(dc);
    r->clip(0, 0, 320, 528);
    if (save_failed && opened < 0)
        r->text(dc, 6, 506, "App missing or NTOS.hh3 needs update", INK, 12);
}
static void open_id(unsigned id)
{
    if (id == 0x10000u) {
        opened = selected = -1;
        mode = search = 1;
        jump = query_len = list_offset = drag_valid = 0;
        query[0] = 0;
        CALL(SipShowIM, (1));
        return;
    }
    opened = index_id(id);
    search = jump = editing = 0;
    if (id == APP_SETTINGS_ID)
        settings->reset();
    if (id == 7)
        files_reset();
    if (id == 6)
        excel_reset();
    CALL(SipShowIM, (0));
}
static void activate(unsigned id)
{
    int i = index_id(id);
    if (i < 0)
        return;
    if (CALL(coredll_launch, (cat->apps[i].path))) {
        save_failed = 0;
        CALL(PostQuitMessage, (0));
        return;
    }
    // Settings remains accessible even with an older kernel lacking launch support.
    if (id == APP_SETTINGS_ID)
        open_id(id);
    else
        save_failed = 1;
}
static void navigate(int direction)
{
    if (mode) {
        int i = selected;
        do {
            i += direction;
        } while (i >= 0 && i < (int)cat->count && !match(cat->apps[i].name));
        if (i >= 0 && i < (int)cat->count) {
            selected = i;
            int y = 80, last = 0;
            for (int j = 0; j <= i; ++j) {
                if (!match(cat->apps[j].name))
                    continue;
                int c = lower(cat->apps[j].name[0]);
                if (c != last) {
                    y += 40;
                    last = c;
                }
                if (j < i)
                    y += 44;
            }
            if (y - list_offset < 80)
                list_offset = y - 80;
            if (y + 44 - list_offset > bottom())
                list_offset = y + 44 - bottom();
        }
        return;
    }
    int n = -1;
    for (unsigned i = 0; i < cat->pin_count; ++i)
        if (index_id(cat->pins[i].id) == selected)
            n = (int)i;
    n = n < 0 ? (direction > 0 ? 0 : (int)cat->pin_count - 1)
              : clampi(n + direction, 0, (int)cat->pin_count - 1);
    if (n < 0)
        return;
    const Pin *p = &cat->pins[n];
    selected = index_id(p->id);
    int top = 28 + p->y * 52, bottom = top + p->size * 52;
    if (top - offset < 28)
        offset = top - 28;
    if (bottom - offset > view_bottom - 8)
        offset = bottom - (view_bottom - 8);
    offset = clampi(offset, 0, home_end() > view_bottom - 16 ? home_end() - (view_bottom - 16) : 0);
}
static void jump_to(int c)
{
    int y = 0, last = 0;
    for (unsigned i = 0; i < cat->count; ++i) {
        int l = cat->apps[i].name[0];
        if (l >= 'a' && l <= 'z')
            l -= 32;
        if (last != l) {
            if (l >= c) {
                list_offset = y;
                return;
            }
            last = l;
            y += 40;
        }
        y += 44;
    }
}
static void scroll_by(int delta)
{
    if (!mode) {
        offset = clampi(offset + delta, 0,
                        home_end() > view_bottom - 16 ? home_end() - (view_bottom - 16) : 0);
        return;
    }
    int height = rows(0, -1, -1) - 80, view = bottom() - 80;
    list_offset = clampi(list_offset + delta, 0, height > view ? height - view : 0);
}
static void refresh_catalog(void)
{
    static const W16 cache[] = {'\\', 'f', 'l', 's', '0', '\\', 'a', 'p',
                                'p',  's', '.', 'd', 'a', 't',  0};
    CALL(DeleteFileW, (cache));
    CALL(catalog_CatalogReload, ());
    selected = opened = tap = -1;
    mode = offset = list_offset = drag_valid = 0;
}
static int input(U32 msg, U32 wp, U32 lp)
{
    int x = (int)(short)(lp & 65535u), y = (int)(short)(lp >> 16);
    if (opened >= 0 && cat->apps[opened].id == 7) {
        int result = files_input(msg, wp, x, y);
        if (result < 0) {
            CALL(PostQuitMessage, (0));
            opened = -1;
        }
        return result != 0;
    }
    if (opened >= 0 && cat->apps[opened].id == APP_SETTINGS_ID) {
        if (msg == WM_UP_) {
            drag_valid = dragged = 0;
            tap = -1;
        }
        if (msg == WM_KEY_ && (wp == 0x1b || wp == 0x25) && sip_on()) {
            CALL(SipShowIM, (0));
            return 1;
        }
        int result = settings->input(msg, wp, lp);
        if (result < 0) {
            CALL(PostQuitMessage, (0));
            opened = -1;
            mode = offset = 0;
            CALL(SipShowIM, (0));
            return 1;
        }
        return result;
    }
    if (msg == WM_KEY_ && wp == 0x74) {
        refresh_catalog();
        return 1;
    }
    if (msg == WM_KEY_ && wp == 0x72 && selected >= 0) {
        open_id(cat->apps[selected].id);
        editing = 1;
        return 1;
    }
    if (msg == WM_KEY_ && wp == 0x71) {
        opened = -1;
        mode = search = 1;
        CALL(SipShowIM, (1));
        return 1;
    }
    if (msg == WM_KEY_) {
        drag_valid = dragged = 0;
        tap = -1;
        if (wp == 0x1bu || wp == 0x25u) {
            if (sip_on()) {
                CALL(SipShowIM, (0));
                search = 0;
                return 1;
            }
            if (opened >= 0) {
                CALL(PostQuitMessage, (0));
                opened = -1;
                mode = offset = 0;
            } else if (jump)
                jump = 0;
            else {
                if (!mode && !search && !query_len)
                    CALL(PostQuitMessage, (0));
                mode = search = query_len = offset = 0;
                query[0] = 0;
            }
            return 1;
        }
        if (opened >= 0 && cat->apps[opened].id == 6)
            return excel_input(msg, wp, x, y);
        if (wp == 0x27u) {
            mode = 1;
            selected = -1;
            return 1;
        }
        if (wp == 0x26u || wp == 0x28u) {
            navigate(wp == 0x26u ? -1 : 1);
            return 1;
        }
        if (search && wp == 8) {
            if (query_len) {
                --query_len;
                while (query_len && ((unsigned char)query[query_len] & 0xc0) == 0x80)
                    --query_len;
                query[query_len] = 0;
            }
            list_offset = 0;
            return 1;
        }
        if (wp == 13) {
            if (search) {
                CALL(SipShowIM, (0));
                search = 0;
            } else if (selected >= 0)
                activate(cat->apps[selected].id);
            else
                mode = !mode;
            return 1;
        }
    }
    if ((msg == WM_CHAR_ || msg == WM_KEY_) && search && wp >= 32 && wp <= 0xffff &&
        query_len < 44) {
        if (wp < 128)
            query[query_len++] = (char)wp;
        else if (wp < 2048) {
            query[query_len++] = (char)(0xc0 | (wp >> 6));
            query[query_len++] = (char)(0x80 | (wp & 63));
        } else {
            query[query_len++] = (char)(0xe0 | (wp >> 12));
            query[query_len++] = (char)(0x80 | ((wp >> 6) & 63));
            query[query_len++] = (char)(0x80 | (wp & 63));
        }
        query[query_len] = 0;
        list_offset = 0;
        return 1;
    }
    if (opened >= 0 && cat->apps[opened].id == 6)
        return excel_input(msg, wp, x, y);
    if (msg == WM_UP_) {
        int app = tap, dy = y - drag_y, dx = x - drag_x;
        int launch =
            drag_valid && !dragged && dx >= -8 && dx <= 8 && dy >= -8 && dy <= 8 && app >= 0;
        tap = -1;
        drag_valid = dragged = 0;
        if (launch)
            activate(cat->apps[app].id);
        return launch;
    }
    if (msg == WM_DOWN_ && drag_valid) {
        int dy = y - drag_y, dx = x - drag_x;
        if (dx > 8 || dx < -8 || dy > 8 || dy < -8) {
            dragged = 1;
            tap = -1;
            if (opened < 0 && !jump && drag_mode == mode) {
                int target = drag_scroll + drag_y - y;
                scroll_by(target - (mode ? list_offset : offset));
                selected = -1;
            }
        }
        return 1;
    }

    if (msg == WM_DOWN_) {
        dragged = 0;
        tap = -1;
        drag_x = x;
        drag_y = y;
        drag_scroll = mode ? list_offset : offset;
        drag_mode = mode;
        drag_valid = 1;
        if (y < 24)
            return 0;
        if (opened >= 0) {
            const AppEntry *a = &cat->apps[opened];
            if (y < 80) {
                opened = -1;
                mode = offset = 0;
                return 1;
            }
            if (editing && y >= 352 && y < 392)
                save_failed = !CALL(catalog_CatalogPin, (a->id, pin_size(a->id) ? 0u : 2u));
            if (editing && y >= 404 && y < 444)
                save_failed = !CALL(catalog_CatalogPin, (a->id, pin_size(a->id) == 1 ? 2u : 1u));
            return 1;
        }
        if (jump) {
            if (x >= 32 && x < 288 && y >= 54 && y < 502) {
                int i = div_(y - 54, 64) * 4 + div_(x - 32, 64);
                if (i >= 1 && i <= 26 && group_exists('A' + i - 1))
                    jump_to('A' + i - 1);
            }
            jump = 0;
            return 1;
        }
        if (mode) {
            if (y < 65) {
                if (x > 286 && query_len) {
                    query_len = 0;
                    query[0] = 0;
                    list_offset = 0;
                }
                search = 1;
                CALL(SipShowIM, (1));
                return 1;
            }
            if (y >= bottom())
                return 0;
            int i = rows(0, x, y);
            if (i == -2) {
                jump = 1;
                search = 0;
                CALL(SipShowIM, (0));
                return 1;
            }
            if (i >= 0) {
                selected = i;
                tap = i;
                return 1;
            }
        } else {
            if (y + offset >= home_end() - 24) {
                if (x < 100)
                    refresh_catalog();
                else {
                    mode = 1;
                    selected = -1;
                }
                return 1;
            }
            for (unsigned i = 0; i < cat->pin_count; ++i) {
                const Pin *p = &cat->pins[i];
                int xx = 6 + p->x * 52, yy = 28 + p->y * 52, sz = p->size * 52 - 4;
                if (x >= xx && x < xx + sz && y + offset >= yy && y + offset < yy + sz) {
                    selected = index_id(p->id);
                    tap = selected;
                    return 1;
                }
            }
        }
    }
    if (msg == WM_MOVE_ && drag_valid && opened < 0 && !jump && drag_mode == mode) {
        int dy = y - drag_y, dx = x - drag_x;
        if (dy >= -8 && dy <= 8 && dx >= -8 && dx <= 8)
            return 0;
        dragged = 1;
        tap = -1;
        int target = drag_scroll + drag_y - y;
        scroll_by(target - (mode ? list_offset : offset));
        selected = -1;
        return 1;
    }
    return 0;
}
void StartMenuInitialize(void)
{
    r = CALL(render_SpGetThreadContext, ());
    CALL(catalog_CatalogReload, ());
    cat = CALL(catalog_CatalogGet, ());
    settings = CALL(settings_SettingsGetView, ());
    mode = offset = list_offset = jump = search = query_len = save_failed = drag_valid = dragged =
        0;
    selected = opened = tap = -1;
    editing = 0;
    query[0] = 0;
    CALL(SipShowIM, (0));
    unsigned char intent = 0;
    if (load_("\\fls0\\next.dat", &intent, 1) == 1) {
        static const W16 pending[] = {'\\', 'f', 'l', 's', '0', '\\', 'n', 'e',
                                      'x',  't', '.', 'd', 'a', 't',  0};
        CALL(DeleteFileW, (pending));
        if (intent == 1)
            open_id(0x10000u);
    }
}
void StartMenuUninitialize(void)
{
    CALL(SipShowIM, (0));
    r = 0;
    cat = 0;
}
static int page(void)
{
    return mode;
}
static int scroll(void)
{
    return mode ? list_offset : offset;
}
static const Menu menu = {PROFILE, paint, input, page, scroll, open_id, bounds};
const Menu *UIXGetSchemaFactory(void)
{
    return &menu;
}
void UIXSchemaUnloaded(void)
{
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
