/* MMROTEST.EXE: actual protected firmware maps .rodata RO and .bss RW.
 * Non-destructive: reads the RO page, writes ONLY its own writable RAM.
 * Expected 0x4D520010; this does NOT attempt a write-protection fault. */
#include <stdint.h>
typedef uint32_t u32;
typedef u32 (*rights_fn)(u32);
typedef int32_t (*hint_fn)(u32);
static const volatile uint8_t ro[8192u] __attribute__((aligned(4096))) = {
    [0]=0xA5u, [4096]=0x5Au, [8191]=0x7Du
};
static volatile uint8_t rw[8192u] __attribute__((aligned(4096)));
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api || !api[23] || !api[38])return 0xBAD70001u;
    rights_fn rights=(rights_fn)api[38];
    hint_fn hint=(hint_fn)api[23];
    if(rights((u32)&ro[0])!=1u || rights((u32)&ro[4096])!=1u)
        return 0xBAD70002u;
    if(rights((u32)&rw[0])!=2u || rights((u32)&rw[4096])!=2u)
        return 0xBAD70003u;
    if(rights(0x70000000u)!=0u)return 0xBAD70004u;
    if(hint((u32)&ro[0])!=0 || hint((u32)&ro[4096])!=0 ||
       hint((u32)&rw[0])!=0 || hint((u32)&rw[4096])!=0)
        return 0xBAD70005u;
    if(ro[0]!=0xA5u || ro[4096]!=0x5Au || ro[8191]!=0x7Du)
        return 0xBAD70006u;
    rw[0]=0x51u;rw[4096]=0x63u;rw[8191]=0x77u;
    if(rw[0]!=0x51u || rw[4096]!=0x63u || rw[8191]!=0x77u)
        return 0xBAD70007u;
    return 0x4D520010u;
}
