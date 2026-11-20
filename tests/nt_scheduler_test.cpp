#include "../src/nt/scheduler.hpp"
#include <cassert>
#include <cstdio>
static bool ack(void *,uint32_t,uint32_t) {return true;}
int main() {
    nt::CooperativeScheduler<2> s{};
    nt::SH4Context a{},b{};a.complete=b.complete=true;
    a.gpr[15]=0x8C100000u;a.bank0[4]=0x1234u;
    a.dsp.x[0]=0xFACEu;a.spc=0x401000u;a.pr=0x401100u;
    b.gpr[15]=0x8C200000u;b.bank1[4]=0x5678u;
    b.dsp.x[0]=0xBEEFu;b.spc=0x501000u;b.dsp.dsr=0x40001u;
    assert(s.register_thread(0,257u,65537u,a)==nt::SUCCESS);
    assert(s.register_thread(1,258u,65538u,b)==nt::SUCCESS);
    assert(s.register_thread(1,258u,65539u,b)==nt::INVALID_PARAMETER);
    assert(s.begin(0)==nt::SUCCESS);
    unsigned chosen=99u;nt::SH4Context out{};
    assert(s.yield(a,&chosen,&out,&ack,nullptr)==nt::SUCCESS);
    assert(chosen==1u && out.gpr[15]==b.gpr[15] &&
           out.bank1[4]==b.bank1[4] && out.dsp.x[0]==b.dsp.x[0] &&
           out.dsp.dsr==b.dsp.dsr);
    b.gpr[0]=0xCAFEu;
    assert(s.yield(b,&chosen,&out,&ack,nullptr)==nt::SUCCESS);
    assert(chosen==0u && out.bank0[4]==a.bank0[4] &&
           out.pr==a.pr && out.dsp.x[0]==a.dsp.x[0]);
    assert(s.yield(a,&chosen,&out,&ack,nullptr)==nt::SUCCESS);
    assert(chosen==1u && out.gpr[0]==0xCAFEu);
    assert(s.stop(0)==nt::SUCCESS);
    assert(s.yield(b,&chosen,&out,&ack,nullptr)==nt::SUCCESS && chosen==1u);
    std::puts("PASS scheduler preflight: two process/thread identities, banked+DSP+stack context, cooperative yield, stop");
}
