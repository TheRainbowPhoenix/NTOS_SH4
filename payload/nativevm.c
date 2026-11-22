/* NATIVEVM.EXE: first bounded NT Native ABI proof on a real fx-CP400.
 * Three Nt-like calls work on the current process pseudo handle (-1):
 *   NtAllocateVirtualMemory, NtProtectVirtualMemory, NtFreeVirtualMemory.
 * In/out arguments MUST reside in writable, resident guest P0 memory,
 * never in the host's P1 stack (the kernel checks this explicitly).
 * Expected 0x4E560010, HINT=5, BAD=0, no spontaneous VBR faults.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef int32_t (*hint_fn)(u32);
typedef u32 (*query_fn)(u32);
typedef u32 (*alloc_fn)(u32,u32 *,u32,u32 *,u32,u32);
typedef u32 (*protect_fn)(u32,u32 *,u32 *,u32,u32 *);
typedef u32 (*free_fn)(u32,u32 *,u32 *,u32);
static volatile u32 native_base __attribute__((aligned(4096)));
static volatile u32 native_size;
static volatile u32 native_old;
enum { CURRENT=0xFFFFFFFFu, MEM_COMMIT=0x1000u,
       MEM_RESERVE=0x2000u,MEM_RELEASE=0x8000u,
       PAGE_RO=2u,PAGE_RW=4u };
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,53u))
        return (int)0xBAD97001u;
    alloc_fn alloc=(alloc_fn)api[51];
    protect_fn protect=(protect_fn)api[52];
    free_fn release=(free_fn)api[53];
    hint_fn hint=(hint_fn)api[23];
    query_fn query=(query_fn)api[44];
    if(!alloc || !protect || !release || !hint || !query)
        return (int)0xBAD97002u;

    // Probe only the guest-owned globals, not a P1 stack pointer.
    if(hint((u32)(uintptr_t)&native_base)!=0 ||
       hint((u32)(uintptr_t)&native_size)!=0 ||
       hint((u32)(uintptr_t)&native_old)!=0)
        return (int)0xBAD97003u;
    u32 *base=(u32 *)(uintptr_t)&native_base;
    u32 *size=(u32 *)(uintptr_t)&native_size;
    u32 *old=(u32 *)(uintptr_t)&native_old;
    native_base=0u;native_size=8192u;native_old=0u;
    if(alloc(CURRENT,base,0u,size,MEM_RESERVE|MEM_COMMIT,
             PAGE_RW)!=0u || !native_base || native_size!=8192u)
        return (int)0xBAD97004u;

    for(u32 i=0u;i<2u;++i) {
        const u32 addr=native_base+i*4096u;
        if(hint(addr)!=0 || query(addr)!=2u)
            return (int)0xBAD97005u;
        volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)addr;
        p[0]=(uint8_t)(i+0x51u);
        p[4095]=(uint8_t)(i+0x71u);
    }
    native_size=4096u;
    if(protect(CURRENT,base,size,PAGE_RO,old)!=0u ||
       native_old!=PAGE_RW || query(native_base)!=1u)
        return (int)0xBAD97006u;
    if(((volatile uint8_t *)(uintptr_t)native_base)[0]!=0x51u)
        return (int)0xBAD97007u;
    if(protect(CURRENT,base,size,PAGE_RW,old)!=0u ||
       native_old!=PAGE_RO || query(native_base)!=2u)
        return (int)0xBAD97008u;

    // Fail-closed: committed live frames are not released, even if
    // the caller requests MEM_RELEASE and a zero byte count.
    native_size=0u;
    if(release(CURRENT,base,size,MEM_RELEASE)!=0xC00000E8u ||
       native_base==0u)
        return (int)0xBAD97009u;

    // A reserved-only region CAN be released, with no stale TLB aliases.
    native_base=0u;native_size=4096u;
    if(alloc(CURRENT,base,0u,size,MEM_RESERVE,PAGE_RO)!=0u ||
       !native_base || native_size!=4096u)
        return (int)0xBAD9700Au;
    native_size=0u;
    if(release(CURRENT,base,size,MEM_RELEASE)!=0u ||
       native_base!=0u)return (int)0xBAD9700Bu;

    // Attempt to pass a P1/host-stack out parameter: must be rejected
    // WITHOUT ever dereferencing it or violating flash ownership.
    u32 invalid_stack=0u;
    native_base=0u;native_size=4096u;
    if(alloc(CURRENT,&invalid_stack,0u,size,MEM_RESERVE,PAGE_RW)
       !=0xC000000Du)
        return (int)0xBAD9700Cu;
    return (int)0x4E560010u;
}
