/* OBTEST.EXE: executable-level regression of NT Ob/Ex gates.
 * Result 0x0B0B0010 = all checks passed. No filesystem calls or writes. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t (*create_event_t)(u32,u32);
typedef u32 (*event_t)(u32);
typedef u32 (*wait_t)(u32,u32);
typedef void *(*alloc_t)(u32,u32);
typedef u32 (*free_t)(void *,u32);
typedef u32 (*query_t)(void);
#define NT_SUCCESS 0x00000000u
#define NT_TIMEOUT 0x00000102u
#define NT_INVALID_HANDLE 0xC0000008u
#define NT_MISMATCH 0xC0000024u
#define NT_TAG_OBTS 0x5354424Fu
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD01001u;
    create_event_t create=(create_event_t)api[28];
    event_t set=(event_t)api[29],reset=(event_t)api[30];
    wait_t wait=(wait_t)api[31];
    event_t close=(event_t)api[32];
    alloc_t alloc=(alloc_t)api[33];
    free_t free_tag=(free_t)api[34];
    query_t peak=(query_t)api[35],live=(query_t)api[36];
    query_t count=(query_t)api[37];
    if(!create||!set||!reset||!wait||!close||!alloc||!free_tag||
       !peak||!live||!count)return 0xBAD01002u;
    if(count()!=0u)return 0xBAD01003u;
    int32_t e=create(1u,0u);
    if(e<=0)return 0xBAD01004u;
    if(wait((u32)e,0u)!=NT_TIMEOUT)return 0xBAD01005u;
    if(set((u32)e)!=NT_SUCCESS)return 0xBAD01006u;
    if(wait((u32)e,0u)!=NT_SUCCESS ||
       wait((u32)e,0u)!=NT_SUCCESS)return 0xBAD01007u;
    if(reset((u32)e)!=NT_SUCCESS ||
       wait((u32)e,0u)!=NT_TIMEOUT)return 0xBAD01008u;
    if(close((u32)e)!=NT_SUCCESS ||
       close((u32)e)!=NT_INVALID_HANDLE)return 0xBAD01009u;
    int32_t a=create(0u,1u);
    if(a<=0 || a==e)return 0xBAD0100Au;
    if(wait((u32)a,0u)!=NT_SUCCESS ||
       wait((u32)a,0u)!=NT_TIMEOUT ||
       set((u32)e)!=NT_INVALID_HANDLE)return 0xBAD0100Bu;
    if(wait((u32)a,1u)!=0xC00000BBu)return 0xBAD0100Cu;
    void *p=alloc(4096u,NT_TAG_OBTS);
    if(!p || live()<4096u || peak()<4096u)return 0xBAD0100Du;
    ((volatile unsigned char *)p)[0]=0x51u;
    ((volatile unsigned char *)p)[4095]=0xA2u;
    if(free_tag(p,NT_TAG_OBTS)!=NT_SUCCESS ||
       free_tag(p,NT_TAG_OBTS)!=0xC000000Du ||
       live()!=0u)return 0xBAD0100Eu;
    if(close((u32)a)!=NT_SUCCESS || count()!=0u)return 0xBAD0100Fu;
    return 0x0B0B0010u;
}
