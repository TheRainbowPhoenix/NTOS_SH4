/* KREGCTX.EXE: two Ke KTHREADs, SH4AL-DSP X0/Y0/M0 and
 * r8-r14 register canaries across 500 yields each. No FPU instructions.
 * Expected 0x4B524731 ("KRG1"). Not timer/IRQ preemption.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*create_fn)(u32,u32);
typedef int32_t (*hint_fn)(u32);
extern u32 kreg_yield_checked(query_fn do_yield,u32 seed);
static uint8_t peer_stack[4096] __attribute__((aligned(4096)));
static volatile u32 parent_count,child_count,error,finished,main_tid,peer_tid;
static query_fn yield_,exit_,get_tid,swaps;
static void child_entry(void) {
    for(u32 i=0u;i<500u;++i) {
        if(parent_count!=i || child_count!=i ||
           get_tid()!=peer_tid)error=0xBADDC101u;
        ++child_count;
        if(kreg_yield_checked(yield_,0x00004310u+i)!=1u)
            error=0xBADDC102u;
    }
    finished=1u;
    (void)exit_();
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,70u))return (int)0xBADDC103u;
    create_fn create=(create_fn)api[67];
    hint_fn hint=(hint_fn)api[23];
    yield_=(query_fn)api[68];exit_=(query_fn)api[69];
    get_tid=(query_fn)api[56];swaps=(query_fn)api[70];
    if(!create || !hint || !yield_ || !exit_ || !get_tid || !swaps)
        return (int)0xBADDC104u;
    main_tid=get_tid();
    if(hint((u32)(uintptr_t)&peer_stack[0])!=0 ||
       hint((u32)(uintptr_t)&peer_stack[4095])!=0 ||
       hint((u32)(uintptr_t)&child_entry)!=0 ||
       hint((u32)(uintptr_t)&kreg_yield_checked)!=0)
        return (int)0xBADDC105u;
    peer_tid=create((u32)(uintptr_t)&child_entry,
                    (u32)(uintptr_t)(peer_stack+sizeof(peer_stack)));
    if(peer_tid!=main_tid+1u)return (int)0xBADDC106u;
    for(u32 i=0u;i<500u;++i) {
        if(parent_count!=i || child_count!=i || get_tid()!=main_tid)
            return (int)0xBADDC107u;
        if(kreg_yield_checked(yield_,0x00005010u+i)!=1u)
            return (int)0xBADDC108u;
        ++parent_count;
        if(child_count!=i+1u)return (int)0xBADDC109u;
    }
    if(yield_()!=0u || error || !finished || get_tid()!=main_tid ||
       swaps()!=1002u || child_count!=500u || parent_count!=500u)
        return (int)(error?error:0xBADDC10Au);
    return (int)0x4B524731u;
}
