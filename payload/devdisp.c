/* DEVDISP.EXE: real HH3 framebuffer driver via NT DRIVER_OBJECT/IRP.
 * Only a bounded pixel write, no raw MMIO, IRQ/USB or SDK FS.
 * Expected 0x44535031 ("DSP1"), PF/EV/BAD=0. The display pixel is
 * written then reset to white in a tiny area; no persistent GUI change.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*ctl_fn)(u32,u32,void *,u32,u32 *);
typedef u32 (*close_fn)(u32);
typedef int32_t (*hint_fn)(u32);
static const char device[]="\\Device\\Display";
static volatile u32 handle,bytes;
static volatile u32 pixel[3]={3u,3u,0xF800u};
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,83u))
        return (int)0xBADD4101u;
    open_fn open_=(open_fn)api[71];
    ctl_fn ctl=(ctl_fn)api[75];
    close_fn close_=(close_fn)api[74];
    hint_fn hint=(hint_fn)api[23];
    if(!open_ || !ctl || !close_ || !hint)
        return (int)0xBADD4102u;
    if(hint((u32)(uintptr_t)device)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&bytes)!=0 ||
       hint((u32)(uintptr_t)pixel)!=0)
        return (int)0xBADD4103u;
    if(open_(device,(u32 *)(uintptr_t)&handle)!=0u || !handle)
        return (int)0xBADD4104u;
    bytes=0u;
    if(ctl(handle,0x4E540001u,(void *)(uintptr_t)pixel,12u,
           (u32 *)(uintptr_t)&bytes)!=0u || bytes!=12u)
        return (int)0xBADD4105u;
    pixel[0]=0xFFFFFFFFu;
    if(ctl(handle,0x4E540001u,(void *)(uintptr_t)pixel,12u,
           (u32 *)(uintptr_t)&bytes)!=0xC000000Du)
        return (int)0xBADD4106u;
    pixel[0]=3u;pixel[2]=0xFFFFFFFFu;
    if(ctl(handle,0x4E540001u,(void *)(uintptr_t)pixel,12u,
           (u32 *)(uintptr_t)&bytes)!=0xC000000Du)
        return (int)0xBADD4107u;
    pixel[2]=0xFFFFu;
    if(ctl(handle,0x4E540001u,(void *)(uintptr_t)pixel,12u,
           (u32 *)(uintptr_t)&bytes)!=0u)
        return (int)0xBADD4108u;
    if(ctl(handle,0xFFFFFFFFu,(void *)(uintptr_t)pixel,12u,
           (u32 *)(uintptr_t)&bytes)!=0xC00000BBu)
        return (int)0xBADD4109u;
    if(close_(handle)!=0u)return (int)0xBADD410Au;
    return (int)0x44535031u;
}
