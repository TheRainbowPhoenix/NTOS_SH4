#include "../src/hal/sh7305_tmu_snapshot.hpp"
#include <cassert>
#include <cstdio>
struct Bus {
    unsigned reads=0u;
    uint32_t addresses[5]{};
    unsigned widths[5]{};
    static uint32_t load(void *opaque,uint32_t address,unsigned width) {
        auto &b=*static_cast<Bus *>(opaque);
        assert(b.reads<5u);
        b.addresses[b.reads]=address;
        b.widths[b.reads]=width;
        ++b.reads;
        switch(address) {
        case ntos_hal::TMU_TSTR:return 5u;
        case ntos_hal::TMU_TCOR0:return 1000u;
        case ntos_hal::TMU_TCNT0:return 319u;
        case ntos_hal::TMU_TCR0:return 0x0123u;
        case ntos_hal::INTC_IPRA:return 0xDB90u;
        }assert(false);return 0u;
    }
};
int main() {
    using namespace ntos_hal;
    Bus bus{};
    TmuSnapshot s{};
    assert(tmu_snapshot(&s,sizeof(s)-4u,TMU_SNAPSHOT_IOCTL,
                        &Bus::load,&bus)==nt::INVALID_PARAMETER);
    assert(tmu_snapshot(&s,sizeof(s),0xFFFFFFFFu,
                        &Bus::load,&bus)==nt::NOT_SUPPORTED);
    assert(tmu_snapshot(&s,sizeof(s),TMU_SNAPSHOT_IOCTL,
                        nullptr,&bus)==nt::NOT_SUPPORTED);
    assert(bus.reads==0u);
    assert(tmu_snapshot(&s,sizeof(s),TMU_SNAPSHOT_IOCTL,
                        &Bus::load,&bus)==nt::SUCCESS);
    assert(s.tag==TMU_SNAPSHOT_TAG && s.running==5u &&
           s.period==1000u && s.counter==319u &&
           s.control==0x0123u && s.priority==0xDB90u);
    assert(bus.reads==5u &&
           bus.addresses[0]==TMU_TSTR && bus.widths[0]==1u &&
           bus.addresses[1]==TMU_TCOR0 && bus.widths[1]==4u &&
           bus.addresses[2]==TMU_TCNT0 && bus.widths[2]==4u &&
           bus.addresses[3]==TMU_TCR0 && bus.widths[3]==2u &&
           bus.addresses[4]==INTC_IPRA && bus.widths[4]==2u);
    std::puts("PASS SH7305 native timer: read-only P2 TMU/INTC snapshot, exact widths/addresses, deny untrusted IOCTL");
}
