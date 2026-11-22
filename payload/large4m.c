/* LARGE4M.EXE: >4 MiB real PE data and 1024 explicit MMU page hints.
 * The 4 MiB .rodata data is deliberately file-backed, not BSS.
 * Expected return: 0x4B029400; bad states: 0xBAD4xxxx.
 * This is an experimental-pager/emulator test ONLY. It cannot run with
 * the conservative 55-page eager-loader variant. */
#include <stdint.h>
extern const unsigned char ntos_large_blob[];
typedef int32_t (*hint_t)(uint32_t);
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api || !api[23])return 0xBAD40001u;
    hint_t prime=(hint_t)api[23];
    uint32_t sum=0u;
    for(uint32_t i=0u;i<1024u;++i) {
        const volatile unsigned char *p=
            (const volatile unsigned char *)(ntos_large_blob+i*4096u);
        // Explicit hint BEFORE reading P0 data, so this does not rely on
        // unverified spontaneous TLB-miss delivery on the real CP400.
        if(prime((uint32_t)p)!=0)return 0xBAD40002u;
        sum+=*p;
        if(*p!=0xA5u)return 0xBAD40003u;
    }
    return 0x4B000000u | sum;
}
