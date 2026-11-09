#include "../src/console.hpp"
#include <cassert>
#include <cstring>
int main() {
 TextConsole<5,3> c; unsigned paints=0;
 auto paint=[&](unsigned r,const char *s){assert(r<3&&std::strlen(s)<=5);++paints;};
 c.puts("abcdeX",paint);assert(!std::strcmp(c.line(0),"abcde"));assert(!std::strcmp(c.line(1),"X"));
 c.puts("Z\r\n",paint);assert(c.row()==2&&!std::strcmp(c.line(1),"Z"));
 c.puts("next",paint);assert(c.row()==2&&!std::strcmp(c.line(1),"next"));
 c.reset();c.puts("long",paint);c.position(0);c.puts("a",paint);assert(!std::strcmp(c.line(0),"a"));assert(paints>0);
 unsigned scrolls=0; c.reset();
 for(unsigned i=0;i<4;++i)c.puts("row",paint,[&](){++scrolls;return true;});
 assert(scrolls==2&&!std::strcmp(c.line(1),"row"));
 TextConsole<> full;for(unsigned i=0;i<40;++i)full.puts("line",[](unsigned,const char*){});assert(full.row()==39);
}

