// Host-only regression for bounded, random-access PE header parsing.
// Run: g++ -std=c++20 -Wall -Wextra -Werror -pedantic tests/pe_stream_test.cpp -o /tmp/pe_stream_test && /tmp/pe_stream_test
#include "../src/pe_stream.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

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
int main() {
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
    return 0;
}
