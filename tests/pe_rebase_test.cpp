// Host regression test for the exact NTOS on-device relocation and UTLB
// placement logic. Accepts shipping PE images generated in dist/.
#include "../src/pe_rebase.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

struct TestImage {
    pe::Image hdr{};
    std::vector<uint8_t> file;
    std::vector<uint8_t> memory;
    explicit TestImage(const char *path) {
        FILE *fp=fopen(path,"rb");
        assert(fp);
        assert(fseek(fp,0,SEEK_END)==0);
        const long length=ftell(fp);
        assert(length>0);
        rewind(fp);
        file.resize(static_cast<size_t>(length));
        assert(fread(file.data(),1,file.size(),fp)==file.size());
        fclose(fp);
        assert(pe::parse(file.data(),static_cast<uint32_t>(file.size()),
                         &hdr)==0);
        memory.resize(hdr.oh.size_of_image);
        assert(!memory.empty());
        for(uint32_t i=0;i<hdr.section_count;++i) {
            auto sh=hdr.sections[i];
            if(sh.raw_size) {
                assert(sh.raw_offset<=file.size());
                assert(sh.raw_size<=file.size()-sh.raw_offset);
                assert(sh.rva<=memory.size());
                assert(sh.raw_size<=memory.size()-sh.rva);
                memcpy(memory.data()+sh.rva,file.data()+sh.raw_offset,
                       sh.raw_size);
            }
        }
    }
};

static uint32_t first_fixup(const TestImage &im) {
    const auto &h=im.hdr.oh;
    assert(h.basereloc_rva && h.basereloc_size>8);
    uint32_t off=h.basereloc_rva,end=off+h.basereloc_size;
    while(off<end) {
        uint32_t page=pe::rd32le(im.memory.data()+off);
        uint32_t size=pe::rd32le(im.memory.data()+off+4);
        assert(size>=8 && size<=end-off);
        for(uint32_t i=8;i<size;i+=2) {
            uint16_t e=pe::rd16le(im.memory.data()+off+i);
            if((e>>12)==pe::BRELOC_HIGHLOW)
                return page+(e&4095u);
        }
        off+=size;
    }
    assert(false && "missing HIGHLOW fixups");return 0;
}
int main(int argc,char **argv) {
    assert(argc==4);
    TestImage core(argv[1]),xaml(argv[2]),exe(argv[3]);
    using namespace ntos_rebase;
    assert(core.hdr.oh.image_base==0x00800000u);
    assert(xaml.hdr.oh.image_base==0x00C00000u);
    assert(exe.hdr.oh.image_base==0x00400000u);
    Region used[3]={};
    unsigned count=0;
    uint32_t base=0,corebase=0,selected=0;
    assert(choose_base(exe.hdr.oh.image_base,
                       (uint32_t)(exe.memory.size()+4095u)>>12,
                       used,count,&base));
    used[count++]={base,(uint32_t)(exe.memory.size()+4095u)>>12};
    assert(choose_base(core.hdr.oh.image_base,
                       (uint32_t)(core.memory.size()+4095u)>>12,
                       used,count,&corebase));
    used[count++]={corebase,(uint32_t)(core.memory.size()+4095u)>>12};
    assert(corebase==0x00800000u);

    // Simulate two DLLs genuinely linked at the SAME preferred ImageBase:
    // relocate a copy from XAML's original 0xC00000 to 0x800000, change
    // the simulated PE preferred header to 0x800000, then ask NTOS to
    // load it with COREDLL already owning the address range.
    uint32_t fixup=first_fixup(xaml);
    const uint32_t before=pe::rd32be(xaml.memory.data()+fixup);
    const char *error=nullptr;
    assert(apply(xaml.hdr,xaml.memory.data(),
                 (uint32_t)xaml.memory.size(),0x00800000u,&error));
    assert(pe::rd32be(xaml.memory.data()+fixup)
           ==before+(0x00800000u-0x00C00000u));
    pe::Image same_base=xaml.hdr;
    same_base.oh.image_base=0x00800000u;
    const uint32_t pages=(uint32_t)(xaml.memory.size()+4095u)>>12;
    assert(choose_base(same_base.oh.image_base,pages,used,count,&selected));
    assert(selected!=corebase);
    assert(!overlap(selected,pages,corebase,used[1].pages));
    used[count++]={selected,pages};
    assert(fits(selected,pages,used,count-1));
    assert(apply(same_base,xaml.memory.data(),
                 (uint32_t)xaml.memory.size(),selected,&error));
    assert(pe::rd32be(xaml.memory.data()+fixup)
           ==before+(selected-0x00C00000u));
    printf("PASS DLL conflict: COREDLL=%08x XAMLUI relocated=%08x\n",
           corebase,selected);

    // Relocated import thunks must point to the ACTUAL module base, not
    // its preferred ImageBase or the physical P1 heap backing address.
    uint32_t fill_rva=0;
    assert(pe::export_lookup(core.hdr,core.memory.data(),
                             (uint32_t)core.memory.size(),
                             "FillRect",&fill_rva));
    uint8_t iat[4]={};
    pe::wr32be(iat,corebase+fill_rva);
    assert(pe::rd32be(iat)==corebase+fill_rva);
    printf("PASS import resolution: COREDLL!FillRect=%08x\n",
           pe::rd32be(iat));

    // Invalid or absent relocation metadata must never allow rebasing.
    auto bad=xaml.memory;
    auto meta=same_base;
    meta.oh.basereloc_size=0;
    assert(!apply(meta,bad.data(),(uint32_t)bad.size(),
                  selected+PAGE,&error));
    assert(error!=nullptr);
    meta=same_base;
    uint32_t reloc=meta.oh.basereloc_rva;
    bad=xaml.memory;
    // First relocation block size is LE, and must be validated.
    bad[reloc+4]=1;bad[reloc+5]=0;
    bad[reloc+6]=0;bad[reloc+7]=0;
    assert(!apply(meta,bad.data(),(uint32_t)bad.size(),
                  selected+PAGE,&error));
    assert(!fits(corebase,used[1].pages,used,count));
    assert(!fits(0,1,used,count));
    assert(!fits(API_BASE,1,used,count));
    printf("PASS malformed PE relocation rejection and UTLB set allocation\n");
    return 0;
}
