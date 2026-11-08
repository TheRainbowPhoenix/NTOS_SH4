// Independent test of the on-demand *file-backed* page loader. No UTLB
// miss handler is enabled; exercise arbitrary pages explicitly on host.
#include "../src/pe_pager.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

struct Backing {
    std::vector<uint8_t> bytes;
    uint32_t reads=0,read_bytes=0;
};
static bool read_at(void *ctx,uint32_t pos,uint8_t *out,uint32_t size) {
    Backing &b=*static_cast<Backing *>(ctx);
    if(pos>b.bytes.size() || size>b.bytes.size()-pos)return false;
    for(uint32_t i=0;i<size;++i)out[i]=b.bytes[pos+i];
    ++b.reads;b.read_bytes+=size;
    return true;
}
static void set32(uint8_t *d,uint32_t v) {
    d[0]=static_cast<uint8_t>(v);d[1]=static_cast<uint8_t>(v>>8);
    d[2]=static_cast<uint8_t>(v>>16);d[3]=static_cast<uint8_t>(v>>24);
}
int main() {
    pe::Image h{};
    h.oh.magic=pe::OPT_MAGIC_PE32;
    h.oh.image_base=0x10000000u;
    h.oh.size_of_image=0x389000u; // 3.5 MiB virtual PE
    h.oh.size_of_headers=0x200u;
    h.oh.basereloc_rva=0x3000u;
    h.oh.basereloc_size=12u;
    h.section_count=2u;
    h.sections[0].rva=0x1000u;
    h.sections[0].vsize=0x800u;
    h.sections[0].raw_size=128u;
    h.sections[0].raw_offset=0x400u;
    h.sections[1].rva=0x3000u;
    h.sections[1].vsize=12u;
    h.sections[1].raw_size=12u;
    h.sections[1].raw_offset=0x600u;
    Backing blob{};
    blob.bytes.resize(0x800u);
    for(unsigned i=0;i<0x200u;++i)blob.bytes[i]=0x77;
    for(unsigned i=0;i<128u;++i)blob.bytes[0x400u+i]=0x11;
    ntos_rebase::store32(blob.bytes.data()+0x410u,0x10001000u,true);
    set32(blob.bytes.data()+0x600u,0x1000u);
    set32(blob.bytes.data()+0x604u,12u);
    blob.bytes[0x608u]=0x10;blob.bytes[0x609u]=0x30;
    ntos_pager::Source source{&h,&blob,read_at,
        static_cast<uint32_t>(blob.bytes.size()),0x01000000u,true};
    uint8_t page[4096];
    const char *error=nullptr;
    assert(ntos_pager::materialize(source,0u,page,&error));
    assert(page[0]==0x77 && page[0x200]==0u);
    assert(ntos_pager::materialize(source,1u,page,&error));
    assert(page[0]==0x11);
    assert(ntos_rebase::load32(page+0x10u,true)==0x01001000u);
    assert(page[200]==0u); // .bss tail and virtual padding
    assert(ntos_pager::materialize(source,0x388u,page,&error));
    assert(page[0]==0u && page[4095]==0u);
    // We generated 3.5 MiB of *virtual* PE with only 2 KiB of
    // file backing and needed no 3.5 MiB in-memory image allocation.
    assert(ntos_pager::materialize(source,3u,page,&error));
    assert(page[0]==0x00u && page[8]==0x10u);
    assert(blob.read_bytes<4096u);
    assert(!ntos_pager::materialize(source,0x389u,page,&error));
    assert(!ntos_pager::materialize(source,0xffffffffu,page,&error));

    // Bad relocation entry must be rejected, without installing the page.
    blob.bytes[0x609u]=0xF0;
    assert(!ntos_pager::materialize(source,1u,page,&error));
    blob.bytes[0x609u]=0x30;

    ntos_pager::PageTable<2> plan{};
    assert(plan.register_page(0x1000u,0x8c100000u,true));
    assert(plan.register_page(0x2000u,0x8c101000u,false));
    assert(plan.lookup(0x1000u)==0);
    plan.pages[1].dirty=true;
    assert(plan.victim()==-1); // dirty and pinned entries must not evict
    plan.pages[1].dirty=false;
    assert(plan.victim()==1);
    assert(plan.register_page(0x3000u,0x8c102000u,false));
    assert(plan.lookup(0x3000u)==1);
    assert(plan.lookup(0x1000u)==0);
    assert(NTOS_ENABLE_TLB_MISS_PAGER==0);
    std::puts("PASS demand-page materializer: 3.5MiB virtual PE from 2KiB backing");
    std::puts("PASS SH4BE HIGHLOW lazy fixups, zero-fill, LRU pin/dirty protections");
    std::puts("PASS hardware TLB fault ownership remains disabled");
    return 0;
}
