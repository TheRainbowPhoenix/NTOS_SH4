/* ASOWN.EXE: verify KPROCESS-private root and VM lifetime.
 * Expected 0x41534F10; committed live MEM_RELEASE remains denied.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*zero_fn)(void);
typedef u32 (*reserve_fn)(u32,u32);
typedef u32 (*range_fn)(u32,u32,u32);
typedef u32 (*release_fn)(u32);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,65u))
        return (int)0xBAD9B001u;
    zero_fn pid=(zero_fn)api[55],root=(zero_fn)api[64],
            epoch=(zero_fn)api[65];
    reserve_fn reserve=(reserve_fn)api[41];
    range_fn commit=(range_fn)api[42];
    release_fn release=(release_fn)api[45];
    if(!pid||!root||!epoch||!reserve||!commit||!release)
        return (int)0xBAD9B002u;
    if(!pid() || root()!=pid() || epoch()==0u)
        return (int)0xBAD9B003u;
    u32 va=reserve(0u,1u);
    if(!va || commit(va,1u,2u)!=0u)
        return (int)0xBAD9B004u;
    volatile uint8_t *p=(volatile uint8_t *)(uintptr_t)va;
    p[0]=0x65u;p[4095]=0x9Au;
    if(p[0]!=0x65u || p[4095]!=0x9Au)
        return (int)0xBAD9B005u;
    if(release(va)!=0xC00000E8u || root()!=pid())
        return (int)0xBAD9B006u;
    return (int)0x41534F10u;
}
