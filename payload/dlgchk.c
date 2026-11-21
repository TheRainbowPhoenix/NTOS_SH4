/* DLGCHK.EXE: deterministic SH4BE WinCE COREDLL modal/GWES canary.
 * Expected 0x444C4731 (DLG1). Failure BADF91xx pinpoints stage.
 * All drawing uses actual COREDLL integer RGB565 pixel/rectangle path.
 * No modal blocking input needed to execute the program.
 */
#include <stdint.h>
#include "ntimport.h"
typedef uint32_t U;
typedef uint16_t W;
typedef struct {int l,t,r,b;} RECT;
typedef struct {
    U size,parent,height,align,theme,keyboard;
    int x,y,width;
    const W *title;
} CONFIG;
typedef struct {
    U size,type,id;
    RECT bounds;
    W text[48];
} ITEM;
NT_IMPORT(int,NtosThemeSet,(U))
NT_IMPORT(U,NtosThemeGet,(void))
NT_IMPORT(U,NtosDialogOpen,(const CONFIG *))
NT_IMPORT(int,NtosDialogAdd,(U,const ITEM *))
NT_IMPORT(int,NtosDialogMove,(U,int,int))
NT_IMPORT(int,NtosDialogGetRect,(U,RECT *))
NT_IMPORT(U,NtosDialogTop,(void))
NT_IMPORT(int,NtosDialogEnd,(U,U))
NT_IMPORT(U,NtosDialogResult,(U))
NT_IMPORT(int,NtosDialogRelease,(U))
NT_IMPORT(int,NtosDialogSetText,(U,U,const W *))
NT_IMPORT(int,NtosDialogDraw,(void))
NT_IMPORT(U,NtosGwesCreate,(void))
NT_IMPORT(int,NtosGwesClose,(U))
NT_IMPORT(int,NtosGwesSetMode,(U))
NT_IMPORT(int,NtosGwesReset,(void))
NT_DEFINE(NtosThemeSet)
NT_DEFINE(NtosThemeGet)
NT_DEFINE(NtosDialogOpen)
NT_DEFINE(NtosDialogAdd)
NT_DEFINE(NtosDialogMove)
NT_DEFINE(NtosDialogGetRect)
NT_DEFINE(NtosDialogTop)
NT_DEFINE(NtosDialogEnd)
NT_DEFINE(NtosDialogResult)
NT_DEFINE(NtosDialogRelease)
NT_DEFINE(NtosDialogSetText)
NT_DEFINE(NtosDialogDraw)
NT_DEFINE(NtosGwesCreate)
NT_DEFINE(NtosGwesClose)
NT_DEFINE(NtosGwesSetMode)
NT_DEFINE(NtosGwesReset)
static const W title[]={'G','W','E','S',' ','D','i','a','l','o','g',0};
static const W body[]={'P','i','x','e','l',' ','t','h','e','m','e',0};
static const U heights[6]={25u,35u,55u,60u,75u,95u};
static void ui_nocrt_zero(void *ptr,unsigned bytes) {
    volatile unsigned char *p=(volatile unsigned char *)ptr;
    for(unsigned i=0;i<bytes;++i)p[i]=0u;
}
int main(const void *const *api,char **argv,char **envp) {
    (void)api;(void)argv;(void)envp;
    if(!NT_CALL(NtosGwesReset,()))
        return (int)0xBADF9101u;
    U a=NT_CALL(NtosGwesCreate,());
    U b=NT_CALL(NtosGwesCreate,());
    if(!a || !b || !NT_CALL(NtosGwesSetMode,(1u)))
        return (int)0xBADF9102u;
    /* Check six model states without showing six partial redraws. */
    for(U i=0;i<6u;++i) {
        if(!NT_CALL(NtosThemeSet,(i%4u)) ||
           NT_CALL(NtosThemeGet,())!=(i%4u))
            return (int)0xBADF9103u;
        CONFIG cfg={sizeof(cfg),i&1u?a:0u,heights[i],i%3u,
                    0xFFFFFFFFu,0u,-1,-1,260,title};
        U id=NT_CALL(NtosDialogOpen,(&cfg));
        RECT rc={0,0,0,0};
        if(!id || NT_CALL(NtosDialogTop,())!=id ||
           !NT_CALL(NtosDialogGetRect,(id,&rc)) ||
           rc.l<0 || rc.t<0 || rc.r>320 || rc.b>528 ||
           rc.r-rc.l!=260 || rc.b-rc.t<70)
            return (int)0xBADF9104u;
        ITEM label;
        ui_nocrt_zero(&label,sizeof(label));
        label.size=sizeof(label);label.type=1u;label.id=11u;
        label.bounds=(RECT){12,12,228,33};
        if(!NT_CALL(NtosDialogAdd,(id,&label)) ||
           !NT_CALL(NtosDialogSetText,(id,11u,body)))
            return (int)0xBADF9105u;
        ITEM ok;
        ui_nocrt_zero(&ok,sizeof(ok));
        ok.size=sizeof(ok);ok.type=2u;ok.id=0x3EAu;
        ok.bounds=(RECT){12,42,116,76};
        ok.text[0]='O';ok.text[1]='K';ok.text[2]=0u;
        if(!NT_CALL(NtosDialogAdd,(id,&ok)))
            return (int)0xBADF9106u;
        if(!NT_CALL(NtosDialogMove,(id,10+(int)i*12,8+(int)i*15)) ||
           !NT_CALL(NtosDialogGetRect,(id,&rc)) ||
           rc.l<0 || rc.t<0 || rc.r>320 || rc.b>528)
            return (int)0xBADF9107u;
        if((i==5u && !NT_CALL(NtosDialogDraw,())) ||
           !NT_CALL(NtosDialogEnd,(id,0x3EAu)) ||
           NT_CALL(NtosDialogResult,(id))!=0x3EAu ||
           !NT_CALL(NtosDialogRelease,(id)))
            return (int)0xBADF9108u;
    }
    CONFIG cfg={sizeof(cfg),0u,35u,1u,0xFFFFFFFFu,
                0u,-1,-1,260,title};
    U lower=NT_CALL(NtosDialogOpen,(&cfg));
    U higher=NT_CALL(NtosDialogOpen,(&cfg));
    if(!lower || !higher || lower==higher ||
       NT_CALL(NtosDialogTop,())!=higher)
        return (int)0xBADF9109u;
    if(!NT_CALL(NtosDialogEnd,(higher,0x3EBu)) ||
       NT_CALL(NtosDialogResult,(higher))!=0x3EBu ||
       NT_CALL(NtosDialogTop,())!=lower ||
       !NT_CALL(NtosDialogRelease,(higher)))
        return (int)0xBADF910Au;
    if(!NT_CALL(NtosDialogEnd,(lower,0x3EAu)) ||
       !NT_CALL(NtosDialogRelease,(lower)) ||
       NT_CALL(NtosDialogTop,())!=0u)
        return (int)0xBADF910Bu;
    cfg.keyboard=1u;
    U keyboard=NT_CALL(NtosDialogOpen,(&cfg));
    RECT rc={0,0,0,0};
    if(!keyboard ||
       !NT_CALL(NtosDialogGetRect,(keyboard,&rc)) ||
       rc.b>271 || !NT_CALL(NtosDialogEnd,(keyboard,0x3EAu)) ||
       !NT_CALL(NtosDialogRelease,(keyboard)))
        return (int)0xBADF910Cu;
    if(!NT_CALL(NtosGwesClose,(a)) ||
       !NT_CALL(NtosGwesClose,(b)) ||
       !NT_CALL(NtosGwesReset,()) ||
       !NT_CALL(NtosThemeSet,(0u)))
        return (int)0xBADF910Du;
    return (int)0x444C4731u;
}
