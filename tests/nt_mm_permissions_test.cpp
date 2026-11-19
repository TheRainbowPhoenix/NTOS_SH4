// Host-side NT Mm policy tests: section overlaps, no-access gaps,
// 55-slot replacement and RO/RW retention. This cannot simulate a CPU trap.
#include "../src/fault_pager.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>

struct Calls {
    unsigned ro=0u,rw=0u;
    unsigned last_slot=0u;
    uint32_t last_va=0u;
    unsigned last_protection=9u;
};
static bool install(void *context,uint32_t va,uint32_t pa,
                    unsigned slot,unsigned protect) {
    Calls &c=*static_cast<Calls *>(context);
    assert(slot<55u && protect<=1u);
    assert(pa==0x0C000000u+va-0x00400000u);
    c.last_slot=slot;c.last_va=va;c.last_protection=protect;
    if(protect==1u)++c.ro;else ++c.rw;
    return true;
}
static pe::Image image(){
    pe::Image img{};
    img.oh.size_of_image=5u*4096u;
    img.oh.size_of_headers=512u;
    img.section_count=4u;
    // A section can share a 4096-byte hardware page with another section.
    img.sections[0].rva=0x200u;
    img.sections[0].vsize=0xE00u;
    img.sections[0].characteristics=pe::SCN_MEM_READ|pe::SCN_MEM_EXECUTE;
    img.sections[1].rva=0x1000u;
    img.sections[1].vsize=0x1000u;
    img.sections[1].characteristics=pe::SCN_MEM_READ;
    img.sections[2].rva=0x2000u;
    img.sections[2].vsize=0x200u;
    img.sections[2].characteristics=pe::SCN_MEM_READ;
    img.sections[3].rva=0x2200u;
    img.sections[3].vsize=0x500u;
    img.sections[3].characteristics=pe::SCN_MEM_READ|pe::SCN_MEM_WRITE;
    return img;
}
int main(){
    const pe::Image img=image();
    using ntos_mm::Access;
    assert(ntos_mm::pe_page_access(img,0u)==Access::read_only);
    assert(ntos_mm::pe_page_access(img,1u)==Access::read_only);
    assert(ntos_mm::pe_page_access(img,2u)==Access::read_write);
    assert(ntos_mm::pe_page_access(img,3u)==Access::no_access);
    assert(ntos_mm::pe_page_access(img,4u)==Access::no_access);
    assert(ntos_mm::pe_page_access(img,5u)==Access::no_access);
    ntos_fault::Pager p{};
    assert(p.add(0x00400000u,0x0C000000u,5u,&img,true));
    Calls c{};
    assert(p.access(0x00400000u)==Access::read_only);
    assert(p.access(0x00402000u)==Access::read_write);
    assert(p.access(0x00403000u)==Access::no_access);
    assert(p.fault(0x00400000u,install,&c));
    assert(c.ro==1u && c.last_protection==1u);
    assert(p.fault(0x00401000u,install,&c));
    assert(c.ro==2u);
    assert(p.fault(0x00402000u,install,&c));
    assert(c.rw==1u && c.last_protection==0u);
    assert(!p.fault(0x00403000u,install,&c)); // guard gap
    assert(!p.fault(0x70000000u,install,&c)); // unrelated VA
    // Hardware miss must re-install the SAME PR encoding even on a hit.
    assert(p.fault(0x00401000u,install,&c,false,true));
    assert(c.last_protection==1u && c.ro==3u);

    ntos_fault::Pager backwards_compatible{};
    assert(backwards_compatible.add(0x00400000u,0x0C000000u,5u));
    assert(backwards_compatible.fault(0x00403000u,install,&c));
    assert(c.last_protection==0u); // opt-in only, old builds remain RW

    pe::Image big{};
    big.oh.size_of_image=96u*4096u;
    big.oh.size_of_headers=512u;
    big.section_count=2u;
    big.sections[0].rva=0x1000u;
    big.sections[0].vsize=59u*4096u;
    big.sections[0].characteristics=pe::SCN_MEM_READ;
    big.sections[1].rva=60u*4096u;
    big.sections[1].vsize=36u*4096u;
    big.sections[1].characteristics=pe::SCN_MEM_READ|pe::SCN_MEM_WRITE;
    ntos_fault::Pager rotating{};
    assert(rotating.add(0x00400000u,0x0C000000u,96u,&big,true));
    Calls d{};
    for(unsigned i=0;i<96u;++i)
        assert(rotating.fault(0x00400000u+i*4096u,install,&d));
    assert(rotating.evictions==96u-55u);
    assert(d.ro==60u && d.rw==36u);
    for(unsigned i=96u;i>0u;--i)
        assert(rotating.fault(0x00400000u+(i-1u)*4096u,
                              install,&d,false,true));
    assert(d.ro>60u && d.rw>36u);
    assert(rotating.evictions>41u);
    assert(!rotating.add(0x00400000u,0x0C000000u,95u,&big,true));
    std::puts("PASS NT Mm 4KiB PE RO/RW/guard rights, mixed sections and 55-slot PR retention");
}
