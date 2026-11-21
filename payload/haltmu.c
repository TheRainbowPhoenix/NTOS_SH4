/* HALTMU.EXE: native SH7305 TMU+INTC *read-only* hardware MMIO,
 * over an NT DRIVER_OBJECT/IRP path. No timer stop/start, IRQ priority,
 * power, USB or host SDK calls. Success 0x544D5531 (TMU1).
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*ctl_fn)(u32,u32,void *,u32,u32 *);
typedef u32 (*close_fn)(u32);
typedef int32_t (*hint_fn)(u32);
static const char dev[]="\\Device\\Timer";
static volatile u32 handle,done;
static volatile u32 data[6];
static volatile u32 earlier;
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,88u))
        return (int)0xBADF6101u;
    open_fn open_=(open_fn)api[71];
    ctl_fn ctl=(ctl_fn)api[75];
    close_fn close_=(close_fn)api[74];
    hint_fn hint=(hint_fn)api[23];
    if(!open_ || !ctl || !close_ || !hint)
        return (int)0xBADF6102u;
    if(hint((u32)(uintptr_t)dev)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&done)!=0 ||
       hint((u32)(uintptr_t)data)!=0 ||
       hint((u32)(uintptr_t)&data[5])!=0 ||
       hint((u32)(uintptr_t)&earlier)!=0)
        return (int)0xBADF6103u;
    if(open_(dev,(u32 *)(uintptr_t)&handle)!=0u || !handle)
        return (int)0xBADF6104u;
    done=0u;
    if(ctl(handle,0x4E540100u,(void *)(uintptr_t)data,24u,
           (u32 *)(uintptr_t)&done)!=0u || done!=24u)
        return (int)0xBADF6105u;
    if(data[0]!=0x544D5531u || data[1]>7u ||
       data[4]>0xffffu || data[5]>0xffffu)
        return (int)0xBADF6106u;
    earlier=data[2];
    if(ctl(handle,0x4E540100u,(void *)(uintptr_t)data,24u,
           (u32 *)(uintptr_t)&done)!=0u || done!=24u ||
       data[0]!=0x544D5531u || data[1]>7u)
        return (int)0xBADF6107u;
    if(ctl(handle,0x4E540100u,(void *)(uintptr_t)data,20u,
           (u32 *)(uintptr_t)&done)!=0xC000000Du)
        return (int)0xBADF6108u;
    if(ctl(handle,0xFFFFFFFFu,(void *)(uintptr_t)data,24u,
           (u32 *)(uintptr_t)&done)!=0xC00000BBu)
        return (int)0xBADF6109u;
    if(close_(handle)!=0u)
        return (int)0xBADF610Au;
    return (int)0x544D5531u;
}
