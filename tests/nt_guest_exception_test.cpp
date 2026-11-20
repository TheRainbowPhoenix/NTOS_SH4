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
    std::puts("PASS guest VBR+0x100 decision: exact 0xA0/0xC0, P0-only, valid executable recovery, one-shot, no arbitrary retry");
}
