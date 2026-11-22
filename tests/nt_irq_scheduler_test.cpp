// Host-only IRQ context and scheduler gate. CPU preemption still needs
// SH7305 +0x600 ISR assembly, a reserved physical timer and RTE proof.
#include "../src/nt/irq_scheduler.hpp"
#include <cassert>
#include <cstdio>
struct Gate {
    bool ack=true;
    uint32_t calls=0u,refusals=0u;
    static bool flush(void *raw,uint32_t old_pid,uint32_t new_pid) {
        auto &g=*static_cast<Gate *>(raw);
        assert(old_pid!=new_pid && old_pid && new_pid);
        ++g.calls;
        if(!g.ack){++g.refusals;return false;}return true;
    }
};
static nt::SH4Context context(uint32_t seed) {
    nt::SH4Context c{};
    c.complete=true;
    c.ssr=0x000000F0u;
    c.spc=0x00401000u+(seed&0xF0u);
    c.sgr=0x0071FFF0u;
    for(unsigned i=0u;i<16u;++i)c.gpr[i]=seed+i;
    for(unsigned i=0u;i<8u;++i) {
        c.bank0[i]=seed+i;
        c.bank1[i]=seed+i+0x80u;
    }
    c.dsp.x[0]=seed;c.dsp.x[1]=seed+1u;
    c.dsp.y[0]=seed+2u;c.dsp.y[1]=seed+3u;
    c.dsp.a[0]=seed+4u;c.dsp.a[1]=seed+5u;
    c.dsp.dsr=seed+6u;c.dsp.mod=seed+7u;
    return c;
}
int main() {
    nt::IrqScheduler<2> irq{};
    auto a=context(0xAA00u),b=context(0xBB00u);
    assert(irq.ke.register_thread(0u,257u,65537u,a)==nt::SUCCESS);
    assert(irq.ke.register_thread(1u,258u,65538u,b)==nt::SUCCESS);
    assert(irq.ke.begin(0u)==nt::SUCCESS);
    assert(irq.arm(0x400u,1u)==nt::SUCCESS);
    assert(irq.arm(0x400u,1u)==nt::INVALID_PARAMETER);
    unsigned next=3u;nt::SH4Context resume{};Gate gate{};
    assert(irq.timer_irq(0x401u,a,&next,&resume,&Gate::flush,&gate)==nt::INVALID_PARAMETER);
    auto forged=a;forged.ssr=0x400000F0u;
    assert(irq.timer_irq(0x400u,forged,&next,&resume,&Gate::flush,&gate)==nt::INVALID_PARAMETER);
    forged=a;forged.spc=0x80001000u;
    assert(irq.timer_irq(0x400u,forged,&next,&resume,&Gate::flush,&gate)==nt::INVALID_PARAMETER);
    irq.nested=true;
    assert(irq.timer_irq(0x400u,a,&next,&resume,&Gate::flush,&gate)==nt::INVALID_PARAMETER);
    irq.nested=false;
    for(unsigned n=0u;n<1000u;++n) {
        const unsigned current=irq.ke.active;
        const auto saved=context(current?0xBB00u+n:0xAA00u+n);
        if(n==57u) {
            gate.ack=false;
            assert(irq.timer_irq(0x400u,saved,&next,&resume,&Gate::flush,&gate)==nt::DEVICE_BUSY);
            assert(irq.ke.active==current && gate.refusals==1u);
            gate.ack=true;
        }
        assert(irq.timer_irq(0x400u,saved,&next,&resume,&Gate::flush,&gate)==nt::SUCCESS);
        assert(irq.ke.active==(current^1u) && next==(current^1u));
        const auto expected=irq.ke.slots[next].context;
        assert(resume.dsp.x[0]==expected.dsp.x[0] &&
               resume.bank0[3]==expected.bank0[3] &&
               resume.bank1[3]==expected.bank1[3] &&
               resume.ssr==expected.ssr && resume.spc==expected.spc);
    }
    assert(irq.dispatches==1000u && irq.rejected==4u &&
           gate.calls==1001u && gate.refusals==1u);
    std::puts("PASS IRQ scheduler host: 1000 preempt decisions, complete banked+DSP context, wrong event/SSR rejected, nested-safe, fail-closed P4");
}
