/* Only two ROM32A revisions have known experimental P1 arenas.
 * The check addresses/bytes and D6600 alias guard follow Edelweiss.
 * This is NOT an OS-managed memory reservation or universal ROM support. */
#include "profile.hpp"
#include <gint/cpu.h>
#include <stdint.h>
const NtosRomProfile ntos_rom_profiles[2]={
    {{0x8c700000u,0x8c7f0000u,0x8c7f8000u,0x8c800000u,"XD-A8500 ROM32A"},
     {{0x8004634cu,{0x4f,0x22,0x7f,0xc0,0x1f,0x7a,0x1f,0x69,0x1f,0x58,0xe1,0,0x35,0x10,0x8f,2}},
      {0x802d89bcu,{0x4f,0x22,0x7f,0xfc,0xd1,0x96,0x2f,0x52,0xd5,0x96,0x41,0x0b,0xe6,8,0xd7,0x94}},
      {0x80025514u,{0x2f,0xc6,0x2f,0xd6,0x2f,0xe6,0x4f,0x22,0xb0,0x15,0xec,1,0x6d,0x0d,0xd2,0x4a}}}},
    {{0x8c800000u,0x8c8f0000u,0x8c8f8000u,0x8c900000u,"XD-D6600 CY436 ROM32A"},
     {{0x80047020u,{0x4f,0x22,0x7f,0xc0,0x1f,0x7a,0x1f,0x69,0x1f,0x58,0xe1,0,0x35,0x10,0x8f,2}},
      {0x802f980cu,{0x4f,0x22,0x7f,0xfc,0xd1,0x57,0x2f,0x52,0xd5,0x57,0x41,0x0b,0xe6,8,0xd7,0x55}},
      {0x80024ad6u,{0x2f,0xc6,0x2f,0xd6,0x2f,0xe6,0x4f,0x22,0xb0,0x15,0xec,1,0x6d,0x0d,0xd2,0x46}}}}
};
/* ROM32A is physically addressed in SH4's P1/P2 space. A cached P1
 * read may see stale lines after the native world toggles cache/MMU
 * state. Match fixed ROM bytes through the uncached P2 alias instead.
 * This does NOT install mappings or touch MMUCR/TLB/VBR. */
static bool read_rom(void *,uint32_t addr,uint8_t *out,unsigned len)
{
    if(!out || !len || addr<0x80000000u ||
       addr>0x82000000u || len>0x82000000u-addr)return false;
    const volatile uint8_t *src=
        (const volatile uint8_t *)(uintptr_t)(addr|0x20000000u);
    for(unsigned i=0u;i<len;++i)out[i]=src[i];
    return true;
}
static const char *g_arena_error="not checked";
static char g_arena_error_text[81];
const char *ntos_exword_arena_error(void) {return g_arena_error;}
static void puthex(char *dest,uint32_t n)
{
    static const char hex[]="0123456789ABCDEF";
    for(unsigned i=0u;i<8u;++i)
        dest[i]=hex[(n>>(28u-i*4u))&15u];
}
static void mismatch_reason(void)
{
    /* A mismatched 16-byte signature means ROM verification failed,
     * NOT that P1 RAM is absent. Report the actual uncached P2 word
     * so the user's machine can be compared to ROM32A.BIN. */
    const NtosRomProfile &known=ntos_rom_profiles[0];
    for(unsigned index=0u;index<3u;++index) {
        const NtosRomSignature &sig=known.checks[index];
        uint8_t found[16]{};
        if(!read_rom(nullptr,sig.addr,found,16u)) {
            g_arena_error="uncached P2 ROM read failed";
            return;
        }
        unsigned mismatch=0u;
        while(mismatch<16u && found[mismatch]==sig.data[mismatch])
            ++mismatch;
        if(mismatch==16u)continue;
        const char prefix[]="ROM check @";
        unsigned i=0u;
        for(;prefix[i];++i)g_arena_error_text[i]=prefix[i];
        puthex(g_arena_error_text+i,sig.addr);
        i+=8u;
        const char middle[]=" got ";
        for(unsigned j=0u;middle[j];++j)g_arena_error_text[i++]=middle[j];
        uint32_t word=0u;
        for(unsigned j=0u;j<4u;++j)
            word=(word<<8u)|(uint32_t)found[j];
        puthex(g_arena_error_text+i,word);
        i+=8u;
        g_arena_error_text[i]=0;
        g_arena_error=g_arena_error_text;
        return;
    }
    g_arena_error="not XD-A8500 or XD-D6600 ROM32A";
}
bool ntos_exword_arena_check(NtosExwordArena *out)
{
    if(!out) {g_arena_error="invalid P1 arena output";return false;}
    /* ROM32A itself does not change while this D01 is resident. Cache
     * a successful fingerprint rather than re-identifying after every
     * guest/WinCE world switch. Failed probes are never cached. */
    static const NtosRomProfile *verified=nullptr;
    if(!verified)verified=ntos_rom_match(read_rom,nullptr);
    if(!verified) {mismatch_reason();return false;}
    *out=verified->arena;
    g_arena_error="ok";
    if(out->image_start<0x8c800000u)return true;
    /* D6600 upper 8 MiB: reject 8 MiB mirroring, preserving both words.
     * Probe through P2 and flush any cached low aliases before the write. */
    cpu_atomic_start();
    bool valid=true;
    for(uint32_t p=out->image_start;p<out->stack_end;p+=4096u) {
        uint32_t low=p-0x800000u;
        __asm__ volatile("ocbp @%0"::"r"(low):"memory");
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
        __asm__ volatile("synco":::"memory");
        volatile uint32_t *hi=(volatile uint32_t *)(uintptr_t)(p|0x20000000u);
        volatile uint32_t *lo=(volatile uint32_t *)(uintptr_t)(low|0x20000000u);
        uint32_t old_hi=*hi,old_lo=*lo;
        *hi=~old_lo;
        __asm__ volatile("synco":::"memory");
        if(*hi!=~old_lo || *lo!=old_lo)valid=false;
        *hi=old_hi;
        *lo=old_lo;
        __asm__ volatile("synco":::"memory");
        if(!valid)break;
    }
    cpu_atomic_end();
    if(!valid) {
        g_arena_error="upper P1 SRAM aliases lower memory";
        return false;
    }
    for(uint32_t p=out->image_start;p<out->stack_end;p+=32u)
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
    __asm__ volatile("synco":::"memory");
    for(uint32_t p=out->image_start;p<out->stack_end;p+=4u)
        if(*(volatile uint32_t *)(uintptr_t)(p|0x20000000u)) {
            g_arena_error="upper P1 SRAM not clear";
            return false;
        }
    return true;
}
void ntos_exword_arena_release(const NtosExwordArena &a)
{
    if(a.image_start<0x8c800000u)return;
    for(uint32_t p=a.image_start;p<a.stack_end;p+=32u)
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
    __asm__ volatile("synco":::"memory");
    volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)(a.image_start|0x20000000u);
    for(uint32_t n=0;n<(a.stack_end-a.image_start)/4u;++n)p[n]=0u;
    __asm__ volatile("synco":::"memory");
}
