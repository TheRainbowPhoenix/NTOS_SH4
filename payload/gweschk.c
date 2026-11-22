/* GWESCHK.EXE: real SH4BE WinCE/GWES COREDLL import-contract canary.
 * Validates two *logical* window handles (not KPROCESS HWNDs), fullscreen,
 * top/bottom, half-width rotated layout, swap, SIP occlusion, focus,
 * 8x8->32x32 nearest-neighbour RGB565 GDI and cleanup.
 * Expected 0x47574531 (GWE1); failures 0xBADF8nxx.
 */
#include <stdint.h>
#include "ntimport.h"
typedef uint32_t U;
typedef struct {int left,top,right,bottom;} RECT;
NT_IMPORT(U,NtosGwesCreate,(void))
NT_IMPORT(int,NtosGwesClose,(U))
NT_IMPORT(int,NtosGwesSetMode,(U))
NT_IMPORT(U,NtosGwesGetMode,(void))
NT_IMPORT(int,NtosGwesSwap,(void))
NT_IMPORT(int,NtosGwesFocus,(U))
NT_IMPORT(int,NtosGwesGetRect,(U,RECT *))
NT_IMPORT(U,NtosGwesHit,(int,int,U *))
NT_IMPORT(int,NtosGwesKeyboard,(U))
NT_IMPORT(int,NtosGwesReset,(void))
NT_IMPORT(void *,GetDC,(void *))
NT_IMPORT(int,ReleaseDC,(void *,void *))
NT_IMPORT(void *,CreateCompatibleDC,(void *))
NT_IMPORT(int,DeleteDC,(void *))
NT_IMPORT(void *,CreateCompatibleBitmap,(void *,int,int))
NT_IMPORT(void *,SelectObject,(void *,void *))
NT_IMPORT(int,DeleteObject,(void *))
NT_IMPORT(U,SetPixel,(void *,int,int,U))
NT_IMPORT(U,GetPixel,(void *,int,int))
NT_IMPORT(int,StretchBlt,(void *,int,int,int,int,void *,int,int,int,int,U))
NT_IMPORT(void *,CreateSolidBrush,(U))
NT_IMPORT(int,FillRect,(void *,const RECT *,void *))
NT_DEFINE(NtosGwesCreate)
NT_DEFINE(NtosGwesClose)
NT_DEFINE(NtosGwesSetMode)
NT_DEFINE(NtosGwesGetMode)
NT_DEFINE(NtosGwesSwap)
NT_DEFINE(NtosGwesFocus)
NT_DEFINE(NtosGwesGetRect)
NT_DEFINE(NtosGwesHit)
NT_DEFINE(NtosGwesKeyboard)
NT_DEFINE(NtosGwesReset)
NT_DEFINE(GetDC)
NT_DEFINE(ReleaseDC)
NT_DEFINE(CreateCompatibleDC)
NT_DEFINE(DeleteDC)
NT_DEFINE(CreateCompatibleBitmap)
NT_DEFINE(SelectObject)
NT_DEFINE(DeleteObject)
NT_DEFINE(SetPixel)
NT_DEFINE(GetPixel)
NT_DEFINE(StretchBlt)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
static int equals(const RECT *r,int l,int t,int x,int y) {
    return r->left==l && r->top==t && r->right==x && r->bottom==y;
}
int main(const void *const *api,char **argv,char **envp) {
    (void)api;(void)argv;(void)envp;
    U a=NT_CALL(NtosGwesCreate,()),b=NT_CALL(NtosGwesCreate,());
    RECT r={0,0,0,0};U action=0;
    if(!a || !b || a==b || NT_CALL(NtosGwesCreate,())!=0u)
        return (int)0xBADF8101u;
    if(!NT_CALL(NtosGwesGetRect,(a,&r)) || !equals(&r,0,0,320,528) ||
        NT_CALL(NtosGwesGetRect,(b,&r))!=0)
        return (int)0xBADF8102u;
    if(!NT_CALL(NtosGwesSetMode,(1u)) ||
       !NT_CALL(NtosGwesGetRect,(a,&r)) || !equals(&r,0,0,320,264) ||
       !NT_CALL(NtosGwesGetRect,(b,&r)) || !equals(&r,0,264,320,528))
        return (int)0xBADF8103u;
    if(!NT_CALL(NtosGwesSetMode,(2u)) ||
       !NT_CALL(NtosGwesGetRect,(b,&r)) || !equals(&r,160,0,320,528) ||
       NT_CALL(NtosGwesHit,(310,4,&action))!=b || action!=4u)
        return (int)0xBADF8104u;
    if(!NT_CALL(NtosGwesSwap,()) ||
       !NT_CALL(NtosGwesGetRect,(b,&r)) || !equals(&r,0,0,160,528) ||
       !NT_CALL(NtosGwesKeyboard,(1u)) ||
       !NT_CALL(NtosGwesGetRect,(a,&r)) || !equals(&r,160,0,320,271) ||
       NT_CALL(NtosGwesHit,(12,325,&action))!=0u)
        return (int)0xBADF8105u;
    if(!NT_CALL(NtosGwesKeyboard,(0u)) ||
       !NT_CALL(NtosGwesSetMode,(0u)) ||
       !NT_CALL(NtosGwesFocus,(a)) ||
       !NT_CALL(NtosGwesGetRect,(a,&r)) || !equals(&r,0,0,320,528) ||
       NT_CALL(NtosGwesGetRect,(b,&r))!=0)
        return (int)0xBADF8106u;
    /* Now exercise real WinCE GDI offscreen allocation, bitmap selection,
     * COLORREF conversion and nearest-neighbour 8->32 StretchBlt. */
    void *screen=NT_CALL(GetDC,(0));
    void *mem=screen?NT_CALL(CreateCompatibleDC,(screen)):0;
    void *bitmap=mem?NT_CALL(CreateCompatibleBitmap,(screen,8,8)):0;
    if(!screen || !mem || !bitmap)
        return (int)0xBADF8107u;
    void *old=NT_CALL(SelectObject,(mem,bitmap));
    if(!old || NT_CALL(DeleteObject,(bitmap))!=0)
        return (int)0xBADF8108u;
    RECT all={0,0,8,8};
    void *white=NT_CALL(CreateSolidBrush,(0x00FFFFFFu));
    if(!white || !NT_CALL(FillRect,(mem,&all,white)) ||
       NT_CALL(SetPixel,(mem,0,0,0x000000FFu))!=0x000000FFu ||
       NT_CALL(SetPixel,(mem,7,7,0x00FF0000u))!=0x00FF0000u ||
       NT_CALL(GetPixel,(mem,0,0))!=0x000000FFu ||
       NT_CALL(GetPixel,(mem,7,7))!=0x00FF0000u)
        return (int)0xBADF8109u;
    if(!NT_CALL(StretchBlt,(screen,244,100,32,32,mem,0,0,8,8,0x00CC0020u)))
        return (int)0xBADF810Au;
    if(NT_CALL(StretchBlt,(screen,244,100,32,32,mem,0,0,8,8,0xFFFFFFFFu))!=0)
        return (int)0xBADF810Bu;
    if(NT_CALL(SelectObject,(mem,old))!=bitmap ||
       !NT_CALL(DeleteObject,(bitmap)) ||
       !NT_CALL(DeleteObject,(white)) ||
       !NT_CALL(DeleteDC,(mem)) ||
       !NT_CALL(ReleaseDC,(0,screen)))
        return (int)0xBADF810Cu;
    if(!NT_CALL(NtosGwesClose,(a)) || !NT_CALL(NtosGwesClose,(b)) ||
       NT_CALL(NtosGwesClose,(a)) ||
       !NT_CALL(NtosGwesReset,()))
        return (int)0xBADF810Du;
    U c=NT_CALL(NtosGwesCreate,());
    if(!c || c==a || c==b || !NT_CALL(NtosGwesClose,(c)))
        return (int)0xBADF810Eu;
    return (int)0x47574531u;
}
