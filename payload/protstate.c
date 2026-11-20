/* PROTSTATE.EXE: 100% non-destructive CP400 preflight.
 * Read SR *as the currently executing P0 PE*, not through a P1 API.
 * Expected 0x50520001: SR.MD=1 (privileged), RB=BL=0.
 * 0x50520002: already in user mode, so user-trap bridge must NOT run.
 * No SDK access, no intentional MMU exception, no memory write. */
#include <stdint.h>
int main(void) {
    uint32_t sr=0u;
    __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
    if((sr&0x40000000u)==0u)return 0x50520002u;
    if((sr&0x30000000u)!=0u)return 0xBAD95001u;
    return 0x50520001u;
}
