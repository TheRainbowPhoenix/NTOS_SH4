#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static int allocations, fail_after = -1;
static void *test_alloc(size_t n)
{
    if (fail_after == 0)
        return 0;
    if (fail_after > 0)
        --fail_after;
    void *p = malloc(n);
    if (p)
        ++allocations;
    return p;
}
static void test_free(void *p)
{
    if (p) {
        --allocations;
        free(p);
    }
}
#define CE_HOST 1
#define malloc test_alloc
#define free test_free
#include "../commctrl_ce.c"
#include "../common/pe_resources.c"
typedef CE_WCHAR u16;
typedef CE_DWORD u32;
#include "../coredll/format.c"
#undef malloc
#undef free
static int compare(const void *a, const void *b, CE_LPARAM unused)
{
    (void)unused;
    intptr_t x = (intptr_t)a, y = (intptr_t)b;
    return x < y ? -1 : x > y;
}
static int visits;
static int visit(void *item, void *ctx)
{
    (void)item;
    (void)ctx;
    return ++visits < 2;
}
static void put16(unsigned char *p, unsigned n)
{
    p[0] = (unsigned char)n;
    p[1] = (unsigned char)(n >> 8);
}
static void put32(unsigned char *p, unsigned n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}
static void containers(void)
{
    CE_DSA *a = DSA_Create(sizeof(int), 1);
    assert(a);
    int v = 7;
    assert(DSA_InsertItem(a, 0, &v) == 0);
    assert(DSA_InsertItem(a, 0, DSA_GetItemPtr(a, 0)) == 0);
    assert(*(int *)DSA_GetItemPtr(a, 1) == 7);
    v = 42;
    assert(DSA_SetItem(a, 5, &v));
    assert(*(int *)DSA_GetItemPtr(a, 4) == 0);
    CE_DSA *copy = DSA_Clone(a);
    assert(copy && copy->count == 6);
    assert(DSA_DeleteItem(a, 0));
    assert(copy->count == 6);
    fail_after = 0;
    assert(DSA_InsertItem(a, 0, &v) == -1);
    fail_after = -1;
    assert(a->count == 5);
    DSA_Destroy(a);
    DSA_Destroy(copy);
    CE_DPA *p = DPA_Create(1);
    assert(p);
    assert(DPA_InsertPtr(p, 0, (void *)3) == 0);
    assert(DPA_InsertPtr(p, 1, (void *)1) == 1);
    assert(DPA_InsertPtr(p, 2, (void *)2) == 2);
    assert(DPA_Sort(p, compare, 0));
    assert(DPA_GetPtr(p, 0) == (void *)1);
    assert(DPA_Search(p, (void *)2, 0, compare, 0, 1) == 1);
    assert(DPA_GetPtrIndex(p, (void *)3) == 2);
    visits = 0;
    DPA_EnumCallback(p, visit, 0);
    assert(visits == 2);
    CE_DPA *clone = DPA_Clone(p, 0);
    assert(clone && DPA_DeletePtr(clone, 1) == (void *)2);
    DPA_Destroy(clone);
    DPA_Destroy(p);
    CE_WCHAR *text = 0, s[] = {'H', 'i', 0};
    assert(Str_SetPtrW(&text, s));
    fail_after = 0;
    assert(!Str_SetPtrW(&text, s));
    assert(text[1] == 'i');
    fail_after = -1;
    assert(Str_SetPtrW(&text, 0));
    assert(!text);
}
static void controls(void)
{
    CE_INITCOMMON init = {sizeof(init), 0xffff};
    assert(InitCommonControlsEx(&init));
    init.size = 0;
    assert(!InitCommonControlsEx(&init));
    CE_HANDLE list = CeCreateControl(CE_CC_LIST, 0, 0, 1);
    assert(list);
    CE_WCHAR title[] = {'D', 'i', 's', 'p', 'l', 'a', 'y', 0};
    CE_LVITEM item = {0};
    item.mask = 1 | 4 | 8;
    item.item = 0;
    item.text = title;
    item.parameter = 99;
    item.state = 2;
    item.state_mask = 2;
    assert(CeControlMessage(list, 0x104d, 0, (CE_LPARAM)&item) == 0);
    title[0] = 'X';
    assert(CeControlMessage(list, 0x1004, 0, 0) == 1);
    assert(CeControlMessage(list, 0x1032, 0, 0) == 1);
    CE_WCHAR text[16] = {0};
    item.text = text;
    item.text_max = 16;
    assert(CeControlMessage(list, 0x104b, 0, (CE_LPARAM)&item));
    assert(text[0] == 'D' && item.parameter == 99);
    assert(CeControlMessage(list, 0x1008, 0, 0));
    assert(CeControlMessage(list, 0x1004, 0, 0) == 0);
    assert(CeDestroyControl(list));
    assert(!CeControlMessage(list, 0x1004, 0, 0));
    CE_HANDLE tree = CeCreateControl(CE_CC_TREE, 0, 0, 2);
    CE_TVINSERT in = {0};
    in.parent = (void *)(intptr_t)-65536;
    in.item.mask = 1;
    in.item.text = title;
    CE_HANDLE root = (void *)CeControlMessage(tree, 0x1132, 0, (CE_LPARAM)&in);
    assert(root);
    in.parent = root;
    CE_HANDLE child = (void *)CeControlMessage(tree, 0x1132, 0, (CE_LPARAM)&in);
    assert(child);
    assert(CeControlMessage(tree, 0x1105, 0, 0) == 2);
    assert((void *)CeControlMessage(tree, 0x110a, 3, (CE_LPARAM)child) == root);
    assert(CeControlMessage(tree, 0x1101, 0, (CE_LPARAM)root));
    assert(CeControlMessage(tree, 0x1105, 0, 0) == 0);
    CeDestroyControl(tree);
    CE_HANDLE progress = CeCreateControl(CE_CC_PROGRESS, 0, 0, 3);
    CeControlMessage(progress, 0x406, 10, 20);
    CeControlMessage(progress, 0x402, 999, 0);
    assert(CeControlMessage(progress, 0x408, 0, 0) == 20);
    CeDestroyControl(progress);
    CE_HANDLE tab = CeCreateControl(CE_CC_TAB, 0, 0, 4);
    CE_TABITEM ti = {1, title, 16, 0, 0};
    assert(CeControlMessage(tab, 0x133e, 0, (CE_LPARAM)&ti) == 0);
    assert(CeControlMessage(tab, 0x130c, 0, 0) == -1);
    assert(CeControlMessage(tab, 0x130b, 0, 0) == 0);
    CeDestroyControl(tab);
    CE_HANDLE toolbar = CeCreateControl(CE_CC_TOOLBAR, 0, 0, 5);
    CE_TBBUTTON button = {0};
    button.command = 123;
    button.state = 4;
    assert(CeControlMessage(toolbar, 0x414, 1, (CE_LPARAM)&button));
    assert(CeControlMessage(toolbar, 0x419, 123, 0) == 0);
    assert(CeControlMessage(toolbar, 0x409, 123, 0));
    CeControlMessage(toolbar, 0x401, 123, 0);
    assert(!CeControlMessage(toolbar, 0x409, 123, 0));
    CeDestroyControl(toolbar);
    CE_HANDLE up = CreateUpDownControl(0, 0, 0, 10, 20, 0, 42, 0, 0, 20, -10, 99);
    assert(up);
    assert(CeControlMessage(up, 0x472, 0, 0) == 20);
    CeDestroyControl(up);
    CE_HANDLE bar = CommandBar_Create(0, 0, 9);
    assert(bar && CommandBar_Height(bar) == 26);
    assert(CommandBar_Show(bar, 0));
    assert(CommandBar_Height(bar) == 0);
    assert(CommandBar_Show(bar, 1));
    assert(CommandBar_InsertComboBox(bar, 0, 100, 0, 10, 0));
    assert(CommandBar_GetItemWindow(bar, 0));
    CeCommandBarsReset();
    CE_PROPPAGE page = {0};
    page.size = sizeof(page);
    page.title = title;
    CE_HANDLE hp = CreatePropertySheetPageW(&page);
    assert(hp);
    assert(DestroyPropertySheetPage(hp));
    assert(!DestroyPropertySheetPage(hp));
    assert(PropertySheetW(0) == -1);
    CePropertyPagesReset();
    CeControlsReset();
}
static void resources(void)
{
    unsigned char image[1024] = {0};
    put16(image, 0x5a4d);
    put32(image + 60, 0x80);
    put32(image + 0x80, 0x4550);
    put16(image + 0x80 + 20, 224);
    unsigned char *opt = image + 0x98;
    put16(opt, 0x10b);
    put32(opt + 92, 16);
    put32(opt + 112, 0x200);
    put32(opt + 116, 0x100);
    unsigned char *r = image + 0x200;
    put16(r + 14, 1);
    put32(r + 16, 6);
    put32(r + 20, 0x80000018);
    put16(r + 24 + 14, 1);
    put32(r + 40, 1);
    put32(r + 44, 0x80000030);
    put16(r + 48 + 14, 1);
    put32(r + 64, 0x409);
    put32(r + 68, 72);
    put32(r + 72, 0x300);
    put32(r + 76, 46);
    put16(image + 0x300 + 2, 7);
    const char *s = "Display";
    for (int i = 0; i < 7; ++i)
        put16(image + 0x304 + 2 * i, (unsigned)s[i]);
    CE_WCHAR out[16];
    assert(CeLoadString(image, sizeof(image), 1, out, 16) == 7);
    assert(out[0] == 'D' && out[6] == 'y');
    assert(CeLoadString(image, sizeof(image), 1, out, 4) == 3);
    put32(r + 72, 0xfffffff0);
    assert(CeLoadString(image, sizeof(image), 1, out, 16) == 0);
}
static void failures(void)
{
    for (int n = 0; n < 8; ++n) {
        fail_after = n;
        CE_HANDLE h = CeCreateControl(CE_CC_TREE, 0, 0, 1);
        fail_after = -1;
        if (h)
            CeDestroyControl(h);
        assert(allocations == 0);
    }
    CE_HANDLE h = CeCreateControl(CE_CC_PROGRESS, 0, 0, 1);
    assert(h);
    CeControlMessage(h, 0x402, 100, 0);
    CeControlMessage(h, 0x403, INT_MAX, 0);
    assert(CeControlMessage(h, 0x408, 0, 0) == 100);
    CeControlMessage(h, 0x403, (CE_WPARAM)INT_MIN, 0);
    assert(CeControlMessage(h, 0x408, 0, 0) == 0);
    CE_WCHAR text[1] = {123};
    assert(CeControlMessage(h, 0xd, 0, (CE_LPARAM)text) == 0 && text[0] == 123);
    CeDestroyControl(h);
    CE_HANDLE tree = CeCreateControl(CE_CC_TREE, 0, 0, 1);
    CE_TVINSERT in = {0};
    for (int i = 0; i < 256; ++i) {
        in.parent = (void *)CeControlMessage(tree, 0x1132, 0, (CE_LPARAM)&in);
        assert(in.parent);
    }
    assert(CeControlMessage(tree, 0x1105, 0, 0) == 256);
    CeDestroyControl(tree);
    assert(allocations == 0);
}
static void formatting(void)
{
    u16 dest[32], format[] = {'%', '0', '2', 'd', ' ', '%', 's', 0}, word[] = {'O', 'K', 0};
    assert(StringCbPrintfW(dest, sizeof(dest), format, 3, word) == 0);
    assert(dest[0] == '0' && dest[1] == '3' && dest[3] == 'O' && dest[4] == 'K' && dest[5] == 0);
    u16 decimal[] = {'%', 'd', 0};
    assert(StringCbPrintfW(dest, sizeof(dest), decimal, INT_MIN) == 0);
    assert(ce_wlen(dest) == 11 && dest[0] == '-');
    assert(StringCbPrintfW(dest, 4, decimal, 123) == 0x8007007au && dest[1] == 0);
}
int main(void)
{
    containers();
    controls();
    resources();
    failures();
    formatting();
    assert(allocations == 0);
    puts("PASS: containers, failure paths, control messages, owned lifetimes, PE resources; no "
         "leaked allocations");
    return 0;
}
