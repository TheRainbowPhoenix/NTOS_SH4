/* Shared DSA/DPA storage. Reference COMMCTRL forwards these services to COREDLL.
 * No hidden global heap, truncating pointer casts, or fixed item-count limits. */
#include "../include/ce_types.h"
#include <limits.h>
struct CE_DSA {
    int count;
    unsigned char *data;
    int capacity, size, grow;
};
struct CE_DPA {
    int count;
    void **data;
    void *heap;
    int capacity, grow;
};
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(CE_DSA, data) == 4 && offsetof(CE_DSA, size) == 12, "CE DSA layout");
_Static_assert(offsetof(CE_DPA, data) == 4 && offsetof(CE_DPA, capacity) == 12, "CE DPA layout");
#endif
void *ce_copy(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    for (size_t i = 0; i < n; ++i)
        a[i] = b[i];
    return d;
}
void *ce_zero(void *d, size_t n)
{
    unsigned char *a = d;
    for (size_t i = 0; i < n; ++i)
        a[i] = 0;
    return d;
}
size_t ce_wlen(const CE_WCHAR *s)
{
    size_t n = 0;
    if (s)
        while (s[n])
            ++n;
    return n;
}
void ce_wcopy(CE_WCHAR *d, const CE_WCHAR *s, size_t n)
{
    if (!d || !n)
        return;
    size_t i = 0;
    if (s)
        while (i + 1 < n && s[i]) {
            d[i] = s[i];
            ++i;
        }
    d[i] = 0;
}
static int array_reserve(void **data, int *capacity, int needed, int grow, size_t size)
{
    if (needed < 0 || !size || needed > INT_MAX / (int)size)
        return 0;
    if (needed <= *capacity)
        return 1;
    int extra = grow > 0 ? grow : 8;
    int cap = needed;
    if (cap <= INT_MAX - extra)
        cap += extra;
    if ((size_t)cap > SIZE_MAX / size)
        return 0;
    void *next = ce_alloc((size_t)cap * size);
    if (!next)
        return 0;
    ce_zero(next, (size_t)cap * size);
    if (*data)
        ce_copy(next, *data, (size_t)*capacity * size);
    ce_free(*data);
    *data = next;
    *capacity = cap;
    return 1;
}
CE_DSA *DSA_Create(int size, int grow)
{
    if (size <= 0)
        return 0;
    CE_DSA *a = ce_alloc(sizeof(*a));
    if (!a)
        return 0;
    ce_zero(a, sizeof(*a));
    a->size = size;
    a->grow = grow > 0 ? grow : 8;
    return a;
}
int DSA_DeleteAllItems(CE_DSA *a)
{
    if (!a)
        return 0;
    ce_free(a->data);
    a->data = 0;
    a->count = a->capacity = 0;
    return 1;
}
int DSA_Destroy(CE_DSA *a)
{
    if (!a)
        return 0;
    DSA_DeleteAllItems(a);
    ce_free(a);
    return 1;
}
void *DSA_GetItemPtr(CE_DSA *a, int i)
{
    return a && i >= 0 && i < a->count ? a->data + (size_t)i * a->size : 0;
}
int DSA_GetItem(CE_DSA *a, int i, void *dest)
{
    void *p = DSA_GetItemPtr(a, i);
    if (!p || !dest)
        return 0;
    ce_copy(dest, p, (size_t)a->size);
    return 1;
}
int DSA_InsertItem(CE_DSA *a, int i, const void *item)
{
    if (!a || !item || i < 0 || a->count == INT_MAX)
        return -1;
    if (i > a->count)
        i = a->count;
    /* The source may point inside this array. Copy before any realloc/move. */
    void *copy = ce_alloc((size_t)a->size);
    if (!copy)
        return -1;
    ce_copy(copy, item, (size_t)a->size);
    if (!array_reserve((void **)&a->data, &a->capacity, a->count + 1, a->grow, (size_t)a->size)) {
        ce_free(copy);
        return -1;
    }
    for (int k = a->count; k > i; --k)
        ce_copy(a->data + (size_t)k * a->size, a->data + (size_t)(k - 1) * a->size,
                (size_t)a->size);
    ce_copy(a->data + (size_t)i * a->size, copy, (size_t)a->size);
    ce_free(copy);
    ++a->count;
    return i;
}
int DSA_SetItem(CE_DSA *a, int i, const void *item)
{
    if (!a || !item || i < 0 || i == INT_MAX)
        return 0;
    if (i < a->count) {
        unsigned char *d = a->data + (size_t)i * a->size;
        const unsigned char *s = item;
        if ((uintptr_t)d > (uintptr_t)s && (uintptr_t)d - (uintptr_t)s < (size_t)a->size) {
            for (int k = a->size; k--;)
                d[k] = s[k];
        } else
            ce_copy(d, s, (size_t)a->size);
        return 1;
    }
    void *copy = ce_alloc((size_t)a->size);
    if (!copy)
        return 0;
    ce_copy(copy, item, (size_t)a->size);
    if (!array_reserve((void **)&a->data, &a->capacity, i + 1, a->grow, (size_t)a->size)) {
        ce_free(copy);
        return 0;
    }
    ce_zero(a->data + (size_t)a->count * a->size, (size_t)(i + 1 - a->count) * a->size);
    ce_copy(a->data + (size_t)i * a->size, copy, (size_t)a->size);
    ce_free(copy);
    a->count = i + 1;
    return 1;
}
int DSA_DeleteItem(CE_DSA *a, int i)
{
    if (!DSA_GetItemPtr(a, i))
        return 0;
    for (int k = i; k + 1 < a->count; ++k)
        ce_copy(a->data + (size_t)k * a->size, a->data + (size_t)(k + 1) * a->size,
                (size_t)a->size);
    --a->count;
    return 1;
}
CE_DSA *DSA_Clone(CE_DSA *a)
{
    if (!a)
        return 0;
    CE_DSA *b = DSA_Create(a->size, a->grow);
    if (!b)
        return 0;
    if (!array_reserve((void **)&b->data, &b->capacity, a->count, b->grow, (size_t)b->size)) {
        DSA_Destroy(b);
        return 0;
    }
    b->count = a->count;
    if (a->count)
        ce_copy(b->data, a->data, (size_t)a->count * a->size);
    return b;
}
void DSA_EnumCallback(CE_DSA *a, CE_ENUM cb, void *ctx)
{
    if (!a || !cb)
        return;
    for (int i = 0; i < a->count; ++i)
        if (!cb(a->data + (size_t)i * a->size, ctx))
            break;
}
void DSA_DestroyCallback(CE_DSA *a, CE_ENUM cb, void *ctx)
{
    DSA_EnumCallback(a, cb, ctx);
    DSA_Destroy(a);
}
int DSA_Search(CE_DSA *a, const void *item, int start, CE_COMPARE cmp, CE_LPARAM ctx,
               unsigned options)
{
    if (!a || !item || !cmp)
        return -1;
    if (start < 0)
        start = 0;
    for (int i = start; i < a->count; ++i) {
        int c = cmp(item, DSA_GetItemPtr(a, i), ctx);
        if (!c)
            return i;
        if ((options & 1) && c < 0)
            return options & 6 ? i : -1;
    }
    return (options & 6) ? a->count : -1;
}
CE_DPA *DPA_Create(int grow)
{
    CE_DPA *a = ce_alloc(sizeof(*a));
    if (!a)
        return 0;
    ce_zero(a, sizeof(*a));
    a->grow = grow > 0 ? grow : 8;
    return a;
}
int DPA_Grow(CE_DPA *a, int n)
{
    return a && array_reserve((void **)&a->data, &a->capacity, n, a->grow, sizeof(void *));
}
int DPA_DeleteAllPtrs(CE_DPA *a)
{
    if (!a)
        return 0;
    ce_free(a->data);
    a->data = 0;
    a->count = a->capacity = 0;
    return 1;
}
int DPA_Destroy(CE_DPA *a)
{
    if (!a)
        return 0;
    DPA_DeleteAllPtrs(a);
    ce_free(a);
    return 1;
}
void *DPA_GetPtr(CE_DPA *a, int i)
{
    return a && i >= 0 && i < a->count ? a->data[i] : 0;
}
int DPA_GetPtrIndex(CE_DPA *a, const void *p)
{
    if (a)
        for (int i = 0; i < a->count; ++i)
            if (a->data[i] == p)
                return i;
    return -1;
}
int DPA_InsertPtr(CE_DPA *a, int i, void *p)
{
    if (!a || i < 0 || a->count == INT_MAX)
        return -1;
    if (i > a->count)
        i = a->count;
    if (!DPA_Grow(a, a->count + 1))
        return -1;
    for (int k = a->count; k > i; --k)
        a->data[k] = a->data[k - 1];
    a->data[i] = p;
    ++a->count;
    return i;
}
int DPA_SetPtr(CE_DPA *a, int i, void *p)
{
    if (!a || i < 0 || i == INT_MAX || !DPA_Grow(a, i + 1))
        return 0;
    if (i >= a->count) {
        for (int k = a->count; k <= i; ++k)
            a->data[k] = 0;
        a->count = i + 1;
    }
    a->data[i] = p;
    return 1;
}
void *DPA_DeletePtr(CE_DPA *a, int i)
{
    if (!a || i < 0 || i >= a->count)
        return 0;
    void *p = a->data[i];
    for (int k = i; k + 1 < a->count; ++k)
        a->data[k] = a->data[k + 1];
    --a->count;
    return p;
}
int DPA_Sort(CE_DPA *a, CE_COMPARE cmp, CE_LPARAM ctx)
{
    if (!a || !cmp)
        return 0;
    for (int i = 1; i < a->count; ++i) {
        void *v = a->data[i];
        int j = i;
        while (j > 0 && cmp(a->data[j - 1], v, ctx) > 0) {
            a->data[j] = a->data[j - 1];
            --j;
        }
        a->data[j] = v;
    }
    return 1;
}
int DPA_Search(CE_DPA *a, void *item, int start, CE_COMPARE cmp, CE_LPARAM ctx, unsigned options)
{
    if (!a || !cmp)
        return -1;
    if (start < 0)
        start = 0;
    /* This reference returns the last match for an unsorted search. */
    if (!(options & 1)) {
        int found = -1;
        for (int i = start; i < a->count; ++i)
            if (!cmp(item, a->data[i], ctx))
                found = i;
        return found;
    }
    int lo = 0, hi = a->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (cmp(item, a->data[mid], ctx) > 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < a->count && !cmp(item, a->data[lo], ctx))
        return lo;
    return options & 6 ? lo : -1;
}
void DPA_EnumCallback(CE_DPA *a, CE_ENUM cb, void *ctx)
{
    if (!a || !cb)
        return;
    for (int i = 0; i < a->count; ++i)
        if (!cb(a->data[i], ctx))
            break;
}
void DPA_DestroyCallback(CE_DPA *a, CE_ENUM cb, void *ctx)
{
    DPA_EnumCallback(a, cb, ctx);
    DPA_Destroy(a);
}
CE_DPA *DPA_Clone(CE_DPA *a, CE_DPA *b)
{
    if (!a)
        return 0;
    int created = !b;
    if (!b)
        b = DPA_Create(a->grow);
    if (!b)
        return 0;
    if (!DPA_Grow(b, a->count)) {
        if (created)
            DPA_Destroy(b);
        return 0;
    }
    if (a->count)
        ce_copy(b->data, a->data, (size_t)a->count * sizeof(void *));
    b->count = a->count;
    return b;
}
int Str_SetPtrW(CE_WCHAR **dest, const CE_WCHAR *source)
{
    if (!dest)
        return 0;
    CE_WCHAR *p = 0;
    if (source) {
        size_t n = ce_wlen(source);
        if (n > SIZE_MAX / 2 - 1)
            return 0;
        p = ce_alloc((n + 1) * sizeof(*p));
        if (!p)
            return 0;
        ce_copy(p, source, (n + 1) * sizeof(*p));
    }
    ce_free(*dest);
    *dest = p;
    return 1;
}
CE_DPA *DPA_CreateEx(int grow, void *heap)
{ /* Private heaps need a matching allocator backend. */
    if (heap)
        return 0;
    return DPA_Create(grow);
}
int DSA_Grow(CE_DSA *a, int capacity)
{
    return a && array_reserve((void **)&a->data, &a->capacity, capacity, a->grow, (size_t)a->size);
}
int DSA_SetItemRange(CE_DSA *a, int start, int count, const void *items)
{
    if (!a || !items || start < 0 || count <= 0 || start > INT_MAX - count ||
        (size_t)count > SIZE_MAX / (size_t)a->size)
        return 0;
    size_t bytes = (size_t)count * a->size;
    void *copy = ce_alloc(bytes);
    if (!copy)
        return 0;
    ce_copy(copy, items, bytes);
    int n = start + count;
    if (!DSA_Grow(a, n)) {
        ce_free(copy);
        return 0;
    }
    if (n > a->count) {
        ce_zero(a->data + (size_t)a->count * a->size, (size_t)(n - a->count) * a->size);
        a->count = n;
    }
    ce_copy(a->data + (size_t)start * a->size, copy, bytes);
    ce_free(copy);
    return 1;
}
int DSA_Sort(CE_DSA *a, CE_COMPARE cmp, CE_LPARAM context)
{
    if (!a || !cmp)
        return 0;
    if (a->count < 2)
        return 1;
    void *item = ce_alloc((size_t)a->size);
    if (!item)
        return 0;
    for (int i = 1; i < a->count; ++i) {
        ce_copy(item, DSA_GetItemPtr(a, i), (size_t)a->size);
        int j = i;
        while (j > 0 && cmp(DSA_GetItemPtr(a, j - 1), item, context) > 0) {
            ce_copy(DSA_GetItemPtr(a, j), DSA_GetItemPtr(a, j - 1), (size_t)a->size);
            --j;
        }
        ce_copy(DSA_GetItemPtr(a, j), item, (size_t)a->size);
    }
    ce_free(item);
    return 1;
}
