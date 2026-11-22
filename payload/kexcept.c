/* KEXCEPT.EXE: exact per-KTHREAD user 0x0C0 with one-op TRAPA bridge.
 * Expected 0x4B455801, delivered=1, status=0xC0000005.
 */
#include <stdint.h>
#include "ntapi_caps.h"
#include "user_write_probe.h"
typedef uint32_t u32;
typedef u32 (*register_fn)(u32,u32,u32,u32);
typedef u32 (*query_fn)(void);
typedef int32_t (*hint_fn)(u32);
static const volatile uint8_t readonly[4096u]
    __attribute__((aligned(4096)))={[0]=0xA5u,[4095]=0x7Du};
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,63u))
        return (int)0xBAD9A001u;
    register_fn reg=(register_fn)api[58];
    query_fn delivered=(query_fn)api[60],status=(query_fn)api[61],
             address=(query_fn)api[62],depth=(query_fn)api[63];
    hint_fn hint=(hint_fn)api[23];
    if(!reg||!delivered||!status||!address||!depth||!hint)
        return (int)0xBAD9A002u;
    const u32 target=(u32)(uintptr_t)&readonly[0];
    const u32 op=(u32)(uintptr_t)&ntos_user_write_probe_store;
    const u32 cont=(u32)(uintptr_t)&ntos_user_write_probe_trapa;
    if(hint(target)!=0)return (int)0xBAD9A003u;
    if(delivered()!=0u || depth()!=0u)
        return (int)0xBAD9A004u;
    if(reg(target,0xDEADu,op,cont)!=0u)
        return (int)0xBAD9A005u;
    u32 token=reg(target,0x0C0u,op,cont);
    if(!token || depth()!=1u)return (int)0xBAD9A006u;
    ntos_user_write_probe_stub(target);
    if(delivered()!=1u || depth()!=0u)
        return (int)0xBAD9A007u;
    if(status()!=0xC0000005u || address()!=target)
        return (int)0xBAD9A008u;
    if(readonly[0]!=0xA5u || readonly[4095]!=0x7Du)
        return (int)0xBAD9A009u;
    return (int)0x4B455801u;
}
