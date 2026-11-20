// CPU-independent contract preflight only: physical SH7305 instruction
// cache / P4 MMU behavior must be checked by PSCODE.EXE on hardware.
#include "../src/fault_pager.hpp"
#include <cassert>
#include <cstdio>
struct FakeP4 {
    struct Line {uint32_t va=0u,pa=0u;bool valid=false;};
    Line tlb[ntos_fault::SLOTS]{};
    bool allow=true;
    unsigned invalidations=0u;
    static bool install(void *ctx,uint32_t va,uint32_t pa,
                        unsigned slot,unsigned rights) {
        auto &cpu=*static_cast<FakeP4 *>(ctx);
        if(!cpu.allow || slot>=ntos_fault::SLOTS || rights>2u)
            return false;
        cpu.tlb[slot]={va,pa,true};
        return true;
    }
    static bool invalidate(void *ctx,unsigned slot,uint32_t va,uint32_t pa) {
        auto &cpu=*static_cast<FakeP4 *>(ctx);
        if(!cpu.allow || slot>=ntos_fault::SLOTS)return false;
        const Line &entry=cpu.tlb[slot];
        if(!entry.valid || entry.va!=va || entry.pa!=pa)return false;
        cpu.tlb[slot]={};
        ++cpu.invalidations;
        return true;
    }
};
int main() {
    constexpr uint32_t code=0x00600000u,data=0x00700000u;
    constexpr uint32_t code_a=0x0A000000u,code_b=0x0A100000u;
    constexpr uint32_t data_a=0x0B000000u,data_b=0x0B100000u;
    ntos_fault::Pager pager{};
    FakeP4 cpu{};
    assert(pager.add(code,code_a,2u));
    assert(pager.add(data,data_a,1u));
    assert(!pager.add(code+4096u,0x0C000000u,1u));
    for(unsigned p=0u;p<2u;++p)
        assert(pager.fault(code+p*4096u,&FakeP4::install,&cpu,true));
    assert(pager.fault(data,&FakeP4::install,&cpu,true));
    uint32_t private_data[2]{};
    unsigned active=0u;
    for(unsigned n=0u;n<1000u;++n) {
        const unsigned next=active^1u;
        const uint32_t old_pa=active?code_b:code_a;
        const uint32_t new_pa=next?code_b:code_a;
        if(n==57u) {
            cpu.allow=false;
            assert(!pager.invalidate(code,&FakeP4::invalidate,&cpu));
            assert(pager.modules[0].pa==old_pa && active==(n&1u?1u:0u));
            cpu.allow=true;
        }
        for(unsigned p=0u;p<2u;++p)
            assert(pager.invalidate(code+p*4096u,
                                    &FakeP4::invalidate,&cpu));
        assert(pager.invalidate(data,&FakeP4::invalidate,&cpu));
        pager.modules[0].pa=new_pa;
        pager.modules[1].pa=next?data_b:data_a;
        for(unsigned p=0u;p<2u;++p)
            assert(pager.fault(code+p*4096u,&FakeP4::install,&cpu,true));
        assert(pager.fault(data,&FakeP4::install,&cpu,true));
        uint32_t pa=0u;
        for(unsigned p=0u;p<2u;++p) {
            assert(pager.backing(code+p*4096u,&pa));
            assert(pa==new_pa+p*4096u);
            const int slot=pager.lookup(code+p*4096u);
            assert(slot>=0 && cpu.tlb[slot].valid &&
                   cpu.tlb[slot].pa==pa);
        }
        assert(pager.backing(data,&pa) &&
               pa==(next?data_b:data_a));
        ++private_data[next];
        active=next;
    }
    assert(active==0u && private_data[0]==500u &&
           private_data[1]==500u &&
           cpu.invalidations==3000u);
    std::puts("PASS Ps EXEC root host: 1000 paired code/data root changes, same VA different PA, pinned mappings, fail-closed P4 refusal");
}
