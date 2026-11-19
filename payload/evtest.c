/* EVTEST.EXE: NotificationEvent and SynchronizationEvent state machine.
 * Zero-timeout wait only; blocking scheduler is not implemented. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t (*create_fn)(u32,u32);
typedef u32 (*single_fn)(u32);
typedef u32 (*wait_fn)(u32,u32);
typedef u32 (*count_fn)(void);
#define OK 0u
#define TIMEOUT 0x102u
#define INVALID 0xC0000008u
#define UNSUPPORTED 0xC00000BBu
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD30001u;
    create_fn create=(create_fn)api[28];
    single_fn set=(single_fn)api[29],reset=(single_fn)api[30];
    wait_fn wait=(wait_fn)api[31];
    single_fn close=(single_fn)api[32];
    count_fn count=(count_fn)api[37];
    if(!create||!set||!reset||!wait||!close||!count)
        return 0xBAD30002u;
    if(count()!=0u)return 0xBAD30003u;
    int32_t m=create(1u,0u),a=create(0u,0u);
    if(m<=0||a<=0||m==a)return 0xBAD30004u;
    if(wait((u32)m,0u)!=TIMEOUT || wait((u32)a,0u)!=TIMEOUT ||
       wait((u32)m,7u)!=UNSUPPORTED)return 0xBAD30005u;
    if(set((u32)m)!=OK || wait((u32)m,0u)!=OK ||
       wait((u32)m,0u)!=OK)return 0xBAD30006u;
    if(reset((u32)m)!=OK || wait((u32)m,0u)!=TIMEOUT)
        return 0xBAD30007u;
    if(set((u32)a)!=OK || wait((u32)a,0u)!=OK ||
       wait((u32)a,0u)!=TIMEOUT)return 0xBAD30008u;
    if(set((u32)a)!=OK || reset((u32)a)!=OK ||
       wait((u32)a,0u)!=TIMEOUT)return 0xBAD30009u;
    if(close((u32)m)!=OK || close((u32)a)!=OK)
        return 0xBAD3000Au;
    if(wait((u32)m,0u)!=INVALID || set((u32)a)!=INVALID ||
       count()!=0u)return 0xBAD3000Bu;
    // Recycled object slots still carry fresh, non-aliasing handle cookies.
    u32 previous=(u32)a;
    for(u32 i=0u;i<128u;++i) {
        int32_t e=create(i&1u,i&1u);
        if(e<=0 || (u32)e==previous)return 0xBAD3000Cu;
        if(i&1u) {
            if(wait((u32)e,0u)!=OK || wait((u32)e,0u)!=OK)
                return 0xBAD3000Du;
        } else {
            if(wait((u32)e,0u)!=TIMEOUT || set((u32)e)!=OK ||
               wait((u32)e,0u)!=OK || wait((u32)e,0u)!=TIMEOUT)
                return 0xBAD3000Eu;
        }
        if(close((u32)e)!=OK || wait((u32)e,0u)!=INVALID)
            return 0xBAD3000Fu;
        previous=(u32)e;
    }
    if(count()!=0u)return 0xBAD30010u;
    return 0xE7000010u;
}
