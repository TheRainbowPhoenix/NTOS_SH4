/* Second app in the serial per-launch teardown chain. */
#include <stdint.h>
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef int32_t (*create_fn)(u32,u32);
typedef void *(*alloc_fn)(u32,u32);
typedef int32_t (*launch_fn)(const char *);
typedef int32_t (*open_fn)(const char *,u32);
typedef u32 (*set_fn)(u32);
#define TAG 0x434F5250u
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD40101u;
    query_fn count=(query_fn)api[37],live=(query_fn)api[36];
    create_fn create=(create_fn)api[28];
    alloc_fn alloc=(alloc_fn)api[33];
    launch_fn launch=(launch_fn)api[27];
    open_fn open_=(open_fn)api[11];
    set_fn set=(set_fn)api[29];
    if(!count||!live||!create||!alloc||!launch||!open_||!set)
        return 0xBAD40102u;
    if(count()!=0u || live()!=0u ||
       set(0x40000001u)!=0xC0000008u)return 0xBAD40103u;
    if(create(0u,0u)<=0 || !alloc(4096u,TAG))
        return 0xBAD40104u;
    if(launch("\\fls0\\PROCFINAL.EXE")!=1)return 0xBAD40105u;
    if(open_("\\fls0\\HELLO.EXE",1u)<0)return 0xBAD40106u;
    if(count()!=2u || live()!=4096u)return 0xBAD40107u;
    return 0x70020002u;
}
