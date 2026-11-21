#include "../payload/wince/nt_gwes_dialog.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    NT_GWES g;NT_DIALOGS s;
    nt_gwes_init(&g);nt_dlg_init(&s);
    uint32_t p=nt_gwes_create(&g),q=nt_gwes_create(&g);
    assert(p && q && nt_gwes_setmode(&g,NT_GWES_STACK));
    const unsigned heights[]={25u,35u,55u,60u,75u,95u};
    for(unsigned h=0;h<6u;++h)
      for(unsigned align=0;align<3u;++align) {
        NT_DLG_CONFIG c={sizeof(c),p,heights[h],align,0u,0u,-1,-1,0,0};
        uint32_t token=nt_dlg_open(&s,&g,&c);
        assert(token);
        NT_DIALOG *d=nt_dlg_find(&s,token);
        assert(d && d->bounds.left>=0 && d->bounds.right<=320 &&
               d->bounds.bottom<=264 && d->bounds.top>=0);
        if(align==NT_DLG_ALIGN_CENTER)
            assert(d->bounds.top>0);
        assert(nt_dlg_done(&s,token,NT_DIALOG_CANCEL));
        assert(nt_dlg_result(&s,token)==NT_DIALOG_CANCEL);
        assert(nt_dlg_release(&s,token));
        assert(!nt_dlg_find(&s,token));
      }
    const unsigned themes[]={0u,1u,2u,3u};
    for(unsigned i=0;i<4u;++i) {
        assert(nt_dlg_theme_set(&s,themes[i]));
        NT_DLG_STYLE color=nt_dlg_theme(s.theme);
        assert(color.body!=0u && color.button!=0u && color.ink!=0u);
    }
    assert(!nt_dlg_theme_set(&s,4u));
    NT_DLG_CONFIG c={sizeof(c),p,55u,NT_DLG_ALIGN_CENTER,
                     NT_DLG_THEME_XP,0u,-1,-1,260,0};
    uint32_t a=nt_dlg_open(&s,&g,&c);
    assert(a);
    NT_DIALOG *da=nt_dlg_find(&s,a);
    assert(da && da->parent==p);
    NT_DLG_ITEM label={sizeof(label),NT_DLG_LABEL,100u,{12,10,200,32},{'H','i',0}};
    NT_DLG_ITEM edit={sizeof(edit),NT_DLG_EDIT,110u,{12,34,200,59},{0}};
    NT_DLG_ITEM ok={sizeof(ok),NT_DLG_BUTTON,NT_DIALOG_OK,{12,67,112,95},{'O','K',0}};
    NT_DLG_ITEM cancel={sizeof(cancel),NT_DLG_BUTTON,NT_DIALOG_CANCEL,{120,67,222,95},{'N','o',0}};
    assert(nt_dlg_add(&s,a,&label));
    assert(nt_dlg_add(&s,a,&edit));
    assert(nt_dlg_add(&s,a,&ok));
    assert(nt_dlg_add(&s,a,&cancel));
    NT_DLG_CONFIG c2={sizeof(c2),0,35u,NT_DLG_ALIGN_BOTTOM,
                      NT_DLG_THEME_WHITE,1u,40,60,228,0};
    uint32_t b=nt_dlg_open(&s,&g,&c2);
    assert(b && nt_dlg_top(&s)->token==b);
    uint32_t element=0u;
    assert(nt_dlg_route(&s,&g,0,0,&element)==1);
    assert(nt_dlg_result(&s,a)==0u);
    NT_DIALOG *db=nt_dlg_find(&s,b);
    assert(db);
    assert(nt_dlg_move(&s,&g,b,180,230));
    assert(db->bounds.right==320 && db->bounds.bottom<=528);
    int closex=db->bounds.right-10,closey=db->bounds.top+9;
    assert(nt_dlg_route(&s,&g,closex,closey,&element)==3);
    assert(element==NT_DIALOG_CANCEL && nt_dlg_result(&s,b)==NT_DIALOG_CANCEL);
    assert(nt_dlg_top(&s)->token==a);
    assert(nt_dlg_release(&s,b));
    NT_GW_RECT r=da->bounds;
    assert(nt_dlg_route(&s,&g,r.left+15,r.top+24+45,&element)==3);
    assert(da->focused==2);
    assert(nt_dlg_key(&s,'O') && nt_dlg_key(&s,'K'));
    assert(da->edit.length==2u && da->edit.text[0]=='O');
    assert(nt_dlg_key(&s,8u) && da->edit.length==1u);
    assert(nt_dlg_route(&s,&g,r.left+20,r.top+24+82,&element)==3);
    assert(element==NT_DIALOG_OK && nt_dlg_result(&s,a)==NT_DIALOG_OK);
    assert(nt_dlg_release(&s,a));
    assert(!nt_dlg_release(&s,a));
    assert(!nt_dlg_find(&s,a));
    /* Focused panes change geometry without losing modal bounds. */
    nt_gwes_sip(&g,1u);
    c.parent=q;
    c.height=95u;c.align=NT_DLG_ALIGN_CENTER;
    uint32_t e=nt_dlg_open(&s,&g,&c);
    assert(e);
    NT_DIALOG *de=nt_dlg_find(&s,e);
    assert(de->bounds.bottom<=271);
    assert(nt_dlg_move(&s,&g,e,-1,-1));
    assert(de->bounds.top>=0);
    assert(nt_dlg_key(&s,27u));
    assert(nt_dlg_result(&s,e)==NT_DIALOG_CANCEL);
    assert(nt_dlg_release(&s,e));
    /* Avoid stale aliasing across 10,000 begin/end lifetimes. */
    uint32_t last=e;
    for(unsigned j=0;j<10000u;++j) {
        uint32_t id=nt_dlg_open(&s,&g,&c);
        assert(id && id!=last);
        assert(nt_dlg_done(&s,id,NT_DIALOG_OK));
        assert(nt_dlg_release(&s,id));
        last=id;
    }
    puts("PASS dialog: 6 heights x 3 aligns, 4 themes, parent centered and free move, stacked modal focus, OK/Cancel, edit, SIP bounds, 10000 stale tokens");
}
