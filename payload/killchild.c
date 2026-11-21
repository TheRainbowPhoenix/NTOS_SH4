/* KILLCHILD.EXE: different PE, Ke-owned different child PID.
 * Deliberately leak a Null handle so P1 Ke exit must reclaim it.
 * Internal success 0x4B4C4331, never launch this PE alone.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef int32_t (*hint_fn)(u32);
static volatile u32 child_handle,child_count,bytes;
static volatile uint8_t tx[4]={1,2,3,4};
static const char device[]="\\Device\\Null";
int main(const void *const *api,char **argv,char **envp) {
    (void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,88u))
        return (int)0xBADF5301u;
    const u32 parent=(u32)(uintptr_t)argv;
    query_fn pid=(query_fn)api[55],ppid=(query_fn)api[66];
    query_fn childpid=(query_fn)api[82],root=(query_fn)api[81];
    query_fn tick=(query_fn)api[77],yield_=(query_fn)api[68];
    query_fn live=(query_fn)api[88];
    hint_fn hint=(hint_fn)api[23];
    u32 (*open_)(const char *,u32 *)=(u32 (*)(const char *,u32 *))api[71];
    u32 (*write_)(u32,const void *,u32,u32 *)=
        (u32 (*)(u32,const void *,u32,u32 *))api[73];
    if(!pid || !ppid || !childpid || !root || !tick ||
       !yield_ || !live || !hint || !open_ || !write_)
        return (int)0xBADF5302u;
    if(pid()==parent || pid()!=childpid() ||
       ppid()!=parent || root()!=1u || live()!=1u)
        return (int)0xBADF5303u;
    if(hint((u32)(uintptr_t)device)!=0 ||
       hint((u32)(uintptr_t)&child_handle)!=0 ||
       hint((u32)(uintptr_t)&child_count)!=0 ||
       hint((u32)(uintptr_t)tx)!=0 ||
       hint((u32)(uintptr_t)&bytes)!=0)
        return (int)0xBADF5304u;
    if(open_(device,(u32 *)(uintptr_t)&child_handle)!=0u ||
       !child_handle || live()!=2u)
        return (int)0xBADF5305u;
    if(write_(child_handle,(const void *)(uintptr_t)tx,4u,
              (u32 *)(uintptr_t)&bytes)!=0u || bytes!=4u)
        return (int)0xBADF5306u;
    for(u32 i=0u;i<500u;++i) {
        if(root()!=1u || pid()!=childpid() ||
           live()!=2u || child_count!=i)
            return (int)0xBADF5307u;
        ++child_count;
        if(tick()!=i+1u || yield_()!=0u)
            return (int)0xBADF5308u;
    }
    return (int)0x4B4C4331u;
}
