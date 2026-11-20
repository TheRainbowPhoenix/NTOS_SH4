/* DEVNULL.EXE: actual NT-style synchronous IRPs through a named device.
 * Expected 0x444E4C31, no VBR fault, no SDK File_Open.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*read_fn)(u32,void *,u32,u32 *);
typedef u32 (*write_fn)(u32,const void *,u32,u32 *);
typedef u32 (*close_fn)(u32);
typedef u32 (*ctl_fn)(u32,u32,void *,u32,u32 *);
typedef int32_t (*hint_fn)(u32);
static const char name[]="\\Device\\Null";
static volatile u32 handle,done;
static volatile uint8_t data[32]={0xCAu,0x53u,0x49u,0x4Fu};
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,75u))return (int)0xBADAE001u;
    open_fn open_=(open_fn)api[71];
    read_fn read_=(read_fn)api[72];
    write_fn write_=(write_fn)api[73];
    close_fn close_=(close_fn)api[74];
    ctl_fn ctl=(ctl_fn)api[75];
    hint_fn hint=(hint_fn)api[23];
    if(!open_||!read_||!write_||!close_||!ctl||!hint)
        return (int)0xBADAE002u;
    if(hint((u32)(uintptr_t)name)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&done)!=0 ||
       hint((u32)(uintptr_t)data)!=0)
        return (int)0xBADAE003u;
    u32 *out=(u32 *)(uintptr_t)&handle;
    u32 *bytes=(u32 *)(uintptr_t)&done;
    handle=0u;
    if(open_(name,out)!=0u || !handle)return (int)0xBADAE004u;
    done=0u;
    if(write_(handle,(const void *)(uintptr_t)data,16u,bytes)!=0u ||
       done!=16u)return (int)0xBADAE005u;
    done=0xFFFFFFFFu;
    if(read_(handle,(void *)(uintptr_t)data,16u,bytes)!=0u ||
       done!=0u || data[0]!=0xCAu)
        return (int)0xBADAE006u;
    done=0xFFFFFFFEu;
    if(ctl(handle,0x1234u,(void *)(uintptr_t)data,4u,bytes)!=0xC00000BBu ||
       done!=0xFFFFFFFEu)
        return (int)0xBADAE007u;
    if(read_(handle,(void *)(uintptr_t)0x80000000u,4u,bytes)!=0xC000000Du)
        return (int)0xBADAE008u;
    const u32 saved=handle;
    if(close_(saved)!=0u || close_(saved)!=0xC0000008u)
        return (int)0xBADAE009u;
    handle=0u;
    if(open_((const char *)(uintptr_t)0xA0000000u,out)!=0xC000000Du ||
       handle!=0u)return (int)0xBADAE00Au;
    return (int)0x444E4C31u;
}
