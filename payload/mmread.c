/* MMREAD.EXE -- separately gated CP400 protection test for a denied P0 READ.
 * A private 4KiB P1 frame is committed and prefaulted while RW, then
 * reprotected as supervisor-only (SH4A PTEL.PR=01). A P0 user load
 * must raise EXPEVT=0x0A0 at VBR+0x100; the one-shot handler redirects
 * SPC to this function's recovery label without retrying the load.
 * Expected 0x52444101, PROT=1 REC=1 UNEXP=0, PF=0, BAD=0.
 * NEVER package with physical CP400 candidate firmware. */
#include <stdint.h>
#include "ntapi_caps.h"
#include "user_read_probe.h"
typedef uint32_t u32;
typedef u32 (*reserve_fn)(u32,u32);
typedef u32 (*change_fn)(u32,u32,u32);
typedef u32 (*arm_fn)(u32,u32,u32);
typedef u32 (*count_fn)(void);
typedef int32_t (*hint_fn)(u32);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,50u))return 0xBAD94001u;
    reserve_fn reserve=(reserve_fn)api[41];
    change_fn commit=(change_fn)api[42],protect=(change_fn)api[43];
    arm_fn arm=(arm_fn)api[47];
    count_fn seen=(count_fn)api[48],status=(count_fn)api[49],
             tea=(count_fn)api[50];
    hint_fn hint=(hint_fn)api[23];
    if(!reserve||!commit||!protect||!arm||!seen||!status||!tea||!hint)
        return 0xBAD94002u;
    u32 base=reserve(0u,1u);
    if(!base || commit(base,1u,2u)!=0u || hint(base)!=0)
        return 0xBAD94003u;
    volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)base;
    p[0]=0xA5u;
    if(protect(base,1u,3u)!=0u)return 0xBAD94004u;
    // NTOS's normal direct-call guest runs privileged: PR=01 permits its
    // reads. The isolated stackless P0 user stub *really* sets MD=0,
    // executes one read, then TRAPA #0x7b restores the privileged SR.
    const u32 recovery=(u32)(uintptr_t)&ntos_user_probe_trapa;
    if(arm(base,0x0A0u,recovery)!=0u)
        return 0xBAD94005u;
    ntos_user_probe_stub(base);
    if(seen()!=1u)return 0xBAD94006u; // no real 0x0a0 occurred
    if(status()!=0xC0000005u || tea()!=base)
        return 0xBAD94007u;
    if(protect(base,1u,2u)!=0u || p[0]!=0xA5u)
        return 0xBAD94008u;
    return 0x52444101u;
}
