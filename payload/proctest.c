/* PROCTEST.EXE: foreground serial handoff, intentional resource leaks.
 * Expected visible stage returns: 0x70010001, 0x70020002, 0x70030003.
 * Does not spawn threads or a second process address space. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t (*create_fn)(u32,u32);
typedef void *(*alloc_fn)(u32,u32);
typedef u32 (*count_fn)(void);
typedef int32_t (*launch_fn)(const char *);
typedef int32_t (*open_fn)(const char *,u32);
#define TAG 0x434F5250u
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD40011u;
    count_fn count=(count_fn)api[37],live=(count_fn)api[36];
    create_fn create=(create_fn)api[28];
    alloc_fn alloc=(alloc_fn)api[33];
    launch_fn launch=(launch_fn)api[27];
    open_fn open_=(open_fn)api[11];
    if(!count||!live||!create||!alloc||!launch||!open_)
        return 0xBAD40012u;
    if(count()!=0u || live()!=0u)return 0xBAD40013u;
    if(create(1u,1u)<=0 || !alloc(8192u,TAG))
        return 0xBAD40014u;
    // The launch implementation must open+close this EXE once, even
    // while Ob and Ex resources remain owned by the outgoing process.
    if(launch("\\fls0\\PROCNEXT.EXE")!=1)return 0xBAD40015u;
    // Leave one *read-only* SDK file descriptor intentionally open.
    // The NTOS teardown must close it EXACTLY ONCE before next launch.
    if(open_("\\fls0\\HELLO.EXE",1u)<0)return 0xBAD40016u;
    if(count()!=2u || live()!=8192u)return 0xBAD40017u;
    return 0x70010001u;
}
