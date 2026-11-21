/* HALUSB.EXE: first native SH7305 USB hardware-status IRP.
 * READ ONLY clock gate + module-stop registers. No USB controller
 * register is accessed (the peripheral may be powered down).
 * Expected 0x55534231, failure BADF63xx.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*ctl_fn)(u32,u32,void *,u32,u32 *);
typedef u32 (*close_fn)(u32);
typedef int32_t (*hint_fn)(u32);
static const char dev[]="\\Device\\UsbStatus";
static volatile u32 handle,done,data[3];
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,88u))
        return (int)0xBADF6301u;
    open_fn open_=(open_fn)api[71];
    ctl_fn ctl=(ctl_fn)api[75];
    close_fn close_=(close_fn)api[74];
    hint_fn hint=(hint_fn)api[23];
    if(!open_ || !ctl || !close_ || !hint)
        return (int)0xBADF6302u;
    if(hint((u32)(uintptr_t)dev)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&done)!=0 ||
       hint((u32)(uintptr_t)data)!=0 ||
       hint((u32)(uintptr_t)&data[2])!=0)
        return (int)0xBADF6303u;
    if(open_(dev,(u32 *)(uintptr_t)&handle)!=0u || !handle)
        return (int)0xBADF6304u;
    done=0u;
    if(ctl(handle,0x4E540200u,(void *)(uintptr_t)data,12u,
           (u32 *)(uintptr_t)&done)!=0u ||
       done!=12u || data[0]!=0x55534231u)
        return (int)0xBADF6305u;
    if(ctl(handle,0x4E540200u,(void *)(uintptr_t)data,8u,
           (u32 *)(uintptr_t)&done)!=0xC000000Du)
        return (int)0xBADF6306u;
    if(ctl(handle,0xFFFFFFFFu,(void *)(uintptr_t)data,12u,
           (u32 *)(uintptr_t)&done)!=0xC00000BBu)
        return (int)0xBADF6307u;
    if(close_(handle)!=0u)
        return (int)0xBADF6308u;
    return (int)0x55534231u;
}
