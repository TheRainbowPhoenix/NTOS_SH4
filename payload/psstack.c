/* PSSTACK.EXE: two ROOT pages backing the same P0 virtual SP.
 * Actual SH4 r15 switches into P0 stack and back, 500 times per root.
 * Expected 0x5053544B ('PSTK'), distinct failure 0xBADF62xx.
 * Parent/root IDs here are test roots, NOT two user KPROCESSes.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*get_fn)(void);
typedef u32 (*switch_fn)(u32);
typedef int32_t (*hint_fn)(u32);
extern u32 psstack_probe(u32 seed,u32 p0_stack_top);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,81u))
        return (int)0xBADF6201u;
    get_fn get_va=(get_fn)api[79],current=(get_fn)api[81];
    switch_fn sw=(switch_fn)api[80];
    hint_fn hint=(hint_fn)api[23];
    if(!get_va || !current || !sw || !hint)
        return (int)0xBADF6202u;
    const u32 va=get_va();
    if(!va || (va&4095u) || current()!=0u)
        return (int)0xBADF6203u;
    volatile u32 *const page=(volatile u32 *)(uintptr_t)va;
    const u32 stack_top=va+4096u;
    if(hint((u32)(uintptr_t)&psstack_probe)!=0)
        return (int)0xBADF6204u;
    for(u32 i=0u;i<500u;++i) {
        if(current()!=0u ||
           page[0]!=(i?0xA0000000u|(i-1u):0u) ||
           page[1023]!=(i?0xA11A0000u|(i-1u):0u))
            return (int)0xBADF6205u;
        const u32 a=0xA11A0000u|i;
        if(psstack_probe(a,stack_top)!=a || page[1023]!=a)
            return (int)0xBADF6206u;
        page[0]=0xA0000000u|i;
        if(sw(1u)!=0u || current()!=1u)
            return (int)0xBADF6207u;
        if(page[0]!=(i?0xB0000000u|(i-1u):0u) ||
           page[1023]!=(i?0xB22B0000u|(i-1u):0u))
            return (int)0xBADF6208u;
        const u32 b=0xB22B0000u|i;
        if(psstack_probe(b,stack_top)!=b || page[1023]!=b)
            return (int)0xBADF6209u;
        page[0]=0xB0000000u|i;
        if(sw(0u)!=0u || current()!=0u)
            return (int)0xBADF620Au;
    }
    if(page[1023]!=(0xA11A0000u|499u) ||
       sw(1u)!=0u || page[1023]!=(0xB22B0000u|499u))
        return (int)0xBADF620Bu;
    if(sw(0u)!=0u || current()!=0u)
        return (int)0xBADF620Cu;
    return (int)0x5053544Bu;
}
