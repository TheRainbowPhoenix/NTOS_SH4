/* ROM-verified experimental P1 executable arena for EX-word.
 * Do not treat the shared firmware scratch syscall as an allocation.
 */
#pragma once
#include <stdint.h>
struct NtosExwordArena {
    uint32_t image_start,image_end,stack_start,stack_end;
    const char *firmware;
};
using NtosRomRead=bool (*)(void *,uint32_t,uint8_t *,unsigned);
struct NtosRomSignature {uint32_t addr; uint8_t data[16];};
struct NtosRomProfile {
    NtosExwordArena arena;
    NtosRomSignature checks[3];
};
extern const NtosRomProfile ntos_rom_profiles[2];
inline const NtosRomProfile *ntos_rom_match(NtosRomRead read,void *ctx)
{
    if(!read)return nullptr;
    for(const NtosRomProfile &rom:ntos_rom_profiles) {
        bool okay=true;
        for(const NtosRomSignature &sig:rom.checks) {
            uint8_t got[16]{};
            if(!read(ctx,sig.addr,got,16u)){okay=false;break;}
            for(unsigned i=0;i<16u;++i)
                if(got[i]!=sig.data[i]){okay=false;break;}
            if(!okay)break;
        }
        if(okay)return &rom;
    }
    return nullptr;
}
bool ntos_exword_arena_check(NtosExwordArena *out);
/* Distinguish ROM-identity failures from unavailable/aliased P1 memory. */
const char *ntos_exword_arena_error(void);
void ntos_exword_arena_release(const NtosExwordArena &area);
