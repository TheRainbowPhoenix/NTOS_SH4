/* Deterministic PE32 fixup unit tests derived from the WinCE 6 NK
 * loader.c relocation switch and the PE32 base-reloc specification.
 * No Microsoft source/code or uploaded binary is redistributed.
 */
#include "../src/pe_rebase.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

static void le16(uint8_t *p,uint16_t x) {
    p[0]=static_cast<uint8_t>(x);
    p[1]=static_cast<uint8_t>(x>>8);
}
static void le32(uint8_t *p,uint32_t x) {
    p[0]=static_cast<uint8_t>(x);
    p[1]=static_cast<uint8_t>(x>>8);
    p[2]=static_cast<uint8_t>(x>>16);
    p[3]=static_cast<uint8_t>(x>>24);
}
static pe::Image image(uint32_t base,uint32_t rva,uint32_t bytes) {
    pe::Image h{};
    h.oh.magic=pe::OPT_MAGIC_PE32;
    h.oh.image_base=base;
    h.oh.basereloc_rva=rva;
    h.oh.basereloc_size=bytes;
    return h;
}
static void block(uint8_t *mem,uint32_t where,uint32_t page,
                  const std::vector<uint16_t> &entries) {
    le32(mem+where,page);
    le32(mem+where+4u,static_cast<uint32_t>(8u+2u*entries.size()));
    for(unsigned i=0;i<entries.size();++i)
        le16(mem+where+8u+2u*i,entries[i]);
}
static void check_highlow(bool big) {
    std::vector<uint8_t> mem(0x4000u);
    auto h=image(0x10000000u,0x3000u,12u);
    block(mem.data(),0x3000u,0x1000u,{0x3040u,0x0000u});
    ntos_rebase::store32(mem.data()+0x1040u,0x10001000u,big);
    const char *err=nullptr;
    assert(ntos_rebase::apply_with_endian(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x01000000u,big,&err));
    assert(ntos_rebase::load32(mem.data()+0x1040u,big)==0x01001000u);
    std::printf("PASS %s SH4 PE HIGHLOW and negative delta\n",
                big?"big-endian":"little-endian");
}
static void check_high_low_pair() {
    std::vector<uint8_t> mem(0x4000u);
    auto h=image(0x10000000u,0x3000u,12u);
    block(mem.data(),0x3000u,0x1000u,{0x1040u,0x2044u});
    ntos_rebase::store16(mem.data()+0x1040u,0x1000u,true);
    ntos_rebase::store16(mem.data()+0x1044u,0xf000u,true);
    const char *err=nullptr;
    assert(ntos_rebase::apply(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x1001c000u,&err));
    // Rounded high and low split, as in WinCE RelocatePage().
    assert(ntos_rebase::load16(mem.data()+0x1040u,true)==0x1003u);
    assert(ntos_rebase::load16(mem.data()+0x1044u,true)==0xb000u);
    std::puts("PASS WinCE SH paired HIGH/LOW with rounding");
}
static void check_highadj() {
    std::vector<uint8_t> mem(0x4000u);
    auto h=image(0x10000000u,0x3000u,12u);
    // HIGHADJ consumes the following signed 16-bit addend.
    block(mem.data(),0x3000u,0x1000u,{0x4040u,0xf000u});
    ntos_rebase::store16(mem.data()+0x1040u,0x1000u,true);
    const char *err=nullptr;
    assert(ntos_rebase::apply(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x10020000u,&err));
    assert(ntos_rebase::load16(mem.data()+0x1040u,true)==0x1002u);
    std::puts("PASS WinCE HIGHADJ signed extra WORD");
}
static void check_invalid_transactional() {
    std::vector<uint8_t> mem(0x4000u);
    auto h=image(0x10000000u,0x3000u,12u);
    block(mem.data(),0x3000u,0x1000u,{0x3040u,0xf044u});
    ntos_rebase::store32(mem.data()+0x1040u,0x10001400u,true);
    const char *err=nullptr;
    assert(!ntos_rebase::apply(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x10020000u,&err));
    assert(ntos_rebase::load32(mem.data()+0x1040u,true)==0x10001400u);
    // PE images marked IMAGE_FILE_RELOCS_STRIPPED are fixed even if a
    // malformed producer left a relocation directory behind.
    h.fh.characteristics=pe::F_RELOCS_STRIPPED;
    assert(!ntos_rebase::apply(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x10020000u,&err));
    h.fh.characteristics=0;
    // Explicit invalid block size: don't read beyond mapped image.
    le32(mem.data()+0x3000u+4u,0xffffff00u);
    assert(!ntos_rebase::apply(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x10020000u,&err));
    std::puts("PASS malformed and unsupported relocation rejection is atomic");
}
static void check_large_image() {
    // Approximate HWXCHS.DLL SizeOfImage: 0x389000, but use generated
    // fixups rather than redistributing the user's WinCE binary.
    std::vector<uint8_t> mem(0x389000u);
    const uint32_t reloc=0x386000u;
    const uint32_t count_pages=20u;
    const uint32_t block_bytes=136u; // 64 HIGHLOW entries per page
    auto h=image(0x10000000u,reloc,count_pages*block_bytes);
    for(uint32_t p=0;p<count_pages;++p) {
        const uint32_t page=0x1000u+p*0x1000u;
        std::vector<uint16_t> entries;
        for(uint16_t j=0;j<64u;++j) {
            entries.push_back(static_cast<uint16_t>(
                0x3000u+(j*4u)));
            ntos_rebase::store32(mem.data()+page+j*4u,
                                 0x10000000u+page+j*4u,false);
        }
        block(mem.data(),reloc+p*block_bytes,page,entries);
    }
    const char *err=nullptr;
    assert(ntos_rebase::apply_with_endian(h,mem.data(),
           static_cast<uint32_t>(mem.size()),0x01000000u,false,&err));
    assert(ntos_rebase::load32(mem.data()+0x1000u,false)==0x01001000u);
    const uint32_t last=0x1000u+(count_pages-1u)*0x1000u+63u*4u;
    assert(ntos_rebase::load32(mem.data()+last,false)==0x01000000u+last);
    std::puts("PASS 3.7MB WinCE-shaped image, 1280 LE fixups (offline)");
}
int main() {
    check_highlow(true);
    check_highlow(false);
    check_high_low_pair();
    check_highadj();
    check_invalid_transactional();
    check_large_image();
    return 0;
}
