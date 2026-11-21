/* PSUSER.EXE - real user execution with a P0 user stack and 128
 * validated user TRAPA->Ke->RTE services, then privileged return.
 * Expected 0x50535531 (PSU1). BADF51xx identify failure stages.
 * NOT a general unprivileged process or timer-preemptive scheduler.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*arm_fn)(u32,u32);
typedef int32_t (*hint_fn)(u32);
extern u32 psuser_run(u32 process,u32 p0_sp);
extern void psuser_loop_trap(void);
extern void psuser_exit_trap(void);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *meta=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(meta,87u))return (int)0xBADF5101u;
    query_fn stack=(query_fn)api[85],pid=(query_fn)api[55];
    query_fn tid=(query_fn)api[56],total=(query_fn)api[57];
    query_fn session_calls=(query_fn)api[87];
    arm_fn arm=(arm_fn)api[86];
    hint_fn hint=(hint_fn)api[23];
    if(!stack || !pid || !tid || !total || !session_calls ||
       !arm || !hint)return (int)0xBADF5102u;
    const u32 process=pid(),thread=tid(),sp=stack();
    if(!process || !thread || sp<0x1000u || sp>=0x80000000u ||
       (sp&4095u) || total()!=0u || session_calls()!=0u)
        return (int)0xBADF5103u;
    if(hint((u32)(uintptr_t)&psuser_run)!=0 ||
       hint((u32)(uintptr_t)&psuser_loop_trap)!=0 ||
       hint((u32)(uintptr_t)&psuser_exit_trap)!=0)
        return (int)0xBADF5104u;
    if(arm((u32)(uintptr_t)&psuser_loop_trap,
           (u32)(uintptr_t)&psuser_exit_trap)!=0u)
        return (int)0xBADF5105u;
    if(psuser_run(process,sp)!=1u)return (int)0xBADF5106u;
    if(pid()!=process || tid()!=thread || total()!=128u ||
       session_calls()!=128u)return (int)0xBADF5107u;
    return (int)0x50535531u;
}
