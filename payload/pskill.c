/* PSKILL.EXE: parent KPROCESS keeps its own NT Io device while a
 * distinct child PID leaks a second handle and exits. Ke must reclaim
 * only child's FILE_OBJECT, retain parent's, then restore host picker.
 * Expected 0x50534B31 ("PSK1"). This is cooperative child lifetime,
 * NOT independent process stacks/VM/reaper yet.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*create_fn)(u32,u32);
typedef int32_t (*hint_fn)(u32);
typedef int (*entry_fn)(const void *const *,char **,char **);
static uint8_t peer_stack[4096] __attribute__((aligned(4096)));
static volatile u32 child_result,parent_pid,parent_tid,child_tid,parent_handle;
static const char parent_device[]="\\Device\\Loopback";
static const void *const *saved_api;
static query_fn child_exit,peer_yield,live,root_current,child_pid,progress;
static query_fn current_pid,current_tid,parent_of,space_id,ob_handles;
static void child_entry(void) {
    entry_fn child=(entry_fn)(uintptr_t)((query_fn)saved_api[76])();
    child_result=(u32)child(saved_api,(char **)(uintptr_t)parent_pid,
                            (char **)0);
    (void)child_exit();
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,88u))
        return (int)0xBADF5201u;
    saved_api=api;
    create_fn create=(create_fn)api[67];
    hint_fn hint=(hint_fn)api[23];
    query_fn peer_entry=(query_fn)api[76];
    child_exit=(query_fn)api[69];
    peer_yield=(query_fn)api[68];
    root_current=(query_fn)api[81];
    child_pid=(query_fn)api[82];
    progress=(query_fn)api[78];
    live=(query_fn)api[88];
    current_pid=(query_fn)api[55];
    current_tid=(query_fn)api[56];
    parent_of=(query_fn)api[66];
    space_id=(query_fn)api[64];
    ob_handles=(query_fn)api[37];
    u32 (*open_)(const char *,u32 *)=(u32 (*)(const char *,u32 *))api[71];
    u32 (*close_)(u32)=(u32 (*)(u32))api[74];
    if(!create || !hint || !peer_entry || !child_exit ||
       !peer_yield || !root_current || !child_pid || !progress ||
       !live || !current_pid || !current_tid || !parent_of ||
       !space_id || !ob_handles || !open_ || !close_)
        return (int)0xBADF5202u;
    parent_pid=current_pid();parent_tid=current_tid();
    if(!parent_pid || !parent_tid || child_pid()!=0u ||
       root_current()!=0u || !peer_entry() ||
       live()!=0u || ob_handles()!=0u)
        return (int)0xBADF5203u;
    if(hint((u32)(uintptr_t)peer_stack)!=0 ||
       hint((u32)(uintptr_t)&peer_stack[4095])!=0 ||
       hint((u32)(uintptr_t)&child_entry)!=0 ||
       hint((u32)(uintptr_t)&child_result)!=0 ||
       hint((u32)(uintptr_t)&parent_handle)!=0 ||
       hint((u32)(uintptr_t)parent_device)!=0)
        return (int)0xBADF5204u;
    if(open_(parent_device,(u32 *)(uintptr_t)&parent_handle)!=0u ||
       !parent_handle || live()!=1u)
        return (int)0xBADF5205u;
    child_tid=create((u32)(uintptr_t)&child_entry,
                    (u32)(uintptr_t)(peer_stack+sizeof(peer_stack)));
    if(!child_pid() || child_pid()==parent_pid ||
       child_tid!=parent_tid+1u)
        return (int)0xBADF5206u;
    for(u32 i=0u;i<500u;++i) {
        if(current_pid()!=parent_pid || current_tid()!=parent_tid ||
           root_current()!=0u || space_id()!=parent_pid ||
           parent_of()==parent_pid)
            return (int)0xBADF5207u;
        if(peer_yield()!=0u || progress()!=i+1u ||
           child_result!=0u)
            return (int)0xBADF5208u;
    }
    if(peer_yield()!=0u)return (int)0xBADF5209u;
    if(child_result!=0x4B4C4331u || child_pid()!=0u ||
       root_current()!=0u || live()!=1u ||
       ob_handles()!=0u)
        return (int)0xBADF520Au;
    // Child exit must NOT have closed its parent's handle.
    if(close_(parent_handle)!=0u || live()!=0u)
        return (int)0xBADF520Bu;
    return (int)0x50534B31u;
}
