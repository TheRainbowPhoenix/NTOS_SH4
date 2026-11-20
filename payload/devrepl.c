/* DEVREPL.EXE: physically dispatched guest-to-NT IoReplaceDriver
 * rejection while file open, then replacement after close, and transfer.
 * Synthetic Loopback A=plain/B=inverted; not SH7305 MMIO.
 * Expected 0x44525631 ("DRV1").
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef int32_t (*hint_fn)(u32);
typedef u32 (*close_fn)(u32);
typedef u32 (*swap_fn)(u32);
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*io_fn)(u32,void *,u32,u32 *);
static const char name[]="\\Device\\Loopback";
static volatile u32 handle,bytes;
static volatile uint8_t source[8]={0x11u,0x27u,0xA5u,0xF0u};
static volatile uint8_t target[8];
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,83u))return (int)0xBADDD001u;
    open_fn open_=(open_fn)api[71];
    io_fn read_=(io_fn)api[72],write_=(io_fn)api[73];
    close_fn close_=(close_fn)api[74];
    swap_fn swap=(swap_fn)api[83];
    hint_fn hint=(hint_fn)api[23];
    if(!open_ || !read_ || !write_ || !close_ || !swap || !hint)
        return (int)0xBADDD002u;
    if(hint((u32)(uintptr_t)name)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&bytes)!=0 ||
       hint((u32)(uintptr_t)source)!=0 ||
       hint((u32)(uintptr_t)target)!=0)
        return (int)0xBADDD003u;
    if(swap(2u)!=0xC000000Du)return (int)0xBADDD004u;
    for(u32 variant=0u;variant<3u;++variant) {
        if(open_(name,(u32 *)(uintptr_t)&handle)!=0u || !handle)
            return (int)0xBADDD005u;
        bytes=0u;
        if(write_(handle,(void *)(uintptr_t)source,4u,
                  (u32 *)(uintptr_t)&bytes)!=0u || bytes!=4u)
            return (int)0xBADDD006u;
        if(read_(handle,(void *)(uintptr_t)target,4u,
                 (u32 *)(uintptr_t)&bytes)!=0u || bytes!=4u)
            return (int)0xBADDD007u;
        for(u32 i=0u;i<4u;++i) {
            const uint8_t wanted=variant==1u?
                (uint8_t)(source[i]^0xFFu):source[i];
            if(target[i]!=wanted)return (int)0xBADDD008u;
        }
        if(swap(variant==1u?0u:1u)!=0xC00000E8u)
            return (int)0xBADDD009u;
        if(close_(handle)!=0u)return (int)0xBADDD00Au;
        if(variant<2u && swap(variant+1u==1u?1u:0u)!=0u)
            return (int)0xBADDD00Bu;
    }
    if(swap(0u)!=0u)return (int)0xBADDD00Cu;
    return (int)0x44525631u;
}
