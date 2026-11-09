/* GCC -> original MSVC SH4 callbacks. The upstream call gate reserves home slots
 * and preserves GBR/PR; the word arrays are initialized even for short calls. */
#include "../include/ce_types.h"
extern uint32_t nt_msvc_call_typed(const void *, const uint32_t *, uint32_t);
typedef struct {
    const void *function;
    CE_LPARAM context;
} CE_MSCALL;
static int ce_compare_msvc(const void *left, const void *right, CE_LPARAM p)
{
    CE_MSCALL *call = (CE_MSCALL *)p;
    uint32_t w[8] = {(uint32_t)(uintptr_t)left, (uint32_t)(uintptr_t)right,
                     (uint32_t)call->context};
    return (int)nt_msvc_call_typed(call->function, w, 3);
}
static int ce_enum_msvc(void *item, void *p)
{
    CE_MSCALL *call = p;
    uint32_t w[8] = {(uint32_t)(uintptr_t)item, (uint32_t)call->context};
    return (int)nt_msvc_call_typed(call->function, w, 2);
}
int CeMsvc_DPA_Sort(CE_DPA *a, CE_COMPARE callback, CE_LPARAM context)
{
    if (!callback)
        return 0;
    CE_MSCALL call = {(const void *)callback, context};
    return DPA_Sort(a, ce_compare_msvc, (CE_LPARAM)&call);
}
int CeMsvc_DSA_Sort(CE_DSA *a, CE_COMPARE callback, CE_LPARAM context)
{
    if (!callback)
        return 0;
    CE_MSCALL call = {(const void *)callback, context};
    return DSA_Sort(a, ce_compare_msvc, (CE_LPARAM)&call);
}
int CeMsvc_DPA_Search(CE_DPA *a, void *item, int start, CE_COMPARE callback, CE_LPARAM context,
                      unsigned options)
{
    if (!callback)
        return -1;
    CE_MSCALL call = {(const void *)callback, context};
    return DPA_Search(a, item, start, ce_compare_msvc, (CE_LPARAM)&call, options);
}
int CeMsvc_DSA_Search(CE_DSA *a, const void *item, int start, CE_COMPARE callback,
                      CE_LPARAM context, unsigned options)
{
    if (!callback)
        return -1;
    CE_MSCALL call = {(const void *)callback, context};
    return DSA_Search(a, item, start, ce_compare_msvc, (CE_LPARAM)&call, options);
}
#define CE_ENUM_WRAP(type, prefix, method)                                                         \
    void CeMsvc_##prefix##_##method(type *a, CE_ENUM callback, void *context)                      \
    {                                                                                              \
        CE_MSCALL call = {(const void *)callback, (CE_LPARAM)context};                             \
        prefix##_##method(a, callback ? ce_enum_msvc : 0, &call);                                  \
    }
CE_ENUM_WRAP(CE_DPA, DPA, EnumCallback)
CE_ENUM_WRAP(CE_DPA, DPA, DestroyCallback)
CE_ENUM_WRAP(CE_DSA, DSA, EnumCallback)
CE_ENUM_WRAP(CE_DSA, DSA, DestroyCallback)
