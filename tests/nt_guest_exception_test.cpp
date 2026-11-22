#include "../src/nt/guest_exception.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
static bool executable(void *,uint32_t pc) {
    return pc>=0x00401000u && pc<0x00402000u && !(pc&1u);
}
int main() {
    nt::GuestException g{};
    uint32_t target=99u;
    assert(g.arm(0x00405001u,0x0C0u,0x00401080u,
                 &executable,nullptr)==nt::SUCCESS);
    assert(g.arm(0x00405001u,0x0C0u,0x00401080u,
                 &executable,nullptr)==nt::INVALID_PARAMETER);
    assert(!g.decide(0x00405001u,0x0A0u,0x00401020u,
                     0x04001001u,&executable,nullptr,&target));
    assert(!target && g.armed && g.forwarded==1u);
    assert(!g.decide(0x00405003u,0x0C0u,0x00401020u,
                     0x04001001u,&executable,nullptr,&target)); // same-page, wrong byte
    assert(!g.decide(0x00407000u,0x0C0u,0x00401020u,
                     0x04001001u,&executable,nullptr,&target));
    assert(!g.decide(0x00405000u,0x0C0u,0x00401020u,
                     0x40010001u,&executable,nullptr,&target));
    assert(!g.decide(0x00405000u,0x0C0u,0x12340020u,
                     0x04001001u,&executable,nullptr,&target));
    assert(g.decide(0x00405001u,0x0C0u,0x00401020u,
                    0x04001001u,&executable,nullptr,&target));
    assert(target==0x00401080u && g.accepted==1u && !g.armed);
    assert(g.last_status==nt::ACCESS_VIOLATION &&
           g.last_tea==0x00405001u &&
           g.last_event==nt::SH4_PROTECTION_WRITE);
    assert(!g.decide(0x00405000u,0x0C0u,0x00401020u,
                     0x04001001u,&executable,nullptr,&target));
    assert(!target);
    assert(g.arm(0x00406000u,nt::SH4_PROTECTION_READ,0x00401080u,
                 &executable,nullptr)==nt::SUCCESS);
    assert(g.decide(0x00406000u,nt::SH4_PROTECTION_READ,0x00401022u,
                    0x04001001u,&executable,nullptr,&target));
    assert(g.accepted==2u && g.last_status==nt::ACCESS_VIOLATION);
    assert(g.arm(0x00405000u,0x0C0u,0x70000000u,
                 &executable,nullptr)==nt::INVALID_PARAMETER);
    g.cancel();
    // Real NTOS foreground PE execution is currently privileged P0.
    // It must recover its OWN pre-armed RO write fault but never accept
    // a wrong-mode event for an unrelated host task.
    g.reset();
    assert(g.arm(0x00405000u,nt::SH4_PROTECTION_WRITE,0x00401080u,
                 &executable,nullptr,true)==nt::SUCCESS);
    assert(!g.decide(0x00405000u,0x0C0u,0x00401020u,
                     0x04001001u,&executable,nullptr,&target));
    assert(g.armed);
    assert(g.decide(0x00405000u,0x0C0u,0x00401020u,
                    0x40010001u,&executable,nullptr,&target));
    assert(!g.armed && target==0x00401080u && g.accepted==1u);
    // Updated MMGUARD uses the physically proven *user-origin* mechanism
    // from MMREAD: a genuine 0x0C0 must NEVER accept privileged SSR.MD=1.
    g.reset();
    assert(g.arm(0x00405000u,nt::SH4_PROTECTION_WRITE,0x00401080u,
                 &executable,nullptr,false)==nt::SUCCESS);
    assert(!g.decide(0x00405000u,0x0C0u,0x00401020u,
                     0x40010001u,&executable,nullptr,&target));
    assert(g.armed && !target);
    assert(g.decide(0x00405000u,0x0C0u,0x00401020u,
                    0x00010001u,&executable,nullptr,&target));
    assert(!g.armed && target==0x00401080u && g.accepted==1u);
    // Renesas SH4A TRAPA is a processing-completion exception and
    // saves SPC=trap instruction + 2, with TRA=imm*4.
    constexpr uint32_t trap_next=0x00401080u;
    assert(!nt::user_probe_return_allowed(true,0x160u,0x7bu*4u,
           trap_next,0x40000000u,trap_next,0x40010001u));
    assert(!nt::user_probe_return_allowed(true,0x160u,0x7bu*4u,
           trap_next+2u,0x00000000u,trap_next,0x40010001u));
    assert(!nt::user_probe_return_allowed(true,0x160u,0x79u*4u,
           trap_next,0x00000000u,trap_next,0x40010001u));
    assert(!nt::user_probe_return_allowed(true,0x160u,0x7bu*4u,
           trap_next,0x00000000u,trap_next,0x00010001u));
    assert(!nt::user_probe_return_allowed(false,0x160u,0x7bu*4u,
           trap_next,0x00000000u,trap_next,0x40010001u));
    assert(nt::user_probe_return_allowed(true,0x160u,0x7bu*4u,
           trap_next,0x00000000u,trap_next,0x40010001u));
    std::puts("PASS guest 0xA0/0xC0: user READ/WRITE mode gating, exact TEA/SPC, and one-shot TRAPA supervisor return");
}
