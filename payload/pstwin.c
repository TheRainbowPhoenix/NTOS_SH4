/* PSTWIN.EXE: two independent PE EXEs, each Ke cooperative KTHREAD has
 * distinct Ps process identity and a different PA at the same P0 data VA.
 * Code and stacks are still co-resident at disjoint VAs: NOT the final
 * full code-root KPROCESS isolation gate. Expected 0x50535431.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*create_fn)(u32,u32);
typedef int32_t (*hint_fn)(u32);
typedef int (*entry_fn)(const void *const *,char **,char **);
static uint8_t child_stack[4096] __attribute__((aligned(4096)));
static volatile u32 child_result,parent_pid,parent_tid,child_tid;
static volatile u32 parent_device,device_bytes;
static const char device_name[]="\\Device\\Loopback";
static const void *const *saved_api;
static query_fn yield_,exit_,root_va,root_current,get_pid,get_tid;
static query_fn child_pid,progress,switches,parent_of,space_id;
static void run_child(void) {
    if(root_current()!=1u || get_pid()!=child_pid() ||
       get_pid()==parent_pid || parent_of()!=parent_pid ||
       space_id()!=get_pid() || get_tid()!=child_tid)
        child_result=0xBADD2031u;
    else {
        entry_fn child=(entry_fn)(uintptr_t)((query_fn)saved_api[76])();
        child_result=(u32)child(saved_api,(char **)(uintptr_t)parent_device,
                                (char **)0);
    }
    (void)exit_(); // switches physical root back before Ke stack swap
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,82u))return (int)0xBADD2001u;
    saved_api=api;
    create_fn create=(create_fn)api[67];
    hint_fn hint=(hint_fn)api[23];
    query_fn peer_entry=(query_fn)api[76];
    root_va=(query_fn)api[79];root_current=(query_fn)api[81];
    child_pid=(query_fn)api[82];progress=(query_fn)api[78];
    get_pid=(query_fn)api[55];get_tid=(query_fn)api[56];
    parent_of=(query_fn)api[66];space_id=(query_fn)api[64];
    yield_=(query_fn)api[68];exit_=(query_fn)api[69];
    switches=(query_fn)api[70];
    u32 (*open_)(const char *,u32 *)=(u32 (*)(const char *,u32 *))api[71];
    u32 (*close_)(u32)=(u32 (*)(u32))api[74];
    if(!create || !hint || !peer_entry || !root_va ||
       !root_current || !child_pid || !progress || !get_pid ||
       !get_tid || !parent_of || !space_id || !yield_ || !exit_ ||
       !switches || !open_ || !close_)
        return (int)0xBADD2002u;
    parent_pid=get_pid();parent_tid=get_tid();
    if(!parent_pid || !parent_tid || child_pid()!=0u ||
       !peer_entry() || !root_va() || root_current()!=0u)
        return (int)0xBADD2003u;
    if(hint((u32)(uintptr_t)child_stack)!=0 ||
       hint((u32)(uintptr_t)&child_stack[4095])!=0 ||
       hint((u32)(uintptr_t)&run_child)!=0 ||
       hint((u32)(uintptr_t)&parent_device)!=0 ||
       hint((u32)(uintptr_t)device_name)!=0)
        return (int)0xBADD2004u;
    if(open_(device_name,(u32 *)(uintptr_t)&parent_device)!=0u ||
       !parent_device)return (int)0xBADD2005u;
    volatile u32 *const page=(volatile u32 *)(uintptr_t)root_va();
    child_tid=create((u32)(uintptr_t)&run_child,
                     (u32)(uintptr_t)(child_stack+sizeof(child_stack)));
    if(child_tid!=parent_tid+1u || child_pid()==parent_pid ||
       !child_pid())return (int)0xBADD2006u;
    for(u32 i=0u;i<500u;++i) {
        if(root_current()!=0u || get_pid()!=parent_pid ||
           space_id()!=parent_pid || get_tid()!=parent_tid)
            return (int)0xBADD2007u;
        page[0]=0xA5000000u|i;
        page[1023]=0xD5000000u|i;
        if(yield_()!=0u)return (int)0xBADD2008u;
        if(progress()!=i+1u || root_current()!=0u ||
           page[0]!=(0xA5000000u|i) ||
           page[1023]!=(0xD5000000u|i))
            return (int)0xBADD2009u;
    }
    if(yield_()!=0u)return (int)0xBADD200Au;
    if(child_result!=0x50534332u || root_current()!=0u ||
       switches()!=1002u || page[0]!=(0xA5000000u|499u))
        return (int)0xBADD200Bu;
    if(close_(parent_device)!=0u)return (int)0xBADD200Cu;
    return (int)0x50535431u;
}
