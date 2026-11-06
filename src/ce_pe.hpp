// WinCE PE32 mapper POC. Independent of HH3 SDK; can be host-tested.
// Recognizes MZ+PE, fixed-address WinCE executables, custom SH4BE and imports.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace cepe {
enum Status { OK=0, BAD_HEADER=-1, UNSUPPORTED_MACHINE=-2, BOUNDS=-3,
 FIXED_BASE_REQUIRED=-4, BAD_RELOC=-5, UNRESOLVED_IMPORT=-6,
 UNSUPPORTED_FEATURE=-7 };
constexpr uint16_t SH3=0x01a2, SH4=0x01a6, SH4BE_CUSTOM=0x01a8;
struct Section { uint32_t rva,vsize,raw_size,raw_off; };
struct Directory { uint32_t rva,size; };
struct Image {
 bool little=true, mz=false;
 uint16_t machine=0,flags=0,section_count=0;
 uint32_t preferred=0,entry=0,size=0;
 Section sections[16]{};
 Directory dirs[16]{};
};
using Resolver=uint32_t (*)(const char*,const char*,uint16_t,void*);
inline uint16_t r16(const uint8_t *p,bool le) {
 return le?uint16_t(p[0]|(p[1]<<8)):uint16_t((p[0]<<8)|p[1]);
}
inline uint32_t r32(const uint8_t *p,bool le) {
 return le?(uint32_t(p[0])|(uint32_t(p[1])<<8)|
  (uint32_t(p[2])<<16)|(uint32_t(p[3])<<24)):
  ((uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
  (uint32_t(p[2])<<8)|p[3]);
}
inline void w32(uint8_t *p,uint32_t v,bool le) {
 if(le){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
 else {p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
}
inline bool range(uint32_t o,uint32_t n,uint32_t s) {
 return o<=s && n<=s-o;
}
inline bool str_ok(const uint8_t *b,uint32_t n,uint32_t p) {
 if(p>=n)return false;
 for(uint32_t i=p;i<n;i++)if(!b[i])return true;
 return false;
}
inline bool eq(const char *a,const char *b) {
 for(;*a&&*b;a++,b++){
  char x=*a,y=*b;
  if(x>='A'&&x<='Z')x+=32;
  if(y>='A'&&y<='Z')y+=32;
  if(x!=y)return false;
 }
 return !*a&&!*b;
}
inline Status parse(const uint8_t *f,uint32_t n,Image *out) {
 if(!f||!out||n<20)return BAD_HEADER;
 *out=Image{};
 uint32_t h=0;
 if(n>=64&&f[0]=='M'&&f[1]=='Z') {
  uint32_t pe=r32(f+60,true);
  if(!range(pe,24,n)||memcmp(f+pe,"PE\0\0",4))return BAD_HEADER;
  h=pe+4;out->mz=true;
  uint16_t m=r16(f+h,true);
  out->little=(m==SH3||m==SH4);
  if(!out->little&&r16(f+h,false)!=SH4BE_CUSTOM)return UNSUPPORTED_MACHINE;
 } else out->little=false; // legacy raw BE COFF .NTX
 bool le=out->little;
 out->machine=r16(f+h,le);
 if(out->machine!=SH3&&out->machine!=SH4&&out->machine!=SH4BE_CUSTOM)
  return UNSUPPORTED_MACHINE;
 out->section_count=r16(f+h+2,le);
 out->flags=r16(f+h+18,le);
 uint32_t op=h+20,opt_size=r16(f+h+16,le);
 if(out->section_count<1||out->section_count>16||
  opt_size<96||!range(op,opt_size,n)||
  !range(op+opt_size,out->section_count*40,n)||
  r16(f+op,le)!=0x10b)return BAD_HEADER;
 out->preferred=r32(f+op+28,le);
 out->entry=r32(f+op+16,le);
 out->size=r32(f+op+56,le);
 if(!out->size||out->size>12u*1024*1024||out->entry>=out->size)
  return BAD_HEADER;
 uint32_t dirs=r32(f+op+92,le);
 if(dirs>16)dirs=16;
 for(uint32_t i=0;i<dirs;i++) {
  if(opt_size<96+(i+1)*8)return BAD_HEADER;
  out->dirs[i]={r32(f+op+96+i*8,le),r32(f+op+100+i*8,le)};
  if(out->dirs[i].size&&
   !range(out->dirs[i].rva,out->dirs[i].size,out->size))return BOUNDS;
 }
 for(uint32_t i=0;i<out->section_count;i++){
  const uint8_t *s=f+op+opt_size+i*40;
  Section &x=out->sections[i];
  x={r32(s+12,le),r32(s+8,le),r32(s+16,le),r32(s+20,le)};
  uint32_t span=x.vsize>x.raw_size?x.vsize:x.raw_size;
  if(!range(x.rva,span,out->size)||
    (x.raw_size&&!range(x.raw_off,x.raw_size,n)))return BOUNDS;
 }
 return OK;
}
inline Status map(const uint8_t *f,uint32_t file_size,const Image &im,
 uint8_t *mem,uint32_t cap,uint32_t actual) {
 if(!f||!mem||cap<im.size)return BOUNDS;
 const Directory &dir=im.dirs[5];
 if(actual!=im.preferred&&(!dir.rva||!dir.size))
  return FIXED_BASE_REQUIRED;
 memset(mem,0,im.size);
 for(uint32_t i=0;i<im.section_count;i++){
  const Section &s=im.sections[i];
  if(s.raw_size&&!range(s.raw_off,s.raw_size,file_size))return BOUNDS;
  if(s.raw_size)memcpy(mem+s.rva,f+s.raw_off,s.raw_size);
 }
 if(actual==im.preferred)return OK;
 uint32_t delta=actual-im.preferred,off=dir.rva,end=off+dir.size;
 while(off<end) {
  if(!range(off,8,end))return BAD_RELOC;
  uint32_t page=r32(mem+off,im.little),sz=r32(mem+off+4,im.little);
  if(sz<8||(sz&1)||!range(off,sz,end))return BAD_RELOC;
  for(uint32_t j=8;j<sz;j+=2){
   uint16_t x=r16(mem+off+j,im.little);
   unsigned type=x>>12; if(!type)continue;
   if(type!=3)return UNSUPPORTED_FEATURE;
   uint32_t at=page+(x&0xfff);
   if(!range(at,4,im.size))return BAD_RELOC;
   w32(mem+at,r32(mem+at,im.little)+delta,im.little);
  }
  off+=sz;
 }
 return OK;
}
inline Status imports(uint8_t *mem,const Image &im,Resolver resolve,void *ctx) {
 const Directory &dir=im.dirs[1];
 if(!dir.rva||!dir.size)return OK;
 if(!mem||!resolve)return UNRESOLVED_IMPORT;
 uint32_t off=dir.rva,end=off+dir.size;
 bool terminal=false;
 while(range(off,20,end)){
  uint32_t lookup=r32(mem+off,im.little);
  uint32_t name=r32(mem+off+12,im.little);
  uint32_t iat=r32(mem+off+16,im.little);
  if(!lookup&&!name&&!iat){terminal=true;break;}
  if(!iat||!str_ok(mem,im.size,name))return BOUNDS;
  const char *dll=(const char*)(mem+name);
  if(!eq(dll,"coredll.dll"))return UNRESOLVED_IMPORT;
  uint32_t src=lookup?lookup:iat;bool done=false;
  for(uint32_t i=0;i<im.size/4;i++){
   if(!range(src+i*4,4,im.size)||
      !range(iat+i*4,4,im.size))return BOUNDS;
   uint32_t entry=r32(mem+src+i*4,im.little);
   if(!entry){done=true;break;}
   uint16_t ordinal=0;const char *name_symbol=nullptr;
   if(entry&0x80000000u)ordinal=entry&0xffffu;
   else {
    if(!range(entry,3,im.size)||
       !str_ok(mem,im.size,entry+2))return BOUNDS;
    name_symbol=(const char*)(mem+entry+2);
   }
   uint32_t addr=resolve(dll,name_symbol,ordinal,ctx);
   if(!addr)return UNRESOLVED_IMPORT;
   w32(mem+iat+i*4,addr,im.little);
  }
  if(!done)return BOUNDS;
  off+=20;
 }
 return terminal?OK:BOUNDS;
}
inline const char *error(Status s) {
 switch(s) {
  case OK:return "ok";
  case BAD_HEADER:return "invalid PE32 header";
  case UNSUPPORTED_MACHINE:return "unsupported PE machine";
  case BOUNDS:return "PE directory or section outside image";
  case FIXED_BASE_REQUIRED:return "fixed WinCE PE needs preferred virtual base and MMU slot";
  case BAD_RELOC:return "invalid PE base relocation";
  case UNRESOLVED_IMPORT:return "missing COREDLL stub import";
  case UNSUPPORTED_FEATURE:return "unsupported PE relocation type";
  default:return "unknown PE error";
 }
}
} // namespace cepe
