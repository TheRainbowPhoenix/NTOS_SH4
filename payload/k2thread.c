/* K2THREAD.EXE: Ke-owned second KTHREAD, no-FPU SH4AL-DSP.
 * Private direct-call API [67..70] only. One PID, 2 TIDs, 1000 alternations.
 * Expected 0x4B325431. Not a user-mode/preemptive or 2-EXE switch.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*create_fn)(u32,u32);
static volatile u32 main_count,peer_count,main_tid,peer_tid,error,finished;
static uint8_t peer_stack[4096] __attribute__((aligned(4096)));
static query_fn get_pid,get_tid,do_yield,do_exit,switch_count;
static void peer_entry(void) {
    for(u32 i=0u;i<1000u;++i) {
        if(get_tid()!=peer_tid || peer_count!=i ||
           main_count!=i || get_pid()==0u)error=0xBADAB001u;
        ++peer_count;
        if(do_yield()!=0u)error=0xBADAB002u;
    }
    finished=1u;
    (void)do_exit(); // Ke switches back to primary and never resumes us
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,70u))return (int)0xBADAB004u;
    get_pid=(query_fn)api[55];get_tid=(query_fn)api[56];
    create_fn create=(create_fn)api[67];
    do_yield=(query_fn)api[68];do_exit=(query_fn)api[69];
    switch_count=(query_fn)api[70];
    if(!get_pid || !get_tid || !create || !do_yield ||
       !do_exit || !switch_count)return (int)0xBADAB005u;
    main_tid=get_tid();
    if(!get_pid() || main_tid<0x10000u)return (int)0xBADAB006u;
    typedef int32_t (*hint_fn)(u32);
    hint_fn hint=(hint_fn)api[23];
    if(!hint || hint((u32)(uintptr_t)&peer_stack[0])!=0 ||
       hint((u32)(uintptr_t)&peer_stack[4095])!=0 ||
       hint((u32)(uintptr_t)&peer_entry)!=0)
        return (int)0xBADAB007u;
    peer_tid=create((u32)(uintptr_t)&peer_entry,
                    (u32)(uintptr_t)(peer_stack+sizeof(peer_stack)));
    if(peer_tid!=main_tid+1u)return (int)0xBADAB008u;
    for(u32 i=0u;i<1000u;++i) {
        if(main_count!=i || peer_count!=i || get_tid()!=main_tid)
            error=0xBADAB009u;
        if(do_yield()!=0u)error=0xBADAB00Au;
        if(peer_count!=i+1u || get_tid()!=main_tid)
            error=0xBADAB00Bu;
        ++main_count;
    }
    if(do_yield()!=0u)return (int)0xBADAB00Cu;
    if(error)return (int)error;
    if(!finished || main_count!=1000u || peer_count!=1000u ||
       get_tid()!=main_tid || switch_count()!=2002u)
        return (int)0xBADAB00Du;
    return (int)0x4B325431u;
}
