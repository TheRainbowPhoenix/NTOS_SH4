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
static bool read_rom(void *,uint32_t addr,uint8_t *out,unsigned len)
{
    const volatile uint8_t *src=(const volatile uint8_t *)(uintptr_t)addr;
    for(unsigned i=0;i<len;++i)out[i]=src[i];
    return true;
}
bool ntos_exword_arena_check(NtosExwordArena *out)
{
    if(!out)return false;
    const NtosRomProfile *rom=ntos_rom_match(read_rom,nullptr);
    if(!rom)return false;
    *out=rom->arena;
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
    if(!valid)return false;
    for(uint32_t p=out->image_start;p<out->stack_end;p+=32u)
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
    __asm__ volatile("synco":::"memory");
    for(uint32_t p=out->image_start;p<out->stack_end;p+=4u)
        if(*(volatile uint32_t *)(uintptr_t)(p|0x20000000u))return false;
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
