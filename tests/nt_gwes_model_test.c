#include "../payload/wince/ntgwes_model.h"
#include <assert.h>
#include <stdio.h>
static void check_rect(NT_GWES *g,uint32_t id,int l,int t,int r,int b) {
    NT_GW_RECT rect={-1,-1,-1,-1};
    assert(nt_gwes_rect(g,id,&rect));
    assert(rect.left==l && rect.top==t &&
           rect.right==r && rect.bottom==b);
}
int main(void) {
    NT_GWES g;nt_gwes_init(&g);
    assert(!nt_gwes_setmode(&g,NT_GWES_STACK));
    uint32_t a=nt_gwes_create(&g),b=nt_gwes_create(&g);
    assert(a && b && a!=b && !nt_gwes_create(&g));
    check_rect(&g,a,0,0,320,528);
    NT_GW_RECT rect;
    assert(!nt_gwes_rect(&g,b,&rect));
    assert(nt_gwes_consume_dirty(&g,a) && !nt_gwes_consume_dirty(&g,a));
    assert(nt_gwes_setmode(&g,NT_GWES_STACK));
    check_rect(&g,a,0,0,320,264);
    check_rect(&g,b,0,264,320,528);
    unsigned action=0;
    assert(nt_gwes_hit(&g,10,10,&action)==a && action==NT_GWES_TITLE_HIT);
    assert(nt_gwes_hit(&g,310,10,&action)==a && action==NT_GWES_CLOSE_HIT);
    assert(nt_gwes_hit(&g,281,10,&action)==a && action==NT_GWES_SWAP_HIT);
    assert(nt_gwes_hit(&g,251,10,&action)==a && action==NT_GWES_LAYOUT_HIT);
    assert(nt_gwes_hit(&g,20,100,&action)==a && action==NT_GWES_CLIENT_HIT);
    assert(nt_gwes_hit(&g,20,265,&action)==b && action==NT_GWES_TITLE_HIT);
    assert(nt_gwes_setmode(&g,NT_GWES_COLUMNS));
    check_rect(&g,a,0,0,160,528);
    check_rect(&g,b,160,0,320,528);
    assert(nt_gwes_hit(&g,159,400,&action)==a);
    assert(nt_gwes_hit(&g,160,400,&action)==b);
    assert(nt_gwes_swap(&g));
    check_rect(&g,b,0,0,160,528);
    check_rect(&g,a,160,0,320,528);
    nt_gwes_sip(&g,1u);
    check_rect(&g,b,0,0,160,271);
    check_rect(&g,a,160,0,320,271);
    assert(!nt_gwes_hit(&g,50,271,&action) && action==NT_GWES_NONE);
    assert(nt_gwes_setmode(&g,NT_GWES_STACK));
    check_rect(&g,b,0,0,320,135);
    check_rect(&g,a,0,135,320,271);
    /* Repeated mode changes never overlap, leave screen, or cross keyboard. */
    for(unsigned n=0;n<2500;++n) {
        assert(nt_gwes_setmode(&g,n%2u?NT_GWES_STACK:NT_GWES_COLUMNS));
        assert(nt_gwes_swap(&g));
        NT_GW_RECT ra,rb;
        assert(nt_gwes_rect(&g,a,&ra) && nt_gwes_rect(&g,b,&rb));
        assert(ra.left>=0 && ra.top>=0 &&
               ra.right<=320 && ra.bottom<=271);
        assert(rb.left>=0 && rb.top>=0 &&
               rb.right<=320 && rb.bottom<=271);
        assert(ra.bottom<=rb.top || rb.bottom<=ra.top ||
               ra.right<=rb.left || rb.right<=ra.left);
    }
    assert(!nt_gwes_setmode(&g,3));
    assert(!nt_gwes_focus(&g,0xDEADBEEFu));
    assert(!nt_gwes_close(&g,0xDEADBEEFu));
    assert(nt_gwes_setmode(&g,NT_GWES_FULL));
    assert(nt_gwes_focus(&g,a));
    check_rect(&g,a,0,0,320,271);
    assert(!nt_gwes_rect(&g,b,&rect));
    assert(nt_gwes_close(&g,a));
    assert(nt_gwes_index(&g,a)<0 && nt_gwes_index(&g,b)>=0);
    assert(!nt_gwes_setmode(&g,NT_GWES_STACK));
    assert(nt_gwes_close(&g,b));
    assert(!nt_gwes_rect(&g,b,&rect));
    uint32_t c=nt_gwes_create(&g);
    assert(c && c!=a && c!=b);
    puts("PASS GWES two panes: fullscreen, stack/columns rotation, swap, exact title hit, SIP occlusion, 2500 geometry cycles and stale handle rejection");
}
