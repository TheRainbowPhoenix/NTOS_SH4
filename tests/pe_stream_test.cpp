// Host-only regression for bounded, random-access PE header parsing.
// Run: g++ -std=c++20 -Wall -Wextra -Werror -pedantic tests/pe_stream_test.cpp -o /tmp/pe_stream_test && /tmp/pe_stream_test
#include "../src/pe_stream.hpp"
#include "../src/pe_pager.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

struct File {
    uint8_t bytes[4096]{};
    uint32_t size=sizeof(bytes), calls=0, max_read=0;
    bool short_read=false;
};
static bool read_at(void *opaque,uint32_t pos,uint8_t *out,uint32_t count) {
    auto &f=*static_cast<File*>(opaque);
    ++f.calls;
    if(count>f.max_read)f.max_read=count;
    if(f.short_read || pos>f.size || count>f.size-pos)return false;
    std::memcpy(out,f.bytes+pos,count);
    return true;
}
static void wr16(uint8_t *p,uint16_t v) {
    p[0]=static_cast<uint8_t>(v);p[1]=static_cast<uint8_t>(v>>8);
}
static void wr32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;++i)p[i]=static_cast<uint8_t>(v>>(i*8));
}
static void create_pe(File &f,uint32_t pe_offset=0x80u) {
    f.bytes[0]='M';f.bytes[1]='Z';
    wr32(f.bytes+0x3c,pe_offset);
    uint8_t *p=f.bytes+pe_offset;
    p[0]='P';p[1]='E';
    wr16(p+4,pe::MACHINE_SH4BE);
    wr16(p+6,1);
    wr16(p+20,144);
    wr16(p+22,pe::F_EXECUTABLE_IMAGE);
    uint8_t *o=p+24;
    wr16(o+0,pe::OPT_MAGIC_PE32);
    wr32(o+16,0x200); // entry RVA
    wr32(o+28,0x10000000); // ImageBase
    wr32(o+32,0x200);wr32(o+36,0x200);
    wr32(o+56,0x1000);wr32(o+60,0x200);
    wr16(o+68,pe::SUBSYSTEM_WINDOWS_CE);
    uint8_t *s=o+144;
    std::memcpy(s,".text",5);
    wr32(s+8,0x100);
    wr32(s+12,0x200);
    wr32(s+16,0x200);
    wr32(s+20,0x800); // non-overlapping with PE header
    wr32(s+36,pe::SCN_MEM_READ|pe::SCN_MEM_EXECUTE);
}

// File-backed parity of the new streaming callbacks against the old in-memory
// loader on actual shipped PE binaries (only the *test* stages full files).
struct Disk {
    std::FILE *handle;
    uint32_t length;
};
static bool read_disk(void *opaque,uint32_t offset,uint8_t *out,uint32_t count) {
    auto *d=static_cast<Disk *>(opaque);
    if(offset>d->length || count>d->length-offset)return false;
    if(std::fseek(d->handle,static_cast<long>(offset),SEEK_SET)!=0)return false;
    return std::fread(out,1,count,d->handle)==count;
}
struct Memory {
    const uint8_t *data;
    uint32_t length;
};
static bool read_memory(void *opaque,uint32_t offset,uint8_t *out,uint32_t count) {
    auto *m=static_cast<Memory *>(opaque);
    if(offset>m->length || count>m->length-offset)return false;
    std::memcpy(out,m->data+offset,count);
    return true;
}
static void check_real_pe(const char *path) {
    std::FILE *f=std::fopen(path,"rb");
    assert(f);
    assert(std::fseek(f,0,SEEK_END)==0);
    const long length=std::ftell(f);
    assert(length>=512 && length<=8*1024*1024);
    assert(std::fseek(f,0,SEEK_SET)==0);
    std::vector<uint8_t> contents(static_cast<size_t>(length));
    assert(std::fread(contents.data(),1,contents.size(),f)==contents.size());
    const auto size=static_cast<uint32_t>(length);
    Disk disk{f,size};
    Memory memory{contents.data(),size};
    pe::Image from_buffer{},from_disk{};
    assert(pe::parse(contents.data(),size,&from_buffer)==0);
    assert(pe_stream::parse(read_disk,&disk,size,&from_disk)==0);
    assert(from_buffer.fh.machine==from_disk.fh.machine);
    assert(from_buffer.fh.characteristics==from_disk.fh.characteristics);
    assert(from_buffer.oh.size_of_image==from_disk.oh.size_of_image);
    assert(from_buffer.oh.image_base==from_disk.oh.image_base);
    assert(from_buffer.oh.entry_rva==from_disk.oh.entry_rva);
    assert(from_buffer.section_count==from_disk.section_count);
    for(uint32_t i=0;i<from_disk.section_count;++i) {
        assert(from_buffer.sections[i].rva==from_disk.sections[i].rva);
        assert(from_buffer.sections[i].raw_size==from_disk.sections[i].raw_size);
        assert(from_buffer.sections[i].raw_offset==from_disk.sections[i].raw_offset);
    }
    ntos_pager::Source original{&from_buffer,&memory,read_memory,size,
                                from_buffer.oh.image_base,true};
    ntos_pager::Source streamed{&from_disk,&disk,read_disk,size,
                                from_disk.oh.image_base,true};
    uint8_t a[4096]{},b[4096]{};
    const uint32_t pages=(from_disk.oh.size_of_image+4095u)/4096u;
    for(uint32_t index: {0u,pages/2u,pages-1u}) {
        const char *err=nullptr;
        assert(ntos_pager::materialize(original,index,a,&err));
        assert(ntos_pager::materialize(streamed,index,b,&err));
        assert(std::memcmp(a,b,sizeof(a))==0);
    }
    assert(std::fclose(f)==0);
}
int main(int argc,char **argv) {
    File f{};
    pe::Image image{};
    create_pe(f);
    assert(pe_stream::parse(read_at,&f,f.size,&image)==0);
    assert(image.oh.entry_rva==0x200u && image.oh.image_base==0x10000000u);
    assert(image.sections[0].raw_offset==0x800u);
    assert(f.max_read<=1024u && f.calls==3u);
    // DOS stub can be much larger than the bounded stack header.
    File larger{};
    create_pe(larger,0x600u);
    assert(pe_stream::parse(read_at,&larger,larger.size,&image)==0);
    assert(larger.max_read<=1024u);
    // Reject bad signature, file truncation and unrepresentable headers.
    f.bytes[0]='X';
    assert(pe_stream::parse(read_at,&f,f.size,&image)!=0);
    f.bytes[0]='M';
    assert(pe_stream::parse(read_at,&f,100u,&image)!=0);
    f.short_read=true;
    assert(pe_stream::parse(read_at,&f,f.size,&image)!=0);
    f.short_read=false;
    wr16(f.bytes+0x80+20u,900u);
    assert(pe_stream::parse(read_at,&f,f.size,&image)!=0);
    for(int i=1;i<argc;++i)check_real_pe(argv[i]);
    return 0;
}
