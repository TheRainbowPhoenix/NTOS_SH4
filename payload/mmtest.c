/* MMTEST.EXE: explicitly hinted 96-page RAM-backed UTLB replacement.
 * No deliberate P0 fault or illegal write to a protected mapping.
 * Expect 0x4D4D0060, HINT=192, EV>0, BAD=0 and normally PF=0. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t (*hint_fn)(u32);
static volatile uint8_t pages[96u*4096u] __attribute__((aligned(4096)));
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api || !api[23])return 0xBAD50001u;
    hint_fn hint=(hint_fn)api[23];
    // Reject an unmapped virtual page without actually dereferencing it.
    if(hint(0x70000000u)!=-2)return 0xBAD50002u;
    for(u32 i=0u;i<96u;++i) {
        volatile uint8_t *p=pages+i*4096u;
        if(hint((u32)p)!=0)return 0xBAD50003u;
        p[0]=(uint8_t)(i^0xA5u);
        p[4095]=(uint8_t)(i^0x5Au);
    }
    for(u32 i=96u;i>0u;--i) {
        u32 j=i-1u;
        volatile uint8_t *p=pages+j*4096u;
        if(hint((u32)p)!=0)return 0xBAD50004u;
        if(p[0]!=(uint8_t)(j^0xA5u) ||
           p[4095]!=(uint8_t)(j^0x5Au))
            return 0xBAD50005u;
    }
    return 0x4D4D0060u;
}
