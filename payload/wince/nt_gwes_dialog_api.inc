/* NTOS GWES topmost modal surfaces.
 * The actual COREDLL exports NtosDialog* are original NTOS APIs. Standard
 * WinCE MessageBoxW is layered on them; HWND WinCE clients keep the same
 * calling convention and UTF-16 text. No direct ClassPad GUIDialog/SDK
 * implementation or .msstyles parser is copied or invoked.
 */
static void nt_dialog_init_once(void) {
    gwes_init_if_needed();
    if(!g_nt_dialogs_initialized) {
        nt_dlg_init(&g_nt_dialogs);
        g_nt_dialogs_initialized=1u;
    }
}
static int nt_dialog_active(void) {
    return g_nt_dialogs_initialized && nt_dlg_top(&g_nt_dialogs)!=0;
}
static void nt_dialog_word(int x,int y,const uint16_t *text,unsigned cap,
                           uint32_t color) {
    if(!text || !cap)return;
    unsigned n=0u;
    while(n<cap && text[n])++n;
    if(!n)return;
    void *dc=(void *)(uintptr_t)1u;
    const uint32_t saved=SetTextColor(dc,color);
    TextOutW(dc,x,y,text,(int)n);
    SetTextColor(dc,saved);
}
static void nt_dialog_frame(NT_DIALOG *d) {
    if(!d || d->state!=1u)return;
    NT_DLG_STYLE theme=nt_dlg_theme(d->theme);
    NT_GW_RECT r=d->bounds;
    const int x=r.left,y=r.top,w=r.right-r.left,h=r.bottom-r.top;
    if(w<40 || h<44)return;
    ntos_fillrect((u32)(x+3),(u32)(y+3),
                  (u32)(r.right+3<=320?r.right+3:320),
                  (u32)(r.bottom+3<=528?r.bottom+3:528),
                  0x00B5B8C0u);
    ntos_fillrect((u32)x,(u32)y,(u32)r.right,(u32)r.bottom,
                  theme.border);
    ntos_fillrect((u32)(x+2),(u32)(y+2),
                  (u32)(r.right-2),(u32)(r.bottom-2),theme.body);
    ntos_fillrect((u32)(x+2),(u32)(y+2),
                  (u32)(r.right-2),(u32)(y+24),theme.title);
    const int height_before=g_font_height;
    g_font_height=14;
    nt_dialog_word(x+8,y+6,d->title,(unsigned)(w-40)/8u,
                   theme.caption);
    const u16 close_x[2]={'X',0};
    nt_dialog_word(r.right-18,y+6,close_x,1u,theme.caption);
    for(unsigned i=0u;i<d->count;++i) {
        NT_DLG_ITEM *item=&d->items[i];
        const NT_GW_RECT e=item->bounds;
        int left=x+e.left,top=y+24+e.top;
        int right=x+e.right,bottom=y+24+e.bottom;
        if(right>r.right-3)right=r.right-3;
        if(bottom>r.bottom-3)bottom=r.bottom-3;
        if(left>=right || top>=bottom)continue;
        if(item->type==NT_DLG_BUTTON) {
            ntos_fillrect((u32)left,(u32)top,
                          (u32)right,(u32)bottom,theme.border);
            ntos_fillrect((u32)(left+1),(u32)(top+1),
                          (u32)(right-1),(u32)(bottom-1),
                          item->id==NT_DIALOG_OK?theme.accent:theme.button);
            nt_dialog_word(left+8,top+5,item->text,
                           (unsigned)(right-left-12)/8u,
                           item->id==NT_DIALOG_OK?0x00FFFFFFu:theme.ink);
        } else if(item->type==NT_DLG_EDIT) {
            ntos_fillrect((u32)left,(u32)top,
                          (u32)right,(u32)bottom,theme.border);
            ntos_fillrect((u32)(left+1),(u32)(top+1),
                          (u32)(right-1),(u32)(bottom-1),0x00FFFFFFu);
            const unsigned room=(unsigned)(right-left-8)/8u;
            const unsigned len=d->edit.length;
            const unsigned skip=len>room?len-room:0u;
            nt_dialog_word(left+4,top+4,d->edit.text+skip,
                           room,theme.ink);
        } else if(item->type==NT_DLG_LABEL) {
            nt_dialog_word(left,top+2,item->text,
                           (unsigned)(right-left)/8u,theme.ink);
        }
    }
    g_font_height=height_before;
}
static void nt_dialog_paint(void) {
    if(!nt_dialog_active())return;
    /* Screen is redrawn by foreground app. SIP is always foreground to
       the app but behind its modal dialog. No alpha blending or AA. */
    if(nt_sip_flags&1u)nt_sip_paint();
    uint32_t after=0u;
    for(unsigned pass=0u;pass<NT_DIALOG_MAX;++pass) {
        NT_DIALOG *next=0;
        for(unsigned i=0u;i<NT_DIALOG_MAX;++i) {
            NT_DIALOG *d=&g_nt_dialogs.slots[i];
            if(d->state==1u && d->serial>after &&
               (!next || d->serial<next->serial))next=d;
        }
        if(!next)break;
        nt_dlg_layout(next,&g_nt_gwes);
        nt_dialog_frame(next);
        after=next->serial;
    }
}
int NtosThemeSet(u32 theme) {
    nt_dialog_init_once();
    if(!nt_dlg_theme_set(&g_nt_dialogs,theme))return 0;
    for(unsigned i=0;i<NT_DIALOG_MAX;++i)
        if(g_nt_dialogs.slots[i].state==1u)
            g_nt_dialogs.slots[i].theme=theme;
    nt_dialog_paint();coredll_refresh();
    return 1;
}
u32 NtosThemeGet(void) {
    nt_dialog_init_once();return g_nt_dialogs.theme;
}
u32 NtosDialogOpen(const NT_DLG_CONFIG *cfg) {
    nt_dialog_init_once();
    if(!cfg || cfg->size!=sizeof(*cfg))return 0u;
    const u32 prior=nt_sip_flags&1u;
    const u32 wanted=cfg->keyboard?1u:0u;
    if(prior!=wanted && !SipShowIM(wanted))return 0u;
    uint32_t id=nt_dlg_open(&g_nt_dialogs,&g_nt_gwes,cfg);
    if(!id) {
        if(prior!=wanted)SipShowIM(prior);
        return 0u;
    }
    NT_DIALOG *d=nt_dlg_find(&g_nt_dialogs,id);
    if(d)d->keyboard_prev=prior;
    nt_dialog_paint();coredll_refresh();
    return id;
}
int NtosDialogAdd(u32 dialog,const NT_DLG_ITEM *item) {
    nt_dialog_init_once();
    if(!nt_dlg_add(&g_nt_dialogs,dialog,item))return 0;
    nt_dialog_paint();coredll_refresh();return 1;
}
int NtosDialogMove(u32 dialog,int x,int y) {
    nt_dialog_init_once();
    if(!nt_dlg_move(&g_nt_dialogs,&g_nt_gwes,dialog,x,y))return 0;
    /* Repaint the underlying top-level window to erase old rectangle.
       The next composited pass draws the dialog at its new position. */
    if(g_window.magic) {
        g_window.invalidated=1;
        UpdateWindow((void *)&g_window);
    } else {nt_dialog_paint();coredll_refresh();}
    return 1;
}
int NtosDialogGetRect(u32 dialog,NT_RECT *out) {
    nt_dialog_init_once();
    NT_DIALOG *d=nt_dlg_find(&g_nt_dialogs,dialog);
    if(!d || !out || !nt_dlg_layout(d,&g_nt_gwes))return 0;
    out->left=d->bounds.left;out->top=d->bounds.top;
    out->right=d->bounds.right;out->bottom=d->bounds.bottom;
    return 1;
}
u32 NtosDialogTop(void) {
    nt_dialog_init_once();
    NT_DIALOG *d=nt_dlg_top(&g_nt_dialogs);
    return d?d->token:0u;
}
static void nt_dialog_finished(NT_DIALOG *d) {
    if(!d || d->state!=2u)return;
    const u32 keyboard=d->keyboard_prev;
    if((nt_sip_flags&1u)!=keyboard)SipShowIM(keyboard);
    if(g_window.magic) {
        g_window.invalidated=1;UpdateWindow((void *)&g_window);
    } else {
        nt_dialog_paint();coredll_refresh();
    }
}
int NtosDialogEnd(u32 dialog,u32 result) {
    nt_dialog_init_once();
    if(!nt_dlg_done(&g_nt_dialogs,dialog,result))return 0;
    nt_dialog_finished(nt_dlg_find(&g_nt_dialogs,dialog));
    return 1;
}
u32 NtosDialogResult(u32 dialog) {
    nt_dialog_init_once();return nt_dlg_result(&g_nt_dialogs,dialog);
}
int NtosDialogRelease(u32 dialog) {
    nt_dialog_init_once();return nt_dlg_release(&g_nt_dialogs,dialog);
}
u32 NtosDialogEvent(u32 dialog) {
    nt_dialog_init_once();return nt_dlg_take_event(&g_nt_dialogs,dialog);
}
int NtosDialogGetText(u32 dialog,u16 *text,int capacity) {
    nt_dialog_init_once();
    NT_DIALOG *d=nt_dlg_find(&g_nt_dialogs,dialog);
    if(!d || !text || capacity<=0)return 0;
    int n=0;
    while(n+1<capacity && n<(int)d->edit.length) {
        text[n]=d->edit.text[n];++n;
    }
    text[n]=0u;return n;
}
int NtosDialogSetText(u32 dialog,u32 element,const u16 *text) {
    nt_dialog_init_once();
    NT_DIALOG *d=nt_dlg_find(&g_nt_dialogs,dialog);
    if(!d || d->state!=1u)return 0;
    for(unsigned i=0;i<d->count;++i)
        if(d->items[i].id==element) {
            nt_dlg_copy(d->items[i].text,48u,text);
            nt_dialog_paint();coredll_refresh();return 1;
        }
    return 0;
}
int NtosDialogDraw(void) {
    nt_dialog_init_once();
    nt_dialog_paint();coredll_refresh();return 1;
}
static u32 nt_dialog_drag;
static int nt_dialog_drag_x,nt_dialog_drag_y;
static int nt_dialog_capture(const NT_MSG *msg) {
    if(!msg || !nt_dialog_active())return 0;
    NT_DIALOG *d=nt_dlg_top(&g_nt_dialogs);
    if(!d)return 0;
    if(msg->message==0x0201u) { /* touch down */
        const u32 id=d->token;
        uint32_t element=0u; /* typed model's uint32_t* */
        int title_drag=msg->pt.y>=d->bounds.top &&
             msg->pt.y<d->bounds.top+24 &&
             msg->pt.x>=d->bounds.left &&
             msg->pt.x<d->bounds.right-26;
        int routed=nt_dlg_route(&g_nt_dialogs,&g_nt_gwes,
                                msg->pt.x,msg->pt.y,&element);
        if(title_drag && d->state==1u) {
            nt_dialog_drag=d->token;
            nt_dialog_drag_x=msg->pt.x-d->bounds.left;
            nt_dialog_drag_y=msg->pt.y-d->bounds.top;
        }
        if(d->state==2u) {
            nt_dialog_drag=0u;
            nt_dialog_finished(d);
        } else if(routed==3 && d->focused &&
                  (nt_sip_flags&1u)==0u) {
            SipShowIM(1u);
            nt_dialog_paint();coredll_refresh();
        } else if(routed==3u) {
            if(element && element!=NT_DIALOG_OK &&
               element!=NT_DIALOG_CANCEL && g_window.magic)
                enqueue((void *)&g_window,0x0111u,element,id);
            nt_dialog_paint();coredll_refresh();
        }
        return 1;
    }
    if(msg->message==0x0200u) {
        if(nt_dialog_drag && nt_dialog_drag==d->token)
            NtosDialogMove(d->token,msg->pt.x-nt_dialog_drag_x,
                          msg->pt.y-nt_dialog_drag_y);
        return 1;
    }
    if(msg->message==0x0202u) {
        nt_dialog_drag=0u;return 1;
    } /* background pointer/drag/up capture */
    if(msg->message==0x0102u || msg->message==0x0100u) {
        unsigned ch=msg->wParam;
        nt_dlg_key(&g_nt_dialogs,ch);
        if(d->state==2u)nt_dialog_finished(d);
        else {nt_dialog_paint();coredll_refresh();}
        return 1;
    }
    return 0;
}
/* WinCE MessageBoxW: synchronous, real foreground input-modal dialog.
 * Only MB_OK (0) and MB_OKCANCEL (1) are implemented; unsupported button
 * combinations fail rather than impersonating WinCE API semantics. */
