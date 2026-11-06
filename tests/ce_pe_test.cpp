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
 if(argc>=3){
  s=cepe::imports(mapped.data(),image,stub,nullptr);
  assert(s==cepe::OK);
  assert(cepe::r32(mapped.data()+0x2060,image.little)==0x8c280120);
  assert(cepe::r32(mapped.data()+0x2064,image.little)==0x8c280160);
  puts("PASS COREDLL named and ordinal import fixups");
 }
}
