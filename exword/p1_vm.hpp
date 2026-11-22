/* EX-word-safe P1-backed VM metadata; never edits gint/OS UTLB.
 * Pages are ordinary physical P1 SRAM, so RO is a logical permission
 * (query/protect), NOT an enforceable CPU page-fault boundary. */
#ifndef NTOS_EXWORD_P1_VM_HPP
#define NTOS_EXWORD_P1_VM_HPP
#include <stdint.h>
struct NtosP1Vm {
    static constexpr unsigned MAX_REGIONS=4u;
    static constexpr unsigned MAX_PAGES=64u;
    struct Region {
        uint32_t raw,base,pages;
        uint8_t protection[MAX_PAGES];
    };
    Region regions[MAX_REGIONS]{};
    uint32_t (*allocate)(uint32_t,void *)=nullptr;
    void (*deallocate)(uint32_t,void *)=nullptr;
    void (*zero)(uint32_t,uint32_t,void *)=nullptr;
    void *context=nullptr;
    void begin(uint32_t (*a)(uint32_t,void *),
               void (*f)(uint32_t,void *),
               void (*z)(uint32_t,uint32_t,void *),void *ctx) {
        for(auto &r:regions)r={};
        allocate=a;deallocate=f;zero=z;context=ctx;
    }
    uint32_t reserve(uint32_t requested,uint32_t pages) {
        if(requested || pages==0u || pages>MAX_PAGES || !allocate)return 0u;
        for(auto &r:regions)if(!r.raw) {
            uint32_t bytes=pages*4096u+4095u;
            uint32_t raw=allocate(bytes,context);
            if(!raw || raw>0xfffff000u)return 0u;
            uint32_t base=(raw+4095u)&~4095u;
            if(base<raw || base+pages*4096u<base)return 0u;
            r={};
            r.raw=raw;r.base=base;r.pages=pages;
            return base;
        }
        return 0u;
    }
    Region *find(uint32_t at,uint32_t pages,uint32_t *index) {
        if((at&4095u) || !pages || pages>MAX_PAGES)return nullptr;
        for(auto &r:regions)if(r.raw && at>=r.base) {
            uint32_t p=(at-r.base)/4096u;
            if(p<=r.pages && pages<=r.pages-p) {
                if(index)*index=p;
                return &r;
            }
        }
        return nullptr;
    }
    uint32_t change(uint32_t at,uint32_t pages,uint32_t protection,
                    bool is_commit) {
        if(protection!=1u && protection!=2u)return (uint32_t)-1;
        uint32_t p=0u;
        Region *r=find(at,pages,&p);
        if(!r)return (uint32_t)-1;
        for(uint32_t i=0;i<pages;++i)
            if(!is_commit && !r->protection[p+i])return (uint32_t)-1;
        for(uint32_t i=0;i<pages;++i) {
            if(is_commit && r->protection[p+i]==0u && zero)
                zero(at+i*4096u,4096u,context);
            r->protection[p+i]=(uint8_t)protection;
        }
        return 0u;
    }
    uint32_t query(uint32_t at) {
        uint32_t p=0u;
        Region *r=find(at&~4095u,1u,&p);
        return r?r->protection[p]:(uint32_t)-1;
    }
    uint32_t committed() const {
        uint32_t n=0u;
        for(const auto &r:regions)if(r.raw)
            for(uint32_t i=0u;i<r.pages;++i)
                if(r.protection[i])++n;
        return n;
    }
    int32_t hint(uint32_t at) {
        uint32_t p=query(at);
        return p==1u || p==2u ? 0:-1;
    }
    uint32_t release(uint32_t at) {
        for(auto &r:regions)if(r.raw && r.base==at) {
            for(uint32_t i=0u;i<r.pages;++i)
                if(r.protection[i])return (uint32_t)-1;
            if(deallocate)deallocate(r.raw,context);
            r={};
            return 0u;
        }
        return (uint32_t)-1;
    }
};
#endif
