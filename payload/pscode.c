/* PSCODE.EXE: CODEA and CODEB independently linked import-free PE
 * images execute at exactly the SAME P0 VA, with different P1 text/data
 * backing and independent counters, 500 calls each. The kernel switches
 * both PE text/data plus PSROOT private page in one verified P4/IRQ gate.
 * Expected 0x50534344 ("PSCD"); BADCE1xx gives exact failure stage.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*switch_fn)(u32);
typedef int (*entry_fn)(const void *const *,char **,char **);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,84u))
        return (int)0xBADCE101u;
    query_fn get_va=(query_fn)api[79],current=(query_fn)api[81];
    query_fn entry=(query_fn)api[84],get_pid=(query_fn)api[55];
    switch_fn switch_=(switch_fn)api[80];
    if(!get_va || !current || !entry || !switch_ || !get_pid)
        return (int)0xBADCE102u;
    const u32 executable=entry();
    if(!executable || executable>=0x80000000u ||
       (executable&1u) || !get_va() || current()!=0u || !get_pid())
        return (int)0xBADCE103u;
    entry_fn invoke=(entry_fn)(uintptr_t)executable;
    volatile u32 *const page=(volatile u32 *)(uintptr_t)get_va();
    for(u32 i=0u;i<500u;++i) {
        if(current()!=0u || page[0]!=(i?0xA1100000u|(i-1u):0u))
            return (int)0xBADCE104u;
        if((u32)invoke(api,(char **)(uintptr_t)i,(char **)0)!=
           (0xA11A0000u|i))
            return (int)0xBADCE105u;
        page[0]=0xA1100000u|i;
        if(switch_(1u)!=0u || current()!=1u)
            return (int)0xBADCE106u;
        if(page[0]!=(i?0xB2200000u|(i-1u):0u))
            return (int)0xBADCE107u;
        if((u32)invoke(api,(char **)(uintptr_t)i,(char **)0)!=
           (0xB22B0000u|i))
            return (int)0xBADCE108u;
        page[0]=0xB2200000u|i;
        if(switch_(0u)!=0u || current()!=0u)
            return (int)0xBADCE109u;
        if(page[0]!=(0xA1100000u|i))
            return (int)0xBADCE10Au;
    }
    if(switch_(1u)!=0u || page[0]!=(0xB2200000u|499u))
        return (int)0xBADCE10Bu;
    if(switch_(0u)!=0u || page[0]!=(0xA1100000u|499u))
        return (int)0xBADCE10Cu;
    return (int)0x50534344u;
}