int MessageBoxW(void *hwnd,const u16 *message,const u16 *title,u32 type) {
    if((type&0xFu)!=0u && (type&0xFu)!=1u)return 0;
    (void)hwnd;
    NT_DLG_CONFIG config={
        sizeof(config),0u,35u,NT_DLG_ALIGN_CENTER,
        0xFFFFFFFFu,0u,-1,-1,280,title
    };
    u32 dlg=NtosDialogOpen(&config);
    if(!dlg)return 0;
    NT_DLG_ITEM item={0};
    item.size=sizeof(item);
    item.type=NT_DLG_LABEL;item.id=1u;
    item.bounds=(NT_GW_RECT){14,12,260,54};
    nt_dlg_copy(item.text,48u,message);
    if(!NtosDialogAdd(dlg,&item)) {
        NtosDialogEnd(dlg,NT_DIALOG_CANCEL);
        NtosDialogRelease(dlg);return 0;
    }
    item.type=NT_DLG_BUTTON;item.id=NT_DIALOG_OK;
    item.bounds=(NT_GW_RECT){86,70,180,102};
    const u16 ok[]={'O','K',0};
    nt_dlg_copy(item.text,48u,ok);
    if(!NtosDialogAdd(dlg,&item)) {
        NtosDialogEnd(dlg,NT_DIALOG_CANCEL);
        NtosDialogRelease(dlg);return 0;
    }
    if((type&0xFu)==1u) {
        NT_DLG_ITEM cancel={0};
        cancel.size=sizeof(cancel);cancel.type=NT_DLG_BUTTON;
        cancel.id=NT_DIALOG_CANCEL;
        cancel.bounds=(NT_GW_RECT){178,70,266,102};
        const u16 caption_cancel[]={'C','a','n','c','e','l',0};
        nt_dlg_copy(cancel.text,48u,caption_cancel);
        if(!NtosDialogAdd(dlg,&cancel)) {
            NtosDialogEnd(dlg,NT_DIALOG_CANCEL);
            NtosDialogRelease(dlg);return 0;
        }
    }
    for(unsigned turns=0u;!NtosDialogResult(dlg) && turns<4096u;++turns) {
        NT_MSG msg={0};
        int code=GetMessageW(&msg,hwnd,0u,0u);
        if(code<=0) {
            NtosDialogEnd(dlg,NT_DIALOG_CANCEL);break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    u32 result=NtosDialogResult(dlg);
    if(!result)NtosDialogEnd(dlg,NT_DIALOG_CANCEL);
    NtosDialogRelease(dlg);
    return result==NT_DIALOG_OK?1:2;
}
