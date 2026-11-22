/* VMTEST.EXE: NT private reserve/commit/protect with 256KiB P1 arena.
 * Only dereference COMMITTED pages, prefault them explicitly.
 * Expected 0x564D0010, HINT=8, BAD=0, PF=0 on tested launcher. */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*reserve_fn)(u32,u32);
typedef u32 (*change_fn)(u32,u32,u32);
typedef u32 (*query_fn)(u32);
typedef u32 (*count_fn)(void);
typedef int32_t (*hint_fn)(u32);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,46u))return 0xBAD92001u;
    reserve_fn reserve=(reserve_fn)api[41];
    change_fn commit=(change_fn)api[42],protect=(change_fn)api[43];
    query_fn query=(query_fn)api[44],release=(query_fn)api[45];
    count_fn committed=(count_fn)api[46];
    hint_fn hint=(hint_fn)api[23];
    if(!reserve||!commit||!protect||!query||!release||!committed||!hint)
        return 0xBAD92002u;
    u32 base=reserve(0u,12u);
    if(!base || (base&4095u) || query(base)!=0u ||
       committed()!=0u)return 0xBAD92003u;
    if(commit(base,8u,2u)!=0u || committed()!=8u)
        return 0xBAD92004u;
    for(u32 i=0u;i<8u;++i) {
        u32 address=base+i*4096u;
        if(hint(address)!=0 || query(address)!=2u)
            return 0xBAD92005u;
        volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)address;
        p[0]=(uint8_t)(i^0x5Au);
        p[4095]=(uint8_t)(i^0xC3u);
    }
    if(protect(base,4u,1u)!=0u || query(base)!=1u ||
       query(base+16384u)!=2u)return 0xBAD92006u;
    for(u32 i=0u;i<8u;++i) {
        volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)
                             (base+i*4096u);
        if(p[0]!=(uint8_t)(i^0x5Au) ||
           p[4095]!=(uint8_t)(i^0xC3u))
            return 0xBAD92007u;
    }
    if(protect(base,4u,2u)!=0u || query(base)!=2u)
        return 0xBAD92008u;
    ((volatile uint8_t *)(uintptr_t)base)[0]=0x72u;
    if(((volatile uint8_t *)(uintptr_t)base)[0]!=0x72u)
        return 0xBAD92009u;
    if(protect(base+8u*4096u,1u,1u)==0u)
        return 0xBAD9200Au; // uncommitted
    if(release(base)==0u)return 0xBAD9200Bu; // live physical aliases
    if(committed()!=8u)return 0xBAD9200Cu;
    return 0x564D0010u; // release only by process teardown
}
