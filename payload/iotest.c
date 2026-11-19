/* IOTEST.EXE: read-only real SDK gate checks; NEVER inject SDK failures.
 * Short-read / ambiguous-close injection belongs to MOCK HOST TESTS.
 * Requires matching \\fls0\\HELLO.EXE. Expect 0x10F00010. */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t (*open_fn)(const char *,u32);
typedef int32_t (*read_fn)(int32_t,void *,u32);
typedef int32_t (*close_fn)(int32_t);
typedef int32_t (*size_fn)(int32_t);
typedef int32_t (*seek_fn)(int32_t,int32_t,u32);
typedef u32 (*count_fn)(void);
typedef int32_t (*event_fn)(u32,u32);
#define PATH "\\fls0\\HELLO.EXE"
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD60001u;
    open_fn open_=(open_fn)api[11];
    read_fn read_=(read_fn)api[12];
    close_fn close_=(close_fn)api[14];
    size_fn size_=(size_fn)api[17];
    seek_fn seek_=(seek_fn)api[18];
    count_fn count=(count_fn)api[37];
    event_fn event=(event_fn)api[28];
    if(!open_||!read_||!close_||!size_||!seek_||!count||!event)
        return 0xBAD60002u;
    if(count()!=0u)return 0xBAD60003u;
    int32_t fd=open_(PATH,1u);
    if(fd<=0 || count()!=1u)return 0xBAD60004u;
    // Must never forward this second open to the SDK.
    if(open_(PATH,1u)!=-16 || count()!=1u)
        return 0xBAD60005u;
    if(size_(fd)<512)return 0xBAD60006u;
    uint8_t bytes[16]; // File_Read fills every byte before inspection.
    if(read_(fd,bytes,16u)!=16)return 0xBAD60007u;
    // Header signature "MZ"; never modify the file.
    if(bytes[0]!=0x4Du || bytes[1]!=0x5Au)
        return 0xBAD60008u;
    if(seek_(fd,0,0u)<0)return 0xBAD60009u;
    if(read_(fd,bytes,2u)!=2 ||
       bytes[0]!=0x4Du || bytes[1]!=0x5Au)
        return 0xBAD6000Au;
    int32_t ev=event(1u,0u);
    if(ev<=0)return 0xBAD6000Bu;
    // Wrong-type event cookies must NOT enter the SDK file path.
    if(read_(ev,bytes,1u)!=-2 || close_(ev)!=-2)
        return 0xBAD6000Cu;
    if(close_(fd)!=0 || count()!=1u)return 0xBAD6000Du;
    if(read_(fd,bytes,1u)!=-2 || close_(fd)!=-2)
        return 0xBAD6000Eu;
    // A recycled SDK fd must not resurrect the old opaque cookie.
    int32_t second=open_(PATH,1u);
    if(second<=0 || second==fd)return 0xBAD6000Fu;
    if(read_(fd,bytes,1u)!=-2 ||
       close_(second)!=0)return 0xBAD60010u;
    typedef u32 (*nt_close_fn)(u32);
    nt_close_fn ntclose=(nt_close_fn)api[32];
    if(!ntclose || ntclose((u32)ev)!=0u || count()!=0u)
        return 0xBAD60011u;
    return 0x10F00010u;
}
