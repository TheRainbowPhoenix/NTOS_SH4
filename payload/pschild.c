/* PSCHILD.EXE: distinct PID, inherited-from-parent PID, fresh VM root.
 * Expected final 0x50534310. Not a concurrent child.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*pid_fn)(void);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,66u))return (int)0xBAD9D001u;
    pid_fn pid=(pid_fn)api[55],parent=(pid_fn)api[66],
           root=(pid_fn)api[64];
    if(!pid||!parent||!root)return (int)0xBAD9D002u;
    const u32 c=pid(),p=parent();
    if(!p || c!=p+1u || root()!=c)
        return (int)0xBAD9D003u;
    return (int)0x50534310u;
}
