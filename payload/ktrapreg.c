/* KTRAPREG.EXE: privileged Ke service with real user VBR+0x100 RTE.
 * Bank0 r1-r7 and SH4AL-DSP X0/Y0/M0 survive 128 user trap frames.
 * Expected 0x4B545231 ("KTR1"). This is NOT timer preemption.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*arm_fn)(u32,u32);
typedef u32 (*get_fn)(void);
typedef int32_t (*hint_fn)(u32);
extern u32 ktrap_regs_probe(u32 pid,u32 seed);
extern void ktrap_regs_trapa(void);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,57u))
        return (int)0xBADDF101u;
    arm_fn arm=(arm_fn)api[54];
    get_fn pid=(get_fn)api[55],tid=(get_fn)api[56],
           svc_count=(get_fn)api[57];
    hint_fn hint=(hint_fn)api[23];
    if(!arm || !pid || !tid || !svc_count || !hint)
        return (int)0xBADDF102u;
    const u32 process=pid(),thread=tid(),before=svc_count();
    if(!process || !thread || before!=0u)
        return (int)0xBADDF103u;
    if(hint((u32)(uintptr_t)&ktrap_regs_trapa)!=0 ||
       hint((u32)(uintptr_t)&ktrap_regs_probe)!=0)
        return (int)0xBADDF104u;
    for(u32 i=0u;i<128u;++i) {
        if(arm((u32)(uintptr_t)&ktrap_regs_trapa,0x1000u)!=0u)
            return (int)0xBADDF105u;
        if(ktrap_regs_probe(process,0x00003100u+i)!=1u)
            return (int)0xBADDF106u;
        if(pid()!=process || tid()!=thread || svc_count()!=before+i+1u)
            return (int)0xBADDF107u;
    }
    return (int)0x4B545231u;
}
