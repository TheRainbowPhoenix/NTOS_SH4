/* FAULTCAN.EXE -- EMULATOR ONLY, never include in physical CP400 kit.
 * This intentionally makes ONE unhinted P0 write to registered RAM-backed
 * .bss and requires a genuine VBR+0x400 write-miss exception EXPEVT=0x060.
 * If the vector trampoline is broken it can HANG or RESET hardware.
 * The kernel [39] gate must be compiled in for this to proceed.
 * Expected 0xFACC0001 with VBR-CANARY seen=1 and PF>=1, BAD=0. */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef int32_t (*arm_fn)(u32,u32);
typedef u32 (*seen_fn)(void);
static volatile uint8_t pages[80u*4096u] __attribute__((aligned(4096)));
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,40u) || !api[39] || !api[40])
        return 0xBAD80001u; // NEVER trigger a fault on an older firmware
    arm_fn arm=(arm_fn)api[39];
    seen_fn seen=(seen_fn)api[40];
    volatile uint8_t *p=pages+79u*4096u;
    if(arm((u32)p,0x60u)!=0)return 0xBAD80002u;
    // This store, by design, cannot be prefaulted with a page hint.
    *p=0x37u;
    if(*p!=0x37u)return 0xBAD80003u;
    if(seen()!=1u)return 0xBAD80004u;
    return 0xFACC0001u;
}
