// NTOS GWES compositor state for the next process-scheduling phase.
// Kernel-owned handle/owner table and borrowed, verified P1 RGB565 surfaces.
// It does not dereference guest P0 pointers, allocate a framebuffer or switch
// SH4AL-DSP threads: the caller is responsible for verified retained frames.
// Distinct PID ownership and bounded row-wise compositing are testable now.
#pragma once
#include <stdint.h>
namespace nt {

struct GwesArea {int left,top,right,bottom;};
struct GwesSurface {
    uint32_t handle=0,pid=0,tid=0;
    const uint16_t *pixels=nullptr; // ONLY trusted kernel/P1 snapshot memory
    uint16_t width=0,height=0,stride=0;
    bool visible=false,dirty=false;
};
using GwesScanline = void (*)(void *,int,int,const uint16_t *,unsigned);

template<unsigned MAX_WINDOWS=2u>
struct GwesCompositor {
    static_assert(MAX_WINDOWS==2u);
    GwesSurface windows[MAX_WINDOWS]{};
    uint32_t sequence=1u,focused=0u;
    uint8_t mode=0u,swapped=0u,sip=0u;
    bool changed=true;
    static constexpr int width=320,height=528,title=24,sip_y=271;

    GwesSurface *find(uint32_t hwnd) {
        for(auto &s:windows)if(s.handle==hwnd && hwnd)return &s;
        return nullptr;
    }
    const GwesSurface *find(uint32_t hwnd) const {
        for(const auto &s:windows)if(s.handle==hwnd && hwnd)return &s;
        return nullptr;
    }
    unsigned count() const {
        unsigned n=0;
        for(const auto &s:windows)if(s.handle)++n;
        return n;
    }
    uint32_t create(uint32_t pid,uint32_t tid,
                    const uint16_t *p,unsigned w,unsigned h,unsigned pitch) {
        // Reject unowned image pointers, malformed stride and huge surfaces.
        if(!pid || !tid || !p || w<1u || h<1u || w>320u || h>528u ||
           pitch<w || pitch>320u || sequence>0x00ffffffu)return 0u;
        for(auto &s:windows)if(!s.handle) {
            s.handle=0x47000000u|sequence++;
            s.pid=pid;s.tid=tid;s.pixels=p;
            s.width=(uint16_t)w;s.height=(uint16_t)h;
            s.stride=(uint16_t)pitch;s.visible=true;s.dirty=true;
            if(!focused)focused=s.handle;
            changed=true;return s.handle;
        }
        return 0;
    }
    bool owns(uint32_t pid,uint32_t hwnd) const {
        const auto *s=find(hwnd);
        return s && s->pid==pid;
    }
    bool publish(uint32_t pid,uint32_t hwnd,const uint16_t *p,
                 unsigned w,unsigned h,unsigned pitch) {
        GwesSurface *s=find(hwnd);
        if(!s || s->pid!=pid || !p || w<1u || h<1u || w>320u || h>528u ||
           pitch<w || pitch>320u)return false;
        s->pixels=p;s->width=(uint16_t)w;s->height=(uint16_t)h;
        s->stride=(uint16_t)pitch;s->dirty=true;changed=true;
        return true;
    }
    bool close(uint32_t pid,uint32_t hwnd) {
        auto *s=find(hwnd);
        if(!s || s->pid!=pid)return false;
        s->handle=0;s->pixels=nullptr;s->pid=0;s->tid=0;
        s->visible=false;s->dirty=false;
        if(focused==hwnd)
            focused=windows[0].handle?windows[0].handle:windows[1].handle;
        if(count()<2u)mode=0u;
        changed=true;
        return true;
    }
    void close_pid(uint32_t pid) {
        for(unsigned i=0;i<MAX_WINDOWS;++i) {
            uint32_t hwnd=windows[i].handle;
            if(hwnd && windows[i].pid==pid)(void)close(pid,hwnd);
        }
    }
    bool focus(uint32_t pid,uint32_t hwnd) {
        if(!owns(pid,hwnd))return false;
        focused=hwnd;changed=true;return true;
    }
    bool set_layout(unsigned m) {
        if(m>2u || (m && count()<2u))return false;
        mode=(uint8_t)m;changed=true;return true;
    }
    void swap() {swapped^=1u;changed=true;}
    void keyboard(bool visible) {sip=visible?1u:0u;changed=true;}
    int bottom() const {return sip?sip_y:height;}
    bool rect(uint32_t hwnd,GwesArea *out) const {
        if(!out || !find(hwnd))return false;
        const GwesSurface *s=find(hwnd);
        if(!s->visible)return false;
        if(!mode && hwnd!=focused)return false;
        const unsigned slot=(unsigned)(s==&windows[1]);
        const bool first=(slot^(unsigned)swapped)==0u;
        out->left=0;out->top=title;out->right=width;out->bottom=bottom();
        if(mode==1u) {
            int mid=title+(bottom()-title)/2;
            if(first)out->bottom=mid;else out->top=mid;
        } else if(mode==2u) {
            if(first)out->right=width/2;else out->left=width/2;
        }
        return out->left<out->right && out->top<out->bottom;
    }
    uint32_t hit(int x,int y) const {
        if(x<0 || y<title || x>=width || y>=bottom())return 0;
        for(const auto &s:windows) {
            GwesArea r{};
            if(rect(s.handle,&r) && x>=r.left && x<r.right &&
               y>=r.top && y<r.bottom)return s.handle;
        }
        return 0u;
    }
    // Never calls a device driver directly. Scanline writes can DMA/IRP.
    // A process's pixels are never visible unless its *validated* snapshot
    // is registered under a live owner PID. Clipped rows only, no memcpy.
    unsigned compose(GwesScanline output,void *ctx) {
        if(!output || !changed)return 0u;
        unsigned rows=0u;
        for(auto &s:windows) {
            GwesArea r{};
            if(!rect(s.handle,&r) || !s.pixels)continue;
            int w=r.right-r.left;
            int h=r.bottom-r.top;
            if(w>s.width)w=s.width;
            if(h>s.height)h=s.height;
            for(int y=0;y<h;++y) {
                const uint16_t *src=s.pixels+(unsigned)y*(unsigned)s.stride;
                output(ctx,r.left,r.top+y,src,(unsigned)w);
                ++rows;
            }
            s.dirty=false;
        }
        changed=false;return rows;
    }
};
} // namespace nt
