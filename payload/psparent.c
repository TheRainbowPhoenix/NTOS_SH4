/* PSPARENT.EXE serial NtLaunch parent; child runs only after return.
 * This is NOT a multitasking test. Expected 0x50535010 then PSCHILD.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*pid_fn)(void);
typedef int32_t (*launch_fn)(const char *);
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,66u))return (int)0xBAD9C001u;
    pid_fn pid=(pid_fn)api[55],parent=(pid_fn)api[66];
    launch_fn launch=(launch_fn)api[27];
    if(!pid||!parent||!launch||pid()<257u||parent()!=0u)
        return (int)0xBAD9C002u;
    if(launch("\\fls0\\PSCHILD.EXE")!=1)
        return (int)0xBAD9C003u;
    return (int)0x50535010u;
}
