/* MMGUARD.EXE is **EMULATOR ONLY**. It intentionally attempts
 * ONE forbidden P0 store to read-only PE .rodata. VBR+0x100 must catch
 * EXPEVT=0x0C0, install a validated recovery SPC, and return to main.
 * Expected 0x47524401, PROT=1 REC=1 UNEXP=0, BAD=0.
 * This payload must never be bundled in normal physical CP400 firmware. */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*arm_fn)(u32,u32,u32);
typedef u32 (*query_fn)(void);
typedef int32_t (*hint_fn)(u32);
static const volatile uint8_t readonly[4096u]
    __attribute__((aligned(4096))) = {[0]=0xA5u,[4095]=0x7Du};
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,50u))
        return 0xBAD93001u;
    arm_fn arm=(arm_fn)api[47];
    query_fn seen=(query_fn)api[48],status=(query_fn)api[49],
             tea=(query_fn)api[50];
    hint_fn hint=(hint_fn)api[23];
    if(!arm||!seen||!status||!tea||!hint)return 0xBAD93002u;
    u32 target=(u32)(uintptr_t)&readonly[0];
    if(hint(target)!=0)return 0xBAD93003u;
    // GNU C label-address extension: recovery is in THIS function, so the
    // original P0 stack/PR are still valid after RTE rewrites SPC.
    void *recovery=&&recovered;
    __asm__ volatile("" : : "r"(recovery) : "memory");
    if(arm(target,0x0C0u,(u32)(uintptr_t)recovery)!=0u)
        return 0xBAD93004u;
    *(volatile uint8_t *)(uintptr_t)target=0x37u; // deliberate RO violation
    return 0xBAD93005u; // means the protection fault was NOT trapped
recovered:
    if(seen()!=1u || status()!=0xC0000005u || tea()!=target)
        return 0xBAD93006u;
    if(readonly[0]!=0xA5u || readonly[4095]!=0x7Du)
        return 0xBAD93007u;
    return 0x47524401u;
}
