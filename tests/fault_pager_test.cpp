// Model-check RAM-backed hardware UTLB miss policy for arbitrary-size
// virtual PE DLL graphs, including 12 DLLs and >55 pages in one module.
#include "../src/fault_pager.hpp"
#include <cassert>
#include <cstdio>
struct Sink {
    unsigned installations=0;
    unsigned last_slot=0,last_way=0;
};
static bool install(void *ctx,uint32_t va,uint32_t pa,
                    unsigned slot,unsigned way) {
    Sink *s=static_cast<Sink *>(ctx);
    assert(va>=0x01000000u && pa>=0x00100000u);
    assert(slot<ntos_fault::SLOTS && way<4u);
    ++s->installations;s->last_slot=slot;s->last_way=way;
    return true;
}
int main() {
    ntos_fault::Pager page{};
    Sink sink{};
    // API window uses dedicated UTLB slots 55..58 outside Pager::entries.
    assert(page.add(0x01000000u,0x00100000u,512u)); // 2MB DLL
    assert(page.add(0x02000000u,0x00300000u,512u)); // second 2MB DLL
    for(unsigned m=0;m<12;++m)
        assert(page.add(0x03000000u+m*0x10000u,
                        0x00600000u+m*0x10000u,8u));
    assert(page.modules_used==14u);
    uint32_t pa=0;
    assert(page.backing(0x010a1f20u,&pa));
    assert(pa==0x001a1000u);
    assert(!page.backing(0x08000000u,&pa));
    assert(page.fault(0x01000000u,install,&sink,true));
    assert(page.lookup(0x01000000u)>=0);
    for(uint32_t p=1;p<512u;++p)
        assert(page.fault(0x01000000u+p*4096u,install,&sink));
    assert(page.misses==512u);
    assert(page.evictions>400u);
    assert(page.lookup(0x01000000u)>=0); // pinned entry protected
    // A spontaneous hardware eviction of a pinned mapping must be
    // repairable without a new logical allocation or eviction.
    const unsigned before_refault=sink.installations;
    const uint32_t before_misses=page.misses;
    assert(page.fault(0x01000000u,install,&sink,false,true));
    assert(sink.installations==before_refault+1u);
    assert(page.misses==before_misses);
    // An ordinary explicit hint on the same VPN is still a no-op.
    assert(page.fault(0x01000000u,install,&sink));
    assert(sink.installations==before_refault+1u);
    for(uint32_t p=0;p<512u;++p)
        assert(page.fault(0x02000000u+p*4096u,install,&sink));
    assert(page.lookup(0x01000000u)>=0);
    for(unsigned m=0;m<12;++m)
        for(unsigned p=0;p<8;++p)
            assert(page.fault(0x03000000u+m*0x10000u+p*4096u,
                              install,&sink));
    // SH7305 slots are fully associative: repeated VPNs must not occupy
    // multiple slots; there is no fabricated 32-set / four-way limit.
    for(unsigned i=0;i<ntos_fault::SLOTS;++i) {
        const auto &e=page.entries[i];
        if(!e.present)continue;
        assert(e.va%ntos_fault::PAGE==0u);
        for(unsigned j=0;j<i;++j)
            assert(!page.entries[j].present || page.entries[j].va!=e.va);
    }
    assert(sink.installations==page.misses+1u);
    assert(!page.servicing);
    assert(!page.fault(0x08000000u,install,&sink));
    assert(!page.fault(0x01000000u,nullptr,&sink));
    // Regression: 55 virtual pages with identical VPN[16:12] must still
    // fit 55 independent SH7305 UTLB slots. A fake 4-way TLB fails this.
    ntos_fault::Pager associative{};
    Sink same_set{};
    assert(associative.add(0x04000000u,0x01000000u,2048u));
    for(unsigned i=0;i<55u;++i) {
        const uint32_t va=0x04000000u+i*0x20000u;
        assert(associative.fault(va,install,&same_set,true));
    }
    assert(same_set.installations==55u);
    assert(associative.evictions==0u);
    assert(!associative.fault(0x04700000u,install,&same_set));
    ntos_rebase::Region regions[16]={};
    regions[0]={0x00400000u,1u};
    uint32_t base=0;
    assert(ntos_fault::choose_virtual(0x10000000u,902u,regions,1,&base));
    assert(base==0x01000000u);
    regions[1]={base,902u};
    assert(ntos_fault::choose_virtual(0x10000000u,902u,regions,2,&base));
    assert(base!=regions[1].base);
    std::printf("PASS 14-module fault graph, 1024 resident faults + "
                "96 stress pages, %u evictions\n",page.evictions);
    std::puts("PASS 55-slot fully associative LRU, pinned pages and VA allocation");
    return 0;
}
