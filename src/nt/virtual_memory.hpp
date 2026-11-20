// Nt-style per-foreground-launch reserve / commit / protect.
//
// This is intentionally a small, preallocated P1-backed private VM arena,
// NOT a complete NT address-space manager. No SDK, heap, I/O or exception
// vector manipulation is performed by this class. A P0 guest page is backed
// only after MEM_COMMIT. In-memory bookkeeping works with the existing
// 55-entry UTLB pager through the external resolve/reprotect callbacks.
//
// Crucial safety rule: do not free/reuse a committed physical frame while an
// old P0 UTLB/ITLB entry might still map it. Live decommit/release therefore
// fail closed; g_nt_process teardown occurs only AFTER MMU/TLB flush.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"
#include "mm_protect.hpp"

namespace nt {
constexpr uint32_t VM_BASE=0x06000000u;
constexpr uint32_t VM_END =0x07000000u;
constexpr uint32_t VM_PAGE=4096u;
enum class VmAccess : uint8_t { reserved=0u, read_only=1u, read_write=2u };
using VmCollision=bool (*)(void *,uint32_t,uint32_t);
using VmRemap=bool (*)(void *,uint32_t,ntos_mm::Access);
template<unsigned CAPACITY=64u,unsigned REGIONS=8u>
struct VirtualMemory {
    static_assert(CAPACITY>0u && CAPACITY<=256u);
    static_assert(REGIONS>0u && REGIONS<=64u);
    struct Region {uint32_t base=0u,pages=0u;};
    struct Page {
        uint32_t va=0u;
        uint16_t frame=0xFFFFu;
        VmAccess access=VmAccess::reserved;
    };
    Region regions[REGIONS]{};
    Page pages[CAPACITY]{};
    bool frames[CAPACITY]{};
    uint8_t *backing=nullptr;   // aligned P1 arena; owned by loader
    uint32_t physical=0u;     // 4 KiB aligned host physical base
    unsigned reserved_pages=0u,committed_pages=0u,peak_pages=0u;

