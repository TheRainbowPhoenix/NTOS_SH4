/* DEVLOOP.EXE: real guest IRP WRITE -> READ against replaceable
 * kernel loopback driver, with P0 pointer, length and handle checks.
 * Expected 0x444C5031.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*open_fn)(const char *,u32 *);
typedef u32 (*io_fn)(u32,void *,u32,u32 *);
typedef u32 (*close_fn)(u32);
typedef int32_t (*hint_fn)(u32);
static const char name[]="\\Device\\Loopback";
static volatile u32 handle,read_bytes,write_bytes;
static volatile uint8_t send[64] __attribute__((aligned(4096)));
static volatile uint8_t receive[64] __attribute__((aligned(4096)));
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,74u))return (int)0xBADAF001u;
    open_fn open_=(open_fn)api[71];
    io_fn read_=(io_fn)api[72],write_=(io_fn)api[73];
    close_fn close_=(close_fn)api[74];
    hint_fn hint=(hint_fn)api[23];
    if(!open_ || !read_ || !write_ || !close_ || !hint)
        return (int)0xBADAF002u;
    if(hint((u32)(uintptr_t)name)!=0 ||
       hint((u32)(uintptr_t)&handle)!=0 ||
       hint((u32)(uintptr_t)&write_bytes)!=0 ||
       hint((u32)(uintptr_t)&read_bytes)!=0 ||
       hint((u32)(uintptr_t)send)!=0 ||
       hint((u32)(uintptr_t)receive)!=0)
        return (int)0xBADAF003u;
    for(u32 i=0u;i<64u;++i) {
        send[i]=(uint8_t)((i*29u+13u)&255u);receive[i]=0u;
    }
    u32 *h=(u32 *)(uintptr_t)&handle;
    u32 *written=(u32 *)(uintptr_t)&write_bytes;
    u32 *read=(u32 *)(uintptr_t)&read_bytes;
    handle=0u;read_bytes=write_bytes=0u;
    if(open_(name,h)!=0u || !handle)return (int)0xBADAF004u;
    if(write_(handle,(void *)(uintptr_t)send,64u,written)!=0u ||
       write_bytes!=64u)return (int)0xBADAF005u;
    if(read_(handle,(void *)(uintptr_t)receive,64u,read)!=0u ||
       read_bytes!=64u)return (int)0xBADAF006u;
    for(u32 i=0u;i<64u;++i)
        if(receive[i]!=send[i])return (int)0xBADAF007u;
    if(read_(0x5000F00Du,(void *)(uintptr_t)receive,4u,read)!=0xC0000008u)
        return (int)0xBADAF008u;
    if(close_(handle)!=0u)return (int)0xBADAF009u;
    return (int)0x444C5031u;
}
