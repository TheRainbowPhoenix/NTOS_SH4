/* ROMDIAG.EXE: read-only SH7305 / EX-word live MMU snapshot.
 * Display AT/SV/URC and current VBR/SR. No PTEH/PTEL writes, LDTLB,
 * TLB invalidation, exception handler install or kernel API windows.
 * Returns 0x524F4D32 after a keypress so results can be photographed. */
#include <stdint.h>
typedef uint32_t u32;
typedef u32 (*get_fn)(void);
typedef void (*puts_fn)(const char *);
static void hex8(char *p,u32 n) {
    static const char digits[]="0123456789ABCDEF";
    for(u32 i=0u;i<8u;++i)p[i]=digits[(n>>(28u-i*4u))&15u];
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    if(!api || !api[0] || !api[5] ||
       !api[24] || !api[25] || !api[26])
        return (int)0xBADBE001u;
    puts_fn put=(puts_fn)api[0];
    get_fn key=(get_fn)api[5];
    get_fn mmucr=(get_fn)api[24];
    get_fn vbr=(get_fn)api[25];
    get_fn sr=(get_fn)api[26];
    char text[]="MMUCR: 00000000";
    hex8(text+7,mmucr());put(text);
    char vec[] ="VBR:   00000000";
    hex8(vec+7,vbr());put(vec);
    char status[]="SR:    00000000";
    hex8(status+7,sr());put(status);
    put("ROM32A: press any key");
    (void)key();
    return (int)0x524F4D32u;
}
