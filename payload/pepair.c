/* PEPAIR.EXE: two physically co-resident, independently linked SH4BE EXEs.
 * PEPEER is preloaded by stage0, not a DLL or serial NtLaunch.
 * Both share one KPROCESS for now; KTHREADs alternate 250 times.
 * Expected 0x54575031 (child internal 0x54574331).
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef int32_t (*hint_fn)(u32);
typedef u32 (*create_fn)(u32,u32);
typedef int (*entry_fn)(const void *const *,char **,char **);
static uint8_t worker_stack[4096] __attribute__((aligned(4096)));
static volatile u32 status,initial_pid,parent_tid,peer_tid;
static const void *const *saved_api;
static query_fn yield_,exit_,peer_address,peer_progress,get_pid,get_tid,switches;
static void runner(void) {
    const u32 target=peer_address();
    if(!target || get_pid()!=initial_pid ||
       get_tid()!=peer_tid)status=0xBADB0031u;
    else {
        entry_fn child=(entry_fn)(uintptr_t)target;
        status=(u32)child(saved_api,(char **)0,(char **)0);
    }
    (void)exit_();
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,78u))return (int)0xBADB0001u;
    saved_api=api;
    create_fn create=(create_fn)api[67];
    yield_=(query_fn)api[68];exit_=(query_fn)api[69];
    switches=(query_fn)api[70];
    peer_address=(query_fn)api[76];
    peer_progress=(query_fn)api[78];
    get_pid=(query_fn)api[55];get_tid=(query_fn)api[56];
    hint_fn hint=(hint_fn)api[23];
    if(!create || !yield_ || !exit_ || !peer_address ||
       !peer_progress || !get_pid || !get_tid || !hint || !switches)
        return (int)0xBADB0002u;
    initial_pid=get_pid();parent_tid=get_tid();
    if(!initial_pid || !parent_tid || !peer_address())
        return (int)0xBADB0003u;
    if(hint((u32)(uintptr_t)&worker_stack[0])!=0u ||
       hint((u32)(uintptr_t)&worker_stack[4095])!=0u ||
       hint((u32)(uintptr_t)&runner)!=0u)
        return (int)0xBADB0004u;
    peer_tid=create((u32)(uintptr_t)&runner,
                    (u32)(uintptr_t)(worker_stack+sizeof(worker_stack)));
    if(peer_tid!=parent_tid+1u)return (int)0xBADB0005u;
    for(u32 i=0u;i<250u;++i) {
        if(yield_()!=0u)return (int)0xBADB0006u;
        if(peer_progress()!=i+1u || get_tid()!=parent_tid ||
           get_pid()!=initial_pid)
            return (int)0xBADB0007u;
    }
    if(yield_()!=0u)return (int)0xBADB0008u;
    if(status!=0x54574331u || switches()!=502u)
        return (int)0xBADB0009u;
    return (int)0x54575031u;
}
