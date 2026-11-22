/* Third app: the previous two applications must have zero live objects
 * and zero tracked allocations; the host gate must allow a fresh open. */
#include <stdint.h>
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*single_fn)(u32);
typedef int32_t (*open_fn)(const char *,u32);
typedef int32_t (*close_fn)(int32_t);
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD40201u;
    query_fn count=(query_fn)api[37],live=(query_fn)api[36];
    single_fn set=(single_fn)api[29];
    open_fn open_=(open_fn)api[11];
    close_fn close_=(close_fn)api[14];
    if(!count||!live||!set||!open_||!close_)
        return 0xBAD40202u;
    if(count()!=0u || live()!=0u ||
       set(0x40000001u)!=0xC0000008u)
        return 0xBAD40203u;
    int32_t fd=open_("\\fls0\\HELLO.EXE",1u);
    if(fd<=0)return 0xBAD40204u;
    if(close_(fd)!=0 || count()!=0u || live()!=0u)
        return 0xBAD40205u;
    return 0x70030003u;
}
