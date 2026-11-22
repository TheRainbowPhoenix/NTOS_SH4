/* MMSTRESS.EXE: 500 cycles of real hardware P0 cache/TLB reuse.
 * Device gate: ONLY after MMREUSE v2 == 0x4D444331.
 * Still one foreground KTHREAD; two-thread stress remains future work.
 * Expected 0x4D535431.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*alloc_fn)(u32,u32 *,u32,u32 *,u32,u32);
typedef u32 (*free_fn)(u32,u32 *,u32 *,u32);
typedef int32_t (*hint_fn)(u32);
typedef u32 (*count_fn)(void);
static volatile u32 base __attribute__((aligned(4096)));
static volatile u32 bytes;
enum { CURRENT=0xFFFFFFFFu,COMMIT=0x1000u,RESERVE=0x2000u,
       DECOMMIT=0x4000u,RELEASE=0x8000u,RW=4u };
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,53u))return (int)0xBADAA001u;
    alloc_fn alloc=(alloc_fn)api[51];
    free_fn free_=(free_fn)api[53];
    hint_fn hint=(hint_fn)api[23];
    count_fn count=(count_fn)api[46];
    if(!alloc || !free_ || !hint || !count)return (int)0xBADAA002u;
    if(hint((u32)(uintptr_t)&base)!=0 ||
       hint((u32)(uintptr_t)&bytes)!=0)return (int)0xBADAA003u;
    u32 *pb=(u32 *)(uintptr_t)&base;
    u32 *sz=(u32 *)(uintptr_t)&bytes;
    base=0u;bytes=8192u;
    if(alloc(CURRENT,pb,0u,sz,RESERVE|COMMIT,RW)!=0u ||
       count()!=2u)return (int)0xBADAA004u;
    if(hint(base)!=0 || hint(base+4096u)!=0)
        return (int)0xBADAA005u;
    volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)base;
    p[4096]=0x5Au;p[8191]=0xC3u;
    for(u32 i=0u;i<500u;++i) {
        const uint8_t v=(uint8_t)((i*37u+11u)&255u);
        p[0]=v;p[4095]=(uint8_t)~v;
        bytes=4096u;
        if(free_(CURRENT,pb,sz,DECOMMIT)!=0u ||
           count()!=1u)return (int)0xBADAA006u;
        bytes=4096u;
        if(alloc(CURRENT,pb,0u,sz,COMMIT,RW)!=0u ||
           hint(base)!=0 || count()!=2u)
            return (int)0xBADAA007u;
        if(p[0]!=0u || p[4095]!=0u)
            return (int)0xBADAA008u;
        if(p[4096]!=0x5Au || p[8191]!=0xC3u)
            return (int)0xBADAA009u;
    }
    bytes=8192u;
    if(free_(CURRENT,pb,sz,DECOMMIT)!=0u ||
       count()!=0u)return (int)0xBADAA00Au;
    bytes=0u;
    if(free_(CURRENT,pb,sz,RELEASE)!=0u || base!=0u)
        return (int)0xBADAA00Bu;
    return (int)0x4D535431u;
}
