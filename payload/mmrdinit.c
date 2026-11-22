/* MMRDINIT.EXE -- HIGH-RISK protected-read first-mapping probe.
 * Diagnostic pair for MMREAD.EXE, which uses live RW->supervisor PR change.
 * This installs PR=01 on its first UTLB fill; no stale old PR can survive.
 * Expected 0x52444931, PROT=1 REC=1 UNEXP=0, HINT=1, BAD=0.
 * Use only the matching opt-in CP400 VBR candidate; stop at any reset. */
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
    if(!api || !ntos_api_has_slot(window,50u))return 0xBAD96001u;
    reserve_fn reserve=(reserve_fn)api[41];
    change_fn commit=(change_fn)api[42],protect=(change_fn)api[43];
    arm_fn arm=(arm_fn)api[47];
    count_fn seen=(count_fn)api[48],status=(count_fn)api[49],
             tea=(count_fn)api[50];
    hint_fn hint=(hint_fn)api[23];
    if(!reserve||!commit||!protect||!arm||!seen||!status||!tea||!hint)
        return 0xBAD96002u;
    u32 base=reserve(0u,1u);
    if(!base || commit(base,1u,3u)!=0u || hint(base)!=0)
        return 0xBAD96003u;
    const u32 recovery=(u32)(uintptr_t)&ntos_user_probe_trapa;
    if(arm(base,0x0A0u,recovery)!=0u)
        return 0xBAD96004u;
    ntos_user_probe_stub(base);
    if(seen()!=1u)return 0xBAD96005u; // no genuine user-mode 0x0a0
    if(status()!=0xC0000005u || tea()!=base)
        return 0xBAD96006u;
    if(protect(base,1u,2u)!=0u ||
       ((volatile uint8_t *)(uintptr_t)base)[0]!=0u)
        return 0xBAD96007u;
    return 0x52444931u;
}
