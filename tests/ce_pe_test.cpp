#include "../src/ce_pe.hpp"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cassert>
static uint32_t stub(const char *dll,const char *symbol,uint16_t ordinal,void *) {
 if(cepe::eq(dll,"coredll.dll")&&symbol&&
    cepe::eq(symbol,"OutputDebugStringA"))return 0x8c280120;
 if(cepe::eq(dll,"coredll.dll")&&ordinal==42)return 0x8c280160;
 return 0;
}
int main(int argc,char **argv) {
 if(argc<2)return 2;
 std::ifstream f(argv[1],std::ios::binary);
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
 assert(!bytes.empty());
 cepe::Image image{};
 assert(cepe::parse(bytes.data(),(uint32_t)bytes.size(),&image)==cepe::OK);
 std::vector<uint8_t> mapped(image.size);
 cepe::Status s=cepe::map(bytes.data(),(uint32_t)bytes.size(),image,
                         mapped.data(),(uint32_t)mapped.size(),
                         image.preferred+0x100000);
 assert(s==cepe::FIXED_BASE_REQUIRED || image.dirs[5].size);
 assert(cepe::map(bytes.data(),(uint32_t)bytes.size(),image,
                  mapped.data(),(uint32_t)mapped.size(),
                  image.preferred)==cepe::OK);
 printf("PASS WinCE PE32 mapped at preferred 0x%08x, CPU 0x%x\n",
        image.preferred,image.machine);
 if(argc>=3 && argv[2][0]=='l'){
  // The real GCC-built SH4BE HELLO_PE.EXE carries HIGHLOW relocations,
  // so it can execute from HH3's actual heap rather than the fixed WinCE VA.
  assert(image.machine==cepe::SH4BE_CUSTOM);
  assert(image.dirs[5].rva && image.dirs[5].size);
  assert(cepe::map(bytes.data(),(uint32_t)bytes.size(),image,
         mapped.data(),(uint32_t)mapped.size(),0x8c280000)==cepe::OK);
  assert(cepe::imports(mapped.data(),image,stub,nullptr)==cepe::OK);
  uint32_t desc=image.dirs[1].rva;
  assert(desc);
  uint32_t iat=cepe::r32(mapped.data()+desc+16,image.little);
  assert(iat<=image.size-4);
  assert(cepe::r32(mapped.data()+iat,image.little)==0x8c280120);
  puts("PASS genuine SH4BE compiled PE, HIGHLOW relocation and COREDLL IAT binding");
 }
 if(argc>=3 && argv[2][0]=='i'){
  s=cepe::imports(mapped.data(),image,stub,nullptr);
  assert(s==cepe::OK);
  assert(cepe::r32(mapped.data()+0x2060,image.little)==0x8c280120);
  assert(cepe::r32(mapped.data()+0x2064,image.little)==0x8c280160);
  puts("PASS COREDLL named and ordinal import fixups");
 }
}
