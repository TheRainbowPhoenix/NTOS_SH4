/* TWINCHILD.EXE: independently linked import-free PE, child KTHREAD in
 * distinct logical KPROCESS PID and hardware-switched P0 private page.
 * Executable image base is disjoint from parent's (same-VA code is NEXT).
 * Returns internal 0x50534332 ("PSC2").
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*io_fn)(u32);
static volatile u32 child_handle,child_bytes;
static const char null_name[]="\\Device\\Null";
static volatile u32 child_counter;
int main(const void *const *api,char **argv,char **envp) {
    (void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,82u))return (int)0xBADD3001u;
    query_fn yield_=(query_fn)api[68],tick=(query_fn)api[77];
    query_fn pid=(query_fn)api[55],tid=(query_fn)api[56];
    query_fn parent=(query_fn)api[66],root_va=(query_fn)api[79];
    query_fn root_current=(query_fn)api[81];
    query_fn space=(query_fn)api[64],expected_child=(query_fn)api[82];
    int32_t (*hint)(u32)=(int32_t (*)(u32))api[23];
    u32 (*open_)(const char *,u32 *)=(u32 (*)(const char *,u32 *))api[71];
    io_fn close_=(io_fn)api[74];
    if(!yield_ || !tick || !pid || !tid || !parent ||
       !root_va || !root_current || !space || !expected_child ||
       !hint || !open_ || !close_)
        return (int)0xBADD3002u;
    if(hint((u32)(uintptr_t)&child_handle)!=0 ||
       hint((u32)(uintptr_t)null_name)!=0 ||
       hint((u32)(uintptr_t)&child_counter)!=0)
        return (int)0xBADD3003u;
    if(pid()==0u || pid()!=expected_child() || parent()==0u ||
       parent()==pid() || space()!=pid() || !tid())
        return (int)0xBADD3004u;
    volatile u32 *const page=(volatile u32 *)(uintptr_t)root_va();
    for(u32 i=0u;i<500u;++i) {
        if(root_current()!=1u || child_counter!=i)
            return (int)0xBADD3005u;
        if(page[0]!=(i==0u?0u:(0xC7000000u|(i-1u))) ||
           page[1023]!=(i==0u?0u:(0xE7000000u|(i-1u))))
            return (int)0xBADD3006u;
        page[0]=0xC7000000u|i;
        page[1023]=0xE7000000u|i;
        ++child_counter;
        if(tick()!=i+1u)return (int)0xBADD3007u;
        if(yield_()!=0u)return (int)0xBADD3008u;
    }
    // The parent handed us its real Io handle as an opaque 32-bit value.
    // Per-PID FileObject lookup must reject it, without closing parent's.
    const u32 inherited=(u32)(uintptr_t)argv;
    if(!inherited || close_(inherited)!=0xC0000008u)
        return (int)0xBADD3009u;
    child_handle=0u;
    if(open_(null_name,(u32 *)(uintptr_t)&child_handle)!=0u ||
       !child_handle)return (int)0xBADD300Au;
    if(close_(child_handle)!=0u)return (int)0xBADD300Bu;
    if(child_counter!=500u || root_current()!=1u)
        return (int)0xBADD300Cu;
    return (int)0x50534332u;
}
