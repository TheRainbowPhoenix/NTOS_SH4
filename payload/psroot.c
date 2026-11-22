/* PSROOT.EXE: physical P0 same-VA two-PA flip under verified
 * SH7305 UTLB+ITLB removal. Expected 0x50535231 ("PSR1").
 * A single KPROCESS for this isolated MMU acceptance gate.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*switch_fn)(u32);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,81u))return (int)0xBADC1001u;
    query_fn get_va=(query_fn)api[79],current=(query_fn)api[81];
    switch_fn switch_=(switch_fn)api[80];
    if(!get_va || !current || !switch_)return (int)0xBADC1002u;
    const u32 va=get_va();
    if(!va || (va&4095u) || current()!=0u)
        return (int)0xBADC1003u;
    volatile u32 *const page=(volatile u32 *)(uintptr_t)va;
    if(page[0]!=0u || page[1023]!=0u)return (int)0xBADC1004u;
    if(switch_(2u)!=0xC000000Du || current()!=0u)
        return (int)0xBADC1005u;
    for(u32 i=0u;i<500u;++i) {
        page[0]=0xA5000000u|i;
        page[1023]=0xD5000000u|i;
        if(switch_(1u)!=0u || current()!=1u)
            return (int)0xBADC1006u;
        if(page[0]!=(i==0u?0u:(0xC7000000u|(i-1u))) ||
           page[1023]!=(i==0u?0u:(0xE7000000u|(i-1u))))
            return (int)0xBADC1007u;
        page[0]=0xC7000000u|i;
        page[1023]=0xE7000000u|i;
        if(switch_(0u)!=0u || current()!=0u)
            return (int)0xBADC1008u;
        if(page[0]!=(0xA5000000u|i) ||
           page[1023]!=(0xD5000000u|i))
            return (int)0xBADC1009u;
    }
    if(switch_(1u)!=0u || page[0]!=(0xC7000000u|499u) ||
       page[1023]!=(0xE7000000u|499u))
        return (int)0xBADC100Au;
    if(switch_(0u)!=0u || page[0]!=(0xA5000000u|499u))
        return (int)0xBADC100Bu;
    return (int)0x50535231u;
}