    void configure(uint8_t *p1,uint32_t pa) {
        backing=p1;physical=pa;
    }
    void clear() {
        for(unsigned i=0u;i<REGIONS;++i)regions[i]={};
        for(unsigned i=0u;i<CAPACITY;++i){pages[i]={};frames[i]=false;}
        backing=nullptr;physical=0u;
        reserved_pages=committed_pages=peak_pages=0u;
    }
    int find(uint32_t va) const {
        va &= ~(VM_PAGE-1u);
        for(unsigned i=0u;i<CAPACITY;++i)
            if(pages[i].va==va && va!=0u)return static_cast<int>(i);
        return -1;
    }
    bool is_reserved(uint32_t base,uint32_t length) const {
        if(!length || (base&(VM_PAGE-1u)) || base>=VM_END ||
           length>(VM_END-base)/VM_PAGE)return false;
        for(unsigned r=0u;r<REGIONS;++r) {
            const Region &a=regions[r];
            if(a.pages && base>=a.base &&
               base-a.base<=a.pages*VM_PAGE &&
               length<=a.pages-(base-a.base)/VM_PAGE)
                return true;
        }
        return false;
    }
    bool overlaps(uint32_t base,uint32_t count) const {
        for(unsigned r=0u;r<REGIONS;++r) {
            const Region &a=regions[r];
            if(a.pages && base<a.base+a.pages*VM_PAGE &&
               a.base<base+count*VM_PAGE)return true;
        }
        return false;
    }
    // requested==0 chooses first-fit; an explicit base must lie inside the
    // dedicated VM interval, aligned and disjoint from loaded PE modules.
    Status reserve(uint32_t requested,unsigned count,uint32_t *out,
                   VmCollision collides=nullptr,void *context=nullptr) {
        if(!out)return INVALID_PARAMETER;
        *out=0u;
        if(!backing || !count || count>CAPACITY ||
           count>CAPACITY-reserved_pages)
            return INSUFFICIENT_RESOURCES;
        unsigned region=0u;
        while(region<REGIONS && regions[region].pages)++region;
        if(region==REGIONS)return INSUFFICIENT_RESOURCES;
        uint32_t base=requested?requested:VM_BASE;
        if(base<VM_BASE || base>=VM_END || (base&(VM_PAGE-1u)) ||
           count>(VM_END-base)/VM_PAGE)
            return INVALID_PARAMETER;
        bool found=false;
        for(uint32_t va=base;va<=VM_END-count*VM_PAGE;va+=VM_PAGE) {
            if(overlaps(va,count) ||
               (collides && collides(context,va,count))) {
                if(requested)return CONFLICTING_ADDRESSES;
                continue;
            }
            base=va;found=true;break;
        }
        if(!found)return CONFLICTING_ADDRESSES;
        unsigned free_slots=0u;
        for(const Page &p:pages)if(!p.va)++free_slots;
        if(free_slots<count)return INSUFFICIENT_RESOURCES;
        for(unsigned i=0u;i<count;++i) {
            for(Page &p:pages)if(!p.va) {
                p.va=base+i*VM_PAGE;
                p.frame=0xFFFFu;
                p.access=VmAccess::reserved;
                break;
            }
        }
        regions[region]={base,count};
        reserved_pages+=count;
        *out=base;return SUCCESS;
    }
    unsigned free_frames() const {
        unsigned n=0u;for(bool used:frames)if(!used)++n;
        return n;
    }
    Status commit(uint32_t base,unsigned count,VmAccess rights) {
        if(!backing || !count || !is_reserved(base,count) ||
           rights==VmAccess::reserved)
            return INVALID_PARAMETER;
        if(free_frames()<count)return NO_MEMORY;
        // Never make partial commits. One physically assigned frame per VA.
        for(unsigned i=0u;i<count;++i) {
            const int at=find(base+i*VM_PAGE);
            if(at<0)return INVALID_PARAMETER;
            if(pages[at].frame!=0xFFFFu)return ALREADY_COMMITTED;
        }
        for(unsigned i=0u;i<count;++i) {
            const unsigned ix=static_cast<unsigned>(find(base+i*VM_PAGE));
            unsigned frame=0u;
            while(frame<CAPACITY && frames[frame])++frame;
            if(frame==CAPACITY)return NO_MEMORY; // unreachable after preflight
            frames[frame]=true;
            pages[ix].frame=static_cast<uint16_t>(frame);
            pages[ix].access=rights;
            volatile uint8_t *p=backing+frame*VM_PAGE;
            for(unsigned b=0u;b<VM_PAGE;++b)p[b]=0u;
            if(++committed_pages>peak_pages)peak_pages=committed_pages;
        }
        return SUCCESS;
    }
    static ntos_mm::Access convert(VmAccess a) {
        return a==VmAccess::read_write ? ntos_mm::Access::read_write :
               a==VmAccess::read_only ? ntos_mm::Access::read_only :
                                        ntos_mm::Access::no_access;
    }
    bool resolve(uint32_t va,uint32_t *out_pa,ntos_mm::Access *out) const {
        const int ix=find(va);
        if(ix<0 || !out_pa || !out ||
           pages[ix].frame==0xFFFFu)return false;
        const uint32_t offset=uint32_t(pages[ix].frame)*VM_PAGE;
        if(physical>0xFFFFFFFFu-offset)return false;
        *out_pa=physical+offset;
        *out=convert(pages[ix].access);
        return true;
    }
    VmAccess query(uint32_t va) const {
        const int ix=find(va);
        return ix<0 ? VmAccess::reserved : pages[ix].access;
    }
    Status protect(uint32_t base,unsigned count,VmAccess rights,
                   VmRemap remap,void *context,uint32_t *out_old=nullptr) {
        if(!count || !is_reserved(base,count) ||
           rights==VmAccess::reserved)return INVALID_PARAMETER;
        VmAccess previous[CAPACITY]{};
        for(unsigned i=0u;i<count;++i) {
            const int ix=find(base+i*VM_PAGE);
            if(ix<0 || pages[ix].frame==0xFFFFu)
                return NOT_COMMITTED;
            previous[i]=pages[ix].access;
        }
        if(out_old)*out_old=static_cast<uint32_t>(previous[0]);
        for(unsigned i=0u;i<count;++i) {
            const uint32_t va=base+i*VM_PAGE;
            const unsigned ix=static_cast<unsigned>(find(va));
            pages[ix].access=rights;
            if(remap && !remap(context,va,convert(rights))) {
                // Restore both bookkeeping AND any already touched UTLB
                // rights. An inability to restore must fail-stop the host.
                bool rollback_ok=true;
                for(unsigned j=0u;j<=i;++j) {
                    const uint32_t prior_va=base+j*VM_PAGE;
                    const unsigned k=static_cast<unsigned>(find(prior_va));
                    pages[k].access=previous[j];
                    if(remap && !remap(context,prior_va,
                                       convert(previous[j])))
                        rollback_ok=false;
                }
                return rollback_ok?DEVICE_BUSY:INTERNAL_ERROR;
            }
        }
        return SUCCESS;
    }
    // Reserved-only release; a live committed frame may still have cached
    // P0 aliases and must NOT be reused without proven single-slot shootdown.
    Status release(uint32_t base) {
        unsigned r=0u;
        while(r<REGIONS && regions[r].base!=base)++r;
        if(r==REGIONS || !regions[r].pages)return INVALID_PARAMETER;
        for(unsigned i=0u;i<regions[r].pages;++i) {
            const int ix=find(base+i*VM_PAGE);
            if(ix<0)return INTERNAL_ERROR;
            if(pages[ix].frame!=0xFFFFu)return DEVICE_BUSY;
        }
        for(unsigned i=0u;i<regions[r].pages;++i)
            pages[find(base+i*VM_PAGE)]={};
        reserved_pages-=regions[r].pages;
        regions[r]={};
        return SUCCESS;
    }
};
} // namespace nt
