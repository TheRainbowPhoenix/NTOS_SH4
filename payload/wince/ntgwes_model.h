/* NTOS GWES v1: fixed-display, deterministic 2-pane window policy.
 * Original design informed by WinCE's GWES layering, not copied source.
 * Rectangles are physical pixels, half-open. This module does not touch
 * the MMU, HH3 SDK, VBR, USB, FPU or display registers.
 * A pane is a logical window within the current foreground PE, NOT a
 * separate KPROCESS, protected HWND, desktop heap or compositor process.
 */
#ifndef NTOS_GWES_MODEL_H
#define NTOS_GWES_MODEL_H
#include <stdint.h>
#define NT_GWES_WIDTH 320
#define NT_GWES_HEIGHT 528
#define NT_GWES_SIP_TOP 271
#define NT_GWES_TITLE 24
#define NT_GWES_FULL 0u
#define NT_GWES_STACK 1u
#define NT_GWES_COLUMNS 2u
#define NT_GWES_NONE 0u
#define NT_GWES_TITLE_HIT 1u
#define NT_GWES_LAYOUT_HIT 2u
#define NT_GWES_SWAP_HIT 3u
#define NT_GWES_CLOSE_HIT 4u
#define NT_GWES_CLIENT_HIT 5u
typedef struct {int left,top,right,bottom;} NT_GW_RECT;
typedef struct {uint32_t id; unsigned dirty;} NT_GW_PANE;
typedef struct {
    NT_GW_PANE panes[2];
    uint32_t next_id;
    unsigned mode, swapped, sip, focused, serial;
} NT_GWES;
static inline void nt_gwes_init(NT_GWES *g) {
    if(!g)return;
    for(unsigned i=0;i<2u;++i)g->panes[i]=(NT_GW_PANE){0,0};
    g->next_id=1u;g->mode=NT_GWES_FULL;g->swapped=0u;
    g->sip=0u;g->focused=0u;g->serial=1u;
}
static inline void nt_gwes_dirty(NT_GWES *g) {
    ++g->serial;
    for(unsigned i=0;i<2u;++i)
        if(g->panes[i].id)g->panes[i].dirty=1u;
}
static inline int nt_gwes_index(const NT_GWES *g,uint32_t id) {
    if(!g || !id)return -1;
    for(int i=0;i<2;++i)
        if(g->panes[i].id==id)return i;
    return -1;
}
static inline uint32_t nt_gwes_create(NT_GWES *g) {
    if(!g || g->next_id>=0x10000u)return 0u;
    for(unsigned i=0;i<2u;++i)if(!g->panes[i].id) {
        uint32_t id=0x47570000u|g->next_id++;
        g->panes[i].id=id;g->panes[i].dirty=1u;
        if(!g->focused)g->focused=id;
        nt_gwes_dirty(g);
        return id;
    }
    return 0u;
}
static inline int nt_gwes_close(NT_GWES *g,uint32_t id) {
    int index=nt_gwes_index(g,id);
    if(index<0)return 0;
    g->panes[index]=(NT_GW_PANE){0,0};
    if(g->focused==id)
        g->focused=g->panes[1-index].id;
    g->mode=g->panes[0].id && g->panes[1].id?g->mode:NT_GWES_FULL;
    nt_gwes_dirty(g);return 1;
}
static inline int nt_gwes_setmode(NT_GWES *g,unsigned mode) {
    if(!g || mode>NT_GWES_COLUMNS)return 0;
    if(mode!=NT_GWES_FULL &&
       (!g->panes[0].id || !g->panes[1].id))return 0;
    g->mode=mode;nt_gwes_dirty(g);return 1;
}
static inline int nt_gwes_swap(NT_GWES *g) {
    if(!g || !g->panes[0].id || !g->panes[1].id)return 0;
    g->swapped^=1u;
    if(g->mode==NT_GWES_FULL)
        g->focused=g->panes[g->swapped].id;
    nt_gwes_dirty(g);return 1;
}
static inline int nt_gwes_focus(NT_GWES *g,uint32_t id) {
    if(nt_gwes_index(g,id)<0)return 0;
    g->focused=id;
    if(g->mode==NT_GWES_FULL)
        g->swapped=(unsigned)nt_gwes_index(g,id);
    nt_gwes_dirty(g);return 1;
}
static inline void nt_gwes_sip(NT_GWES *g,unsigned active) {
    if(!g)return;
    g->sip=!!active;nt_gwes_dirty(g);
}
static inline int nt_gwes_rect(const NT_GWES *g,uint32_t id,NT_GW_RECT *out) {
    int idx=nt_gwes_index(g,id);
    if(idx<0 || !out)return 0;
    const int bottom=g->sip?NT_GWES_SIP_TOP:NT_GWES_HEIGHT;
    const unsigned first=g->swapped?1u:0u;
    if(g->mode==NT_GWES_FULL) {
        if(g->focused!=id)return 0;
        *out=(NT_GW_RECT){0,0,NT_GWES_WIDTH,bottom};
    } else if(g->mode==NT_GWES_STACK) {
        const int split=bottom/2;
        *out=(idx==(int)first)?
            (NT_GW_RECT){0,0,NT_GWES_WIDTH,split}:
            (NT_GW_RECT){0,split,NT_GWES_WIDTH,bottom};
    } else if(g->mode==NT_GWES_COLUMNS) {
        *out=(idx==(int)first)?
            (NT_GW_RECT){0,0,NT_GWES_WIDTH/2,bottom}:
            (NT_GW_RECT){NT_GWES_WIDTH/2,0,NT_GWES_WIDTH,bottom};
    } else return 0;
    return out->right>out->left &&
           out->bottom-out->top>=NT_GWES_TITLE;
}
static inline uint32_t nt_gwes_hit(const NT_GWES *g,int x,int y,
                                   unsigned *action) {
    if(action)*action=NT_GWES_NONE;
    if(!g || x<0 || y<0 || x>=NT_GWES_WIDTH ||
       y>=(g->sip?NT_GWES_SIP_TOP:NT_GWES_HEIGHT))return 0u;
    for(unsigned i=0;i<2u;++i) {
        uint32_t id=g->panes[i].id;
        NT_GW_RECT rect;
        if(!id || !nt_gwes_rect(g,id,&rect) ||
           x<rect.left || x>=rect.right ||
           y<rect.top || y>=rect.bottom)continue;
        if(action) {
            if(y>=rect.top+NT_GWES_TITLE)*action=NT_GWES_CLIENT_HIT;
            else if(x>=rect.right-24)*action=NT_GWES_CLOSE_HIT;
            else if(x>=rect.right-48)*action=NT_GWES_SWAP_HIT;
            else if(x>=rect.right-72)*action=NT_GWES_LAYOUT_HIT;
            else *action=NT_GWES_TITLE_HIT;
        }
        return id;
    }
    return 0u;
}
static inline int nt_gwes_consume_dirty(NT_GWES *g,uint32_t id) {
    int idx=nt_gwes_index(g,id);
    if(idx<0)return 0;
    int was=(int)g->panes[idx].dirty;
    g->panes[idx].dirty=0;
    return was;
}
#endif
