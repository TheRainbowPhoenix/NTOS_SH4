// pe_pager.hpp -- on-demand PE page materialization, without taking over
// Hollyhock's exception/TLB miss vectors.
//
// WinCE NK loader separates image backing from page population. This code
// does the same: read ONLY the header/section bytes needed for a requested
// 4096-byte RVA page, zero-fill BSS and gaps, then apply supported PE32
// base-relocation entries for that page.
//
// It uses an abstract read_at callback, making filesystem vs ROM vs memory
// independent of relocation and page residency. The callback MUST NOT be
// called in a privileged exception context until a host-safe I/O trampoline,
// dedicated IRQ stack and TLB fault ownership are implemented and tested.
#pragma once
#include <stdint.h>
#include "pe.hpp"
#include "pe_rebase.hpp"

#ifndef NTOS_ENABLE_TLB_MISS_PAGER
#define NTOS_ENABLE_TLB_MISS_PAGER 0
#endif

namespace ntos_pager {
constexpr uint32_t PAGE_BYTES=4096u;
using ReadAt=bool (*)(void *,uint32_t,uint8_t *,uint32_t);
struct Source {
    const pe::Image *hdr;
    void *context;
    ReadAt read;
    uint32_t file_length;
    uint32_t actual_base;
    bool data_big_endian;
};
inline void zero(uint8_t *dst,uint32_t len) {
    for(uint32_t i=0;i<len;++i)dst[i]=0;
}
inline bool raw_read(const Source &s,uint32_t off,uint8_t *dst,
                     uint32_t count) {
    if(!s.read || !dst || off>s.file_length ||
       count>s.file_length-off) return false;
    return s.read(s.context,off,dst,count);
}
inline uint32_t min(uint32_t a,uint32_t b){return a<b?a:b;}
inline uint32_t max(uint32_t a,uint32_t b){return a>b?a:b;}

// Convert an RVA range into a file range (only if fully file-backed).
// PE headers are file-backed; .bss and section virtual tails are zero.
inline bool file_offset(const Source &s,uint32_t rva,uint32_t bytes,
                        uint32_t *out) {
    if(!s.hdr || !out || rva>s.hdr->oh.size_of_image ||
       bytes>s.hdr->oh.size_of_image-rva) return false;
    if(rva<s.hdr->oh.size_of_headers &&
       bytes<=s.hdr->oh.size_of_headers-rva) {
        *out=rva;return true;
    }
    for(uint32_t i=0;i<s.hdr->section_count;++i) {
        const pe::SectionHeader &h=s.hdr->sections[i];
        if(rva<h.rva || rva-h.rva>h.raw_size ||
           bytes>h.raw_size-(rva-h.rva)) continue;
        if(h.raw_offset>s.file_length ||
           rva-h.rva>s.file_length-h.raw_offset) return false;
        const uint32_t off=h.raw_offset+(rva-h.rva);
        if(bytes>s.file_length-off)return false;
        *out=off;return true;
    }
    return false;
}
inline bool read_rva(const Source &s,uint32_t rva,uint8_t *dst,
                     uint32_t count) {
    uint32_t offset=0;
    return file_offset(s,rva,count,&offset) &&
           raw_read(s,offset,dst,count);
}
inline bool read_u16(const Source &s,uint32_t rva,uint16_t *out) {
    uint8_t b[2];
    if(!read_rva(s,rva,b,2u))return false;
    *out=pe::rd16le(b);return true;
}
inline bool read_u32(const Source &s,uint32_t rva,uint32_t *out) {
    uint8_t b[4];
    if(!read_rva(s,rva,b,4u))return false;
    *out=pe::rd32le(b);return true;
}

// Page-in is transactional: a bad metadata record returns false, and the
// caller must discard the destination buffer, NOT install its UTLB entry.
// ABSOLUTE(0) and HIGHLOW(3) cover all NTOS-produced SH4BE DLL fixups.
// Other WinCE SH split-fixups need cross-page pairing and are explicitly
// refused in the *lazy* backend; the eager loader handles them already.
inline bool materialize(const Source &s,uint32_t page_index,
                        uint8_t *dst,const char **error) {
    if(!error || !dst || !s.hdr) return false;
    *error="ok";
    const uint32_t image_size=s.hdr->oh.size_of_image;
    if(image_size==0u || page_index>0xFFFFFFFFu/PAGE_BYTES ||
       page_index*PAGE_BYTES>=image_size) {
        *error="page outside PE image";return false;
    }
    const uint32_t rva=page_index*PAGE_BYTES;
    zero(dst,PAGE_BYTES);
    if(rva<s.hdr->oh.size_of_headers) {
        uint32_t n=min(PAGE_BYTES,s.hdr->oh.size_of_headers-rva);
        n=min(n,image_size-rva);
        if(!raw_read(s,rva,dst,n)) {
            *error="short read PE header page";return false;
        }
    }
    for(uint32_t i=0;i<s.hdr->section_count;++i) {
        const pe::SectionHeader &h=s.hdr->sections[i];
        if(!h.raw_size)continue;
        if(h.rva>image_size || h.raw_size>image_size-h.rva ||
           h.raw_offset>s.file_length ||
           h.raw_size>s.file_length-h.raw_offset) {
            *error="invalid PE section range";return false;
        }
        const uint32_t end=h.rva+h.raw_size;
        const uint32_t begin=max(rva,h.rva);
        const uint32_t limit=min(rva+PAGE_BYTES,end);
        if(begin>=limit)continue;
        if(!raw_read(s,h.raw_offset+(begin-h.rva),
                     dst+(begin-rva),limit-begin)) {
            *error="short read section page";return false;
        }
    }
    const uint32_t delta=s.actual_base-s.hdr->oh.image_base;
    if(delta==0u)return true;
    if(s.hdr->fh.characteristics&pe::F_RELOCS_STRIPPED) {
        *error="fixed PE DLL cannot be lazily rebased";return false;
    }
    const uint32_t reloc=s.hdr->oh.basereloc_rva;
    const uint32_t length=s.hdr->oh.basereloc_size;
    if(!reloc || length<8u || reloc>image_size ||
       length>image_size-reloc) {
        *error="missing PE base relocation directory";return false;
    }
    uint32_t off=0u;
    while(off<length) {
        if(length-off<8u){*error="truncated relocation block";return false;}
        uint32_t block_page=0,block_bytes=0;
        if(!read_u32(s,reloc+off,&block_page) ||
           !read_u32(s,reloc+off+4u,&block_bytes) ||
           (block_page&(PAGE_BYTES-1u)) ||
           block_bytes<8u || (block_bytes&3u) ||
           block_bytes>length-off) {
            *error="malformed relocation block";return false;
        }
        for(uint32_t pos=8u;pos<block_bytes;pos+=2u) {
            uint16_t entry=0;
            if(!read_u16(s,reloc+off+pos,&entry)){
                *error="cannot read relocation entry";return false;
            }
            const uint32_t kind=entry>>12;
            if(kind==0u)continue;
            if(kind!=3u) {
                *error="lazy pager needs HIGH/LOW pair support";return false;
            }
            const uint32_t site=block_page+(entry&0x0FFFu);
            if(site<block_page || site>image_size ||
               4u>image_size-site) {
                *error="relocation site outside PE";return false;
            }
            if(site>=rva && site<rva+PAGE_BYTES) {
                if(site-rva>PAGE_BYTES-4u) {
                    *error="cross-page relocation requires paired pin";
                    return false;
                }
                uint8_t *at=dst+(site-rva);
                const uint32_t v=ntos_rebase::load32(at,s.data_big_endian);
                ntos_rebase::store32(at,v+delta,s.data_big_endian);
            }
        }
        off+=block_bytes;
    }
    return true;
}

// Resident page bookkeeping only; NO register/UTLB modifications occur here.
// A real miss path needs a dedicated handler, safe fault I/O, dirty-page
// writeback and pinning all IAT/dynamic-data pages. Leave disabled by default.
struct ResidentPage {
    uint32_t va;
    uint32_t physical;
    uint32_t last_use;
    bool valid;
    bool dirty;
    bool pinned;
};
template<unsigned SLOTS>
struct PageTable {
    ResidentPage pages[SLOTS]{};
    uint32_t clock=0;
    int lookup(uint32_t va) {
        va&=~(PAGE_BYTES-1u);
        for(unsigned i=0;i<SLOTS;++i)
            if(pages[i].valid && pages[i].va==va) {
                pages[i].last_use=++clock;
                return static_cast<int>(i);
            }
        return -1;
    }
    int victim() const {
        int oldest=-1;
        for(unsigned i=0;i<SLOTS;++i) {
            if(!pages[i].valid)return static_cast<int>(i);
            if(pages[i].pinned || pages[i].dirty)continue;
            if(oldest<0 || pages[i].last_use<pages[oldest].last_use)
                oldest=static_cast<int>(i);
        }
        return oldest;
    }
    bool register_page(uint32_t va,uint32_t pa,bool pin) {
        int slot=victim();
        if(slot<0)return false;
        pages[slot]={va&~(PAGE_BYTES-1u),pa,++clock,true,false,pin};
        return true;
    }
};
} // namespace ntos_pager
