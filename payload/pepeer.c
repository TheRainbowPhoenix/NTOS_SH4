/* PEPEER.EXE: separately linked PE image, co-resident with PEPAIR.
 * Its own .text/.data live at distinct P0 VAs; no WinCE DLL imports.
 * Every one of 250 cooperative yields increments kernel mailbox.
 * Internal return 0x54574331. NOT yet a separate KPROCESS/ASID.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
static volatile u32 local_counter;
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,78u))return (int)0xBADB1001u;
    query_fn yield_=(query_fn)api[68],tick=(query_fn)api[77];
    query_fn pid=(query_fn)api[55],tid=(query_fn)api[56];
    if(!yield_ || !tick || !pid || !tid || !pid() || !tid())
        return (int)0xBADB1002u;
    for(u32 i=0u;i<250u;++i) {
        if(local_counter++!=i || tick()!=i+1u)
            return (int)0xBADB1003u;
        if(yield_()!=0u)return (int)0xBADB1004u;
    }
    if(local_counter!=250u)return (int)0xBADB1005u;
    return (int)0x54574331u;
}
