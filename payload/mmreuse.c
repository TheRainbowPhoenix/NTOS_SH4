/* MMREUSE.EXE: opt-in real CP400 associative UTLB/ITLB invalidation.
 * Tests NtFreeVirtualMemory(MEM_DECOMMIT), recommit and clean frames.
 * Expected 0x4D444331; no address fault, BAD=0. Backup old HH3 first.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*alloc_fn)(u32,u32 *,u32,u32 *,u32,u32);
typedef u32 (*free_fn)(u32,u32 *,u32 *,u32);
typedef int32_t (*hint_fn)(u32);
typedef u32 (*query_fn)(u32);
typedef u32 (*count_fn)(void);
static volatile u32 vm_base __attribute__((aligned(4096)));
static volatile u32 vm_bytes, vm_saved;
enum { CURRENT=0xFFFFFFFFu, RESERVE=0x2000u, COMMIT=0x1000u,
       DECOMMIT=0x4000u, RELEASE=0x8000u, PAGE_RW=4u };
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,53u))
        return (int)0xBAD9E001u;
    alloc_fn allocate=(alloc_fn)api[51];
    free_fn free_=(free_fn)api[53];
    hint_fn hint=(hint_fn)api[23];
    query_fn query=(query_fn)api[44];
    count_fn count=(count_fn)api[46];
    if(!allocate||!free_||!hint||!query||!count)
        return (int)0xBAD9E002u;
    if(hint((u32)(uintptr_t)&vm_base)!=0 ||
       hint((u32)(uintptr_t)&vm_bytes)!=0)
        return (int)0xBAD9E003u;
    u32 *base=(u32 *)(uintptr_t)&vm_base;
    u32 *size=(u32 *)(uintptr_t)&vm_bytes;
    vm_base=0u;vm_bytes=8192u;
    if(allocate(CURRENT,base,0u,size,RESERVE|COMMIT,PAGE_RW)!=0u ||
       !vm_base || count()!=2u)
        return (int)0xBAD9E004u;
    vm_saved=vm_base;
    if(hint(vm_base)!=0 || hint(vm_base+4096u)!=0)
        return (int)0xBAD9E005u;
    volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)vm_base;
    p[0]=0xA5u;p[4095]=0xC7u;p[4096]=0x5Au;
    vm_bytes=0u;
    if(free_(CURRENT,base,size,RELEASE)!=0xC00000E8u)
        return (int)0xBAD9E006u;
    vm_bytes=4096u;
    if(free_(CURRENT,base,size,DECOMMIT)!=0u ||
       vm_base!=vm_saved || vm_bytes!=4096u ||
       query(vm_base)!=0u || count()!=1u)
        return (int)0xBAD9E007u;
    vm_bytes=4096u;
    if(allocate(CURRENT,base,0u,size,COMMIT,PAGE_RW)!=0u ||
       hint(vm_base)!=0 || count()!=2u)
        return (int)0xBAD9E008u;
    if(p[0]!=0u || p[4095]!=0u || p[4096]!=0x5Au)
        return (int)0xBAD9E009u;
    p[0]=0x39u;
    vm_bytes=8192u;
    if(free_(CURRENT,base,size,DECOMMIT)!=0u ||
       count()!=0u || query(vm_saved)!=0u)
        return (int)0xBAD9E00Au;
    vm_bytes=0u;
    if(free_(CURRENT,base,size,RELEASE)!=0u || vm_base!=0u)
        return (int)0xBAD9E00Bu;
    vm_bytes=4096u;
    if(allocate(CURRENT,base,0u,size,RESERVE|COMMIT,PAGE_RW)!=0u ||
       vm_base!=vm_saved || hint(vm_base)!=0)
        return (int)0xBAD9E00Cu;
    p=(volatile uint8_t *)(uintptr_t)vm_base;
    if(p[0]!=0u || p[4095]!=0u)
        return (int)0xBAD9E00Du;
    return (int)0x4D444331u;
}
