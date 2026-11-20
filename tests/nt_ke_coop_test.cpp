#include "../src/nt/ke_coop.hpp"
#include <cassert>
#include <cstdio>
int main() {
    nt::KernelContext kernel{};
    assert(kernel.begin(0x00400000u,0x40000u)==nt::SUCCESS);
    assert(kernel.enter_guest(0x00400200u)==nt::SUCCESS);
    nt::KeCoop coop{};
    uint32_t second=0u;
    assert(coop.create(kernel,0x00400300u,0x00420000u,0x40001000u,0u,
                       &second)==nt::INVALID_PARAMETER); // no DSP save yet
    assert(coop.create(kernel,0x00400300u,0x00420000u,0x40000001u,0x1000u,
                       &second)==nt::SUCCESS);
    assert(second==65538u && coop.tid(kernel)==65537u);
    assert(coop.peer.owning_process_id==kernel.process.unique_process_id);
    assert(coop.frames[1].sp==0x00420000u &&
           coop.frames[1].pr==0x00400300u);
    nt::CoopSaved *from=nullptr;const nt::CoopSaved *to=nullptr;
    for(unsigned i=0u;i<2000u;++i) {
        const unsigned current=coop.active;
        assert(coop.next(kernel,&from,&to));
        assert(from==&coop.frames[current] &&
               to==&coop.frames[current^1u]);
        assert(coop.tid(kernel)==(current ? 65537u : 65538u));
    }
    assert(coop.switches==2000u && coop.active==0u);
    assert(coop.next(kernel,&from,&to));
    assert(coop.active==1u);
    assert(coop.exit_peer(kernel,&from,&to));
    assert(coop.exited && coop.active==0u &&
           coop.peer.state==nt::KTHREAD_STATE::terminated);
    assert(!coop.next(kernel,&from,&to));
    kernel.end();
    coop.reset();
    assert(coop.owner_pid==0u && !coop.created);
    std::puts("PASS Ke bridge host: 2 distinct KTHREADs, 1000 alternating yields, DSP guarded, exit+teardown");
}
