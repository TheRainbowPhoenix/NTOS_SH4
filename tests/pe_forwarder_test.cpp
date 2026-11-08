// Verify PE32 raw export lookup preserves WinCE forwarded entry RVAs,
// for both named and ordinal imports, without requiring a PE compiler.
#include "../src/pe.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static void w16(uint8_t *p,uint16_t v) {
    p[0]=static_cast<uint8_t>(v);
    p[1]=static_cast<uint8_t>(v>>8);
}
static void w32(uint8_t *p,uint32_t v) {
    p[0]=static_cast<uint8_t>(v);
    p[1]=static_cast<uint8_t>(v>>8);
    p[2]=static_cast<uint8_t>(v>>16);
    p[3]=static_cast<uint8_t>(v>>24);
}
int main() {
    std::vector<uint8_t> bytes(0x1000u);
    pe::Image hdr{};
    hdr.oh.magic=pe::OPT_MAGIC_PE32;
    hdr.oh.export_rva=0x100u;
    hdr.oh.export_size=0x100u;
    w32(bytes.data()+0x110u,7u); // ordinal_base
    w32(bytes.data()+0x114u,2u); // NumberOfFunctions
    w32(bytes.data()+0x118u,2u); // NumberOfNames
    w32(bytes.data()+0x11cu,0x200u); // AddressOfFunctions
    w32(bytes.data()+0x120u,0x210u); // AddressOfNames
    w32(bytes.data()+0x124u,0x220u); // AddressOfNameOrdinals
    w32(bytes.data()+0x200u,0x150u); // export index 0 forwards
    w32(bytes.data()+0x204u,0x300u); // export index 1 ordinary code
    w32(bytes.data()+0x210u,0x240u);
    w32(bytes.data()+0x214u,0x250u);
    w16(bytes.data()+0x220u,0u);
    w16(bytes.data()+0x222u,1u);
    std::memcpy(bytes.data()+0x150u,"ST02.stress_d02",16u);
    std::memcpy(bytes.data()+0x240u,"Forward",8u);
    std::memcpy(bytes.data()+0x250u,"Native",7u);
    uint32_t rva=0;
    assert(pe::export_address(hdr,bytes.data(),bytes.size(),"Forward",
                              0u,false,&rva));
    assert(rva==0x150u);
    assert(pe::export_address(hdr,bytes.data(),bytes.size(),nullptr,
                              7u,true,&rva));
    assert(rva==0x150u);
    assert(pe::export_address(hdr,bytes.data(),bytes.size(),"Native",
                              0u,false,&rva));
    assert(rva==0x300u);
    assert(!pe::export_address(hdr,bytes.data(),bytes.size(),nullptr,
                               6u,true,&rva));
    assert(!pe::export_address(hdr,bytes.data(),bytes.size(),"native",
                               0u,false,&rva));
    assert(!pe::export_lookup(hdr,bytes.data(),bytes.size(),
                              "Forward",&rva));
    std::puts("PASS WinCE named/ordinal forwarder exports and valid address lookup");
    return 0;
}
