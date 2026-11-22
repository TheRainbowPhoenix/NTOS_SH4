/* KTHREAD.EXE: real SH4A user-to-Ke system-call gate.
 * The WinCE/COREDLL ABI stays loaded; this is a new private NtosApi gate.
 * Expected 0x4B540010 with KE SVC=3, YIELD=1, BADGATE=0,
 * PF=0, BAD=0 and a clean return to the NTOS picker.
 */
#include <stdint.h>
#include "ntapi_caps.h"
#include "user_syscall_gate.h"
typedef uint32_t u32;
typedef u32 (*arm_fn)(u32,u32);
typedef u32 (*get_fn)(void);
enum {
    CURRENT_PID=0x1000u,
    CURRENT_TID=0x1001u,
    YIELD=0x1002u
};
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *window=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(window,57u))
        return (int)0xBAD99001u;
    arm_fn arm=(arm_fn)api[54];
    get_fn pid_get=(get_fn)api[55],tid_get=(get_fn)api[56],
           count_get=(get_fn)api[57];
    if(!arm || !pid_get || !tid_get || !count_get)
        return (int)0xBAD99002u;
    const u32 process=pid_get(),thread=tid_get();
    if(process<0x100u || thread<0x10000u || count_get()!=0u)
        return (int)0xBAD99003u;
    const u32 trap_pc=(u32)(uintptr_t)&ntos_user_syscall_trapa;
    if(arm(trap_pc,CURRENT_PID)!=0u)
        return (int)0xBAD99004u;
    if(ntos_user_syscall_stub(CURRENT_PID,0u)!=process)
        return (int)0xBAD99005u;
    if(arm(trap_pc,CURRENT_TID)!=0u)
        return (int)0xBAD99006u;
    if(ntos_user_syscall_stub(CURRENT_TID,0u)!=thread)
        return (int)0xBAD99007u;
    if(arm(trap_pc,YIELD)!=0u)
        return (int)0xBAD99008u;
    if(ntos_user_syscall_stub(YIELD,0u)!=0u)
        return (int)0xBAD99009u;
    if(count_get()!=3u || pid_get()!=process ||
       tid_get()!=thread)
        return (int)0xBAD9900Au;
    if(arm(trap_pc,0xDEADu)!=0xC000000Du)
        return (int)0xBAD9900Bu; // no arbitrary system-service number
    return (int)0x4B540010u;
}
