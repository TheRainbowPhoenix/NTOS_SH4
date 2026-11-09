#ifndef CE_COMPAT_TYPES_H
#define CE_COMPAT_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t CE_DWORD;
typedef uint16_t CE_WCHAR;
typedef intptr_t CE_LPARAM;
typedef uintptr_t CE_WPARAM;
typedef intptr_t CE_RESULT;
typedef void *CE_HANDLE;
typedef struct {
    int32_t left, top, right, bottom;
} CE_RECT;
typedef int (*CE_COMPARE)(const void *, const void *, CE_LPARAM);
typedef int (*CE_ENUM)(void *, void *);
void *ce_alloc(size_t bytes);
void ce_free(void *pointer);
void *ce_copy(void *dest, const void *source, size_t bytes);
void *ce_zero(void *dest, size_t bytes);
size_t ce_wlen(const CE_WCHAR *s);
void ce_wcopy(CE_WCHAR *dest, const CE_WCHAR *source, size_t capacity);
/* Handles have owned lifetimes; invalid indices fail without dereferencing. */
typedef struct CE_DSA CE_DSA;
typedef struct CE_DPA CE_DPA;
CE_DSA *DSA_Create(int item_size, int grow);
int DSA_Destroy(CE_DSA *array);
int DSA_DeleteAllItems(CE_DSA *array);
int DSA_DeleteItem(CE_DSA *array, int index);
int DSA_GetItem(CE_DSA *array, int index, void *dest);
void *DSA_GetItemPtr(CE_DSA *array, int index);
int DSA_InsertItem(CE_DSA *array, int index, const void *item);
int DSA_SetItem(CE_DSA *array, int index, const void *item);
CE_DSA *DSA_Clone(CE_DSA *array);
void DSA_EnumCallback(CE_DSA *array, CE_ENUM callback, void *context);
void DSA_DestroyCallback(CE_DSA *array, CE_ENUM callback, void *context);
int DSA_Search(CE_DSA *array, const void *item, int start, CE_COMPARE compare, CE_LPARAM context,
               unsigned options);
CE_DPA *DPA_Create(int grow);
int DPA_Destroy(CE_DPA *array);
int DPA_Grow(CE_DPA *array, int capacity);
void *DPA_GetPtr(CE_DPA *array, int index);
int DPA_GetPtrIndex(CE_DPA *array, const void *item);
int DPA_InsertPtr(CE_DPA *array, int index, void *item);
int DPA_SetPtr(CE_DPA *array, int index, void *item);
void *DPA_DeletePtr(CE_DPA *array, int index);
int DPA_DeleteAllPtrs(CE_DPA *array);
int DPA_Sort(CE_DPA *array, CE_COMPARE compare, CE_LPARAM context);
int DPA_Search(CE_DPA *array, void *item, int start, CE_COMPARE compare, CE_LPARAM context,
               unsigned options);
void DPA_EnumCallback(CE_DPA *array, CE_ENUM callback, void *context);
void DPA_DestroyCallback(CE_DPA *array, CE_ENUM callback, void *context);
CE_DPA *DPA_Clone(CE_DPA *source, CE_DPA *destination);
int Str_SetPtrW(CE_WCHAR **dest, const CE_WCHAR *source);
CE_DPA *DPA_CreateEx(int grow, void *heap);
int DSA_Grow(CE_DSA *array, int capacity);
int DSA_SetItemRange(CE_DSA *array, int start, int count, const void *items);
int DSA_Sort(CE_DSA *array, CE_COMPARE compare, CE_LPARAM context);
#endif
