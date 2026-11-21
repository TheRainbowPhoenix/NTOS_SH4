/* NTOS original modal dialog and theme model (GWES v2).
 * This is not Hollyhock GUIDialog ABI and does not copy its source.
 * The familiar six height presets / three alignments are public ideas.
 * Up to three overlay dialogs can be nested; all data is bounded.
 * Event ownership always goes to the topmost open dialog. Its parent
 * may be a pane or zero (desktop), with free move/width and SIP clamping.
 */
#ifndef NTOS_GWES_DIALOG_H
#define NTOS_GWES_DIALOG_H
#include "ntgwes_model.h"
#include "nt_touchkb.h"
#define NT_DIALOG_MAX 3u
#define NT_DIALOG_ELEMENTS 6u
#define NT_DIALOG_TITLE 48u
#define NT_DIALOG_OK 0x3EAu
#define NT_DIALOG_CANCEL 0x3EBu
#define NT_DLG_LABEL 1u
#define NT_DLG_BUTTON 2u
#define NT_DLG_EDIT 3u
#define NT_DLG_ALIGN_TOP 0u
#define NT_DLG_ALIGN_CENTER 1u
#define NT_DLG_ALIGN_BOTTOM 2u
#define NT_DLG_THEME_WHITE 0u
#define NT_DLG_THEME_98 1u
#define NT_DLG_THEME_XP 2u
#define NT_DLG_THEME_FLAT 3u
/* Colors are WinCE COLORREF (0x00BBGGRR), not hardware RGB565. */
typedef struct {uint32_t body,title,caption,border,button,ink,accent;} NT_DLG_STYLE;
typedef struct {
    uint32_t size,parent;
    uint32_t height,align,theme,keyboard;
    int x,y,width; /* -1 means auto; explicit position is screen-relative */
    const uint16_t *title;
} NT_DLG_CONFIG;
typedef struct {
    uint32_t size,type,id;
    NT_GW_RECT bounds; /* client coordinates relative to dialog top+24 */
    uint16_t text[48];
} NT_DLG_ITEM;
typedef struct {
    uint32_t token,parent,serial;
    unsigned state,height,align,keyboard,keyboard_prev,theme;
    int x,y,width,focused;
    uint32_t result,event;
    NT_GW_RECT bounds;
    uint16_t title[NT_DIALOG_TITLE];
    NT_DLG_ITEM items[NT_DIALOG_ELEMENTS];
    unsigned count;
    NT_KB_EDIT edit;
} NT_DIALOG;
typedef struct {
    NT_DIALOG slots[NT_DIALOG_MAX];
    uint32_t next_id,serial;
    unsigned theme;
} NT_DIALOGS;
static inline void nt_dlg_init(NT_DIALOGS *s) {
    if(!s)return;
    *s=(NT_DIALOGS){0};
    s->next_id=1u;s->serial=1u;
}
static inline void nt_dlg_copy(uint16_t *to,unsigned cap,const uint16_t *from) {
    unsigned i=0;
    if(from)while(i+1u<cap && from[i]) {
        to[i]=from[i];++i;
    }
    to[i]=0u;
}
static inline int nt_dlg_theme_set(NT_DIALOGS *s,unsigned theme) {
    if(!s || theme>NT_DLG_THEME_FLAT)return 0;
    s->theme=theme;return 1;
}
static inline NT_DLG_STYLE nt_dlg_theme(unsigned theme) {
    switch(theme) {
    case NT_DLG_THEME_98:
        return (NT_DLG_STYLE){0x00C0C0C0u,0x00800000u,
                              0x00FFFFFFu,0x00404040u,
                              0x00C0C0C0u,0x00000000u,0x00800000u};
    case NT_DLG_THEME_XP:
        return (NT_DLG_STYLE){0x00F0F0F0u,0x00DA6828u,
                              0x00FFFFFFu,0x00985223u,
                              0x00F6F6F6u,0x00181818u,0x00DA6828u};
    case NT_DLG_THEME_FLAT:
        return (NT_DLG_STYLE){0x00F7F8FAu,0x00F0F0F0u,
                              0x00222222u,0x00D8D8D8u,
                              0x00FFFFFFu,0x00222222u,0x00AA641Du};
    default:
        return (NT_DLG_STYLE){0x00FFFFFFu,0x00F9F3EDu,
                              0x001D2531u,0x00D5C8BDu,
                              0x00F4F0ECu,0x001D2531u,0x00BC733Bu};
    }
}
static inline NT_DIALOG *nt_dlg_find(NT_DIALOGS *s,uint32_t token) {
    if(!s || !token)return 0;
    for(unsigned i=0;i<NT_DIALOG_MAX;++i)
        if(s->slots[i].token==token && s->slots[i].state)
            return &s->slots[i];
    return 0;
}
static inline NT_DIALOG *nt_dlg_top(NT_DIALOGS *s) {
    if(!s)return 0;
    NT_DIALOG *top=0;
    for(unsigned i=0;i<NT_DIALOG_MAX;++i) {
        NT_DIALOG *d=&s->slots[i];
        if(d->state==1u && (!top || d->serial>top->serial))top=d;
    }
    return top;
}
static inline int nt_dlg_height(int space,unsigned preset) {
    if(preset!=25u && preset!=35u && preset!=55u &&
       preset!=60u && preset!=75u && preset!=95u)return 0;
    int pixels=(space*(int)preset)/100-6;
    if(pixels<76)pixels=76;
    if(pixels>space-4)pixels=space-4;
    return pixels;
}
static inline int nt_dlg_clamp(int x,int lo,int hi) {
    if(hi<lo)return lo;
    return x<lo?lo:(x>hi?hi:x);
}
static inline int nt_dlg_layout(NT_DIALOG *d,const NT_GWES *g) {
    if(!d)return 0;
    const int bottom=(g && g->sip)?NT_GWES_SIP_TOP:NT_GWES_HEIGHT;
    NT_GW_RECT base={0,0,NT_GWES_WIDTH,bottom};
    NT_GW_RECT parent;
    if(d->parent && g && nt_gwes_rect(g,d->parent,&parent))
        base=parent; /* center in the parent, not always the display */
    int available=base.bottom-base.top;
    int height=nt_dlg_height(available,d->height);
    if(!height)return 0;
    /* A docked SIP reduces usable height. Preserve enough room for the
       dialog's existing controls instead of hiding its OK/Cancel row. */
    for(unsigned i=0;i<d->count;++i) {
        int required=d->items[i].bounds.bottom+24+8;
        if(height<required)height=required;
    }
    if(height>bottom-4)height=bottom-4;
    int width=d->width>0?d->width:(base.right-base.left-20);
    width=nt_dlg_clamp(width,96,NT_GWES_WIDTH-8);
    int cx=(base.left+base.right-width)/2;
    int cy=base.top;
    if(d->align==NT_DLG_ALIGN_CENTER)
        cy=base.top+(available-height)/2;
    else if(d->align==NT_DLG_ALIGN_BOTTOM)
        cy=base.bottom-height-5;
    else cy=base.top+5;
    int x=d->x<0?cx:d->x;
    int y=d->y<0?cy:d->y;
    x=nt_dlg_clamp(x,0,NT_GWES_WIDTH-width);
    y=nt_dlg_clamp(y,0,bottom-height);
    d->bounds=(NT_GW_RECT){x,y,x+width,y+height};
    return 1;
}
static inline uint32_t nt_dlg_open(NT_DIALOGS *s,const NT_GWES *g,
                                   const NT_DLG_CONFIG *cfg) {
    if(!s || !cfg || cfg->size!=sizeof(*cfg) || s->next_id>=0xFFFFu ||
       cfg->align>NT_DLG_ALIGN_BOTTOM ||
       (cfg->theme>NT_DLG_THEME_FLAT && cfg->theme!=0xFFFFFFFFu) ||
       (cfg->keyboard!=0u && cfg->keyboard!=1u) ||
       cfg->width>320 || cfg->width<-1 ||
       !nt_dlg_height(528,cfg->height))return 0u;
    if(cfg->parent && (!g || nt_gwes_index(g,cfg->parent)<0))return 0u;
    for(unsigned i=0;i<NT_DIALOG_MAX;++i)
        if(!s->slots[i].state) {
            NT_DIALOG *d=&s->slots[i];
            *d=(NT_DIALOG){0};
            d->token=0x444C0000u|s->next_id++;
            d->serial=s->serial++;
            d->parent=cfg->parent;d->height=cfg->height;
            d->align=cfg->align;d->x=cfg->x;d->y=cfg->y;
            d->width=cfg->width;
            d->theme=cfg->theme==0xFFFFFFFFu?s->theme:cfg->theme;
            d->keyboard=cfg->keyboard;d->state=1u;
            nt_dlg_copy(d->title,NT_DIALOG_TITLE,cfg->title);
            nt_kb_edit_init(&d->edit);
            if(!nt_dlg_layout(d,g)){d->state=0u;return 0u;}
            return d->token;
        }
    return 0u;
}
static inline int nt_dlg_move(NT_DIALOGS *s,NT_GWES *g,uint32_t token,
                              int x,int y) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    if(!d || d->state!=1u)return 0;
    d->x=x;d->y=y;
    return nt_dlg_layout(d,g);
}
static inline int nt_dlg_add(NT_DIALOGS *s,uint32_t token,
                             const NT_DLG_ITEM *item) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    if(!d || d->state!=1u || !item || item->size!=sizeof(*item) ||
       item->type<NT_DLG_LABEL || item->type>NT_DLG_EDIT ||
       d->count==NT_DIALOG_ELEMENTS || item->bounds.left<0 ||
       item->bounds.top<0 || item->bounds.right<=item->bounds.left ||
       item->bounds.bottom<=item->bounds.top ||
       item->bounds.right>d->bounds.right-d->bounds.left-8 ||
       item->bounds.bottom+32>NT_GWES_HEIGHT-4)return 0;
    if((item->type==NT_DLG_BUTTON || item->type==NT_DLG_EDIT) &&
       (item->bounds.right-item->bounds.left<32 ||
        item->bounds.bottom-item->bounds.top<18))return 0;
    d->items[d->count]=*item;
    nt_dlg_copy(d->items[d->count].text,48u,item->text);
    ++d->count;return 1;
}
static inline int nt_dlg_done(NT_DIALOGS *s,uint32_t token,uint32_t result) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    if(!d || d->state!=1u ||
       (result!=NT_DIALOG_OK && result!=NT_DIALOG_CANCEL))return 0;
    d->state=2u;d->result=result;
    return 1;
}
static inline uint32_t nt_dlg_result(NT_DIALOGS *s,uint32_t token) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    return d && d->state==2u?d->result:0u;
}
static inline int nt_dlg_release(NT_DIALOGS *s,uint32_t token) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    if(!d || d->state!=2u)return 0;
    d->state=0u;d->token=0u;return 1;
}
static inline int nt_dlg_route(NT_DIALOGS *s,NT_GWES *g,
                               int x,int y,uint32_t *element) {
    /* Returns 0 if no modal; returns 1 if event is captured outside
       modal, 2 for title/body, 3 for button/element activation.
       Underlying client never receives a modal-blocked click. */
    NT_DIALOG *d=nt_dlg_top(s);
    if(element)*element=0u;
    if(!d)return 0;
    nt_dlg_layout(d,g);
    NT_GW_RECT r=d->bounds;
    if(x<r.left || y<r.top || x>=r.right || y>=r.bottom)return 1;
    if(y<r.top+24) {
        if(x>=r.right-26) {
            nt_dlg_done(s,d->token,NT_DIALOG_CANCEL);
            if(element)*element=NT_DIALOG_CANCEL;
            return 3;
        }
        return 2;
    }
    /* Most recently added hit first, so controls can overlap. */
    for(unsigned i=d->count;i>0u;--i) {
        const NT_DLG_ITEM *item=&d->items[i-1u];
        int l=r.left+item->bounds.left;
        int t=r.top+24+item->bounds.top;
        if(x>=l && x<r.left+item->bounds.right &&
           y>=t && y<r.top+24+item->bounds.bottom) {
            if(item->type==NT_DLG_EDIT) {
                d->focused=(int)i;return 3;
            }
            if(item->type==NT_DLG_BUTTON) {
                d->event=item->id;
                if(element)*element=item->id;
                if(item->id==NT_DIALOG_OK || item->id==NT_DIALOG_CANCEL)
                    nt_dlg_done(s,d->token,item->id);
                return 3;
            }
            return 2;
        }
    }
    return 2;
}
static inline int nt_dlg_key(NT_DIALOGS *s,unsigned ch) {
    NT_DIALOG *d=nt_dlg_top(s);
    if(!d)return 0;
    if(ch==27u) {nt_dlg_done(s,d->token,NT_DIALOG_CANCEL);return 1;}
    if(ch==13u && !d->focused) {
        nt_dlg_done(s,d->token,NT_DIALOG_OK);return 1;
    }
    if(d->focused>0 && d->focused<=(int)d->count &&
       d->items[d->focused-1].type==NT_DLG_EDIT) {
        int rc=nt_kb_edit_char(&d->edit,ch);
        if(rc==2) {d->event=NT_DIALOG_OK;return 1;}
        return rc!=0;
    }
    return 1; /* modal always consumes key input */
}
static inline uint32_t nt_dlg_take_event(NT_DIALOGS *s,uint32_t token) {
    NT_DIALOG *d=nt_dlg_find(s,token);
    if(!d)return 0u;
    uint32_t ev=d->event;d->event=0u;return ev;
}
#endif
