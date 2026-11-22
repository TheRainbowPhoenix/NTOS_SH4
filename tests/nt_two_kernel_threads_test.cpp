#include "../src/nt/ke.hpp"
#include "../src/nt/scheduler.hpp"
#include <cassert>
#include <cstdio>
int main() {
    nt::KernelContext k{};
    assert(k.begin(0x00400000u,0x40000u)==nt::SUCCESS);
    assert(k.enter_guest(0x00400200u)==nt::SUCCESS);
    const uint32_t pid=k.process.unique_process_id;
    const uint32_t t0=k.thread.unique_thread_id,t1=t0+1u;
    nt::CooperativeScheduler<2> q{};
    nt::SH4Context frames[2]{};
    frames[0].complete=frames[1].complete=true;
    for(unsigned j=0u;j<2u;++j) {
        for(unsigned reg=0u;reg<16u;++reg)frames[j].gpr[reg]=0x100000u*j+reg;
        for(unsigned reg=0u;reg<8u;++reg) {
            frames[j].bank0[reg]=0x10000u*j+reg;
            frames[j].bank1[reg]=0x20000u*j+reg;
        }
        frames[j].dsp.a[0]=0xAAAA0000u+j;
        frames[j].dsp.x[1]=0xBBBB0000u+j;
        frames[j].dsp.dsr=0x11110000u+j;
        frames[j].spc=0x00400200u+32u*j;
        frames[j].pr=0x00400210u+32u*j;
        frames[j].gpr[15]=0x00420000u+0x1000u*j;
    }
    assert(q.register_thread(0u,pid,t0,frames[0])==nt::SUCCESS);
    assert(q.register_thread(1u,pid,t1,frames[1])==nt::SUCCESS);
    assert(q.begin(0u)==nt::SUCCESS);
    uint32_t counters[2]{};
    for(unsigned i=0u;i<2000u;++i) {
        const unsigned cur=q.active;
        assert(cur==i%2u);
        ++counters[cur];++frames[cur].gpr[0];
        ++frames[cur].dsp.m[1];
        unsigned next=2u;nt::SH4Context resume{};
        assert(q.yield(frames[cur],&next,&resume)==nt::SUCCESS);
        assert(next==(cur^1u));
        assert(resume.gpr[15]==frames[next].gpr[15]);
        assert(resume.spc==frames[next].spc);
        assert(resume.pr==frames[next].pr);
        assert(resume.dsp.a[0]==frames[next].dsp.a[0]);
        assert(resume.dsp.dsr==frames[next].dsp.dsr);
        assert(resume.bank0[7]==frames[next].bank0[7]);
        assert(resume.bank1[7]==frames[next].bank1[7]);
    }
    assert(counters[0]==1000u && counters[1]==1000u);
    assert(q.stop(1u)==nt::SUCCESS);
    unsigned next=2u;nt::SH4Context resume{};
    assert(q.yield(frames[0],&next,&resume)==nt::SUCCESS && next==0u);
    k.end();
    std::puts("PASS Ke host two KTHREADs: 1000 round-robin yields each; no-FPU banked/DSP/context preservation");
}
