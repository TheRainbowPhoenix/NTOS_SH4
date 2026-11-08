// End-to-end verification of the file-backed page materializer against
// the actual built SH4BE PE DLL/EXE section bytes.
#include "../src/pe_pager.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
struct Blob {
    std::vector<uint8_t> contents;
    static bool read(void *ctx,uint32_t offset,uint8_t *dst,uint32_t n) {
        const Blob &x=*static_cast<Blob *>(ctx);
        if(offset>x.contents.size() || n>x.contents.size()-offset)
            return false;
        std::memcpy(dst,x.contents.data()+offset,n);
        return true;
    }
    explicit Blob(const char *path) {
        FILE *f=std::fopen(path,"rb");assert(f);
        assert(std::fseek(f,0,SEEK_END)==0);
        const long size=std::ftell(f); assert(size>0);
        std::rewind(f);
        contents.resize(static_cast<size_t>(size));
        assert(std::fread(contents.data(),1,contents.size(),f)==contents.size());
        std::fclose(f);
    }
};
int main(int argc,char **argv) {
    assert(argc>=3);
    for(int arg=1;arg<argc;++arg) {
        Blob file(argv[arg]);pe::Image hdr{};
        assert(pe::parse(file.contents.data(),
                static_cast<uint32_t>(file.contents.size()),&hdr)==0);
        const uint32_t size=hdr.oh.size_of_image;
        assert(size>0 && size<4*1024*1024);
        const uint32_t pages=(size+4095u)>>12;
        std::vector<uint8_t> staged(static_cast<size_t>(pages)*4096u);
        std::vector<uint8_t> expected(staged.size());
        for(uint32_t i=0;i<hdr.section_count;++i) {
            const auto &section=hdr.sections[i];
            if(section.raw_size==0u)continue;
            assert(section.rva+section.raw_size<=staged.size());
            assert(section.raw_offset+section.raw_size<=file.contents.size());
            std::memcpy(expected.data()+section.rva,
                file.contents.data()+section.raw_offset,section.raw_size);
        }
        ntos_pager::Source source{&hdr,&file,&Blob::read,
            static_cast<uint32_t>(file.contents.size()),hdr.oh.image_base,true};
        const char *error=nullptr;
        for(uint32_t i=0;i<pages;++i)
            assert(ntos_pager::materialize(source,i,
                     staged.data()+i*4096u,&error));
        // Compare every loaded section and BSS, not the PE header region:
        // the old loader didn't copy MZ/PE headers into its mapped page.
        for(uint32_t i=0;i<hdr.section_count;++i) {
            const auto &sec=hdr.sections[i];
            uint32_t span=sec.vsize>sec.raw_size?sec.vsize:sec.raw_size;
            assert(sec.rva+span<=staged.size());
            assert(std::memcmp(staged.data()+sec.rva,
                               expected.data()+sec.rva,span)==0);
        }
        std::printf("PASS page-in parity: %s (%u pages, %u sections)\n",
            argv[arg],pages,hdr.section_count);
    }
    return 0;
}
