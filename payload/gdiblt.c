/* GDIBLIT.EXE: native WinCE COREDLL imports on real SH4BE.
 * 16x16 offscreen bitmap with per-pixel COLORS/FillRect/BitBlt.
 * Checks stale GDI handle, selected-object lifetime, unsupported ROP.
 * Physically blits a tiny 16x16 patch at x=280 y=80 then clears
 * it to white, no host USB, no MMIO and no special VBR instructions.
 * Expected return 0x47444931 ("GDI1"); BADF71xx identifies stage.
 */
#include <stdint.h>
#include "ntimport.h"
typedef uint32_t u32;
typedef struct {int left,top,right,bottom;} RECT;
NT_IMPORT(void *,GetDC,(void *))
NT_IMPORT(int,ReleaseDC,(void *,void *))
NT_IMPORT(void *,CreateCompatibleDC,(void *))
NT_IMPORT(int,DeleteDC,(void *))
NT_IMPORT(void *,CreateCompatibleBitmap,(void *,int,int))
NT_IMPORT(void *,SelectObject,(void *,void *))
NT_IMPORT(int,DeleteObject,(void *))
NT_IMPORT(u32,SetPixel,(void *,int,int,u32))
NT_IMPORT(u32,GetPixel,(void *,int,int))
NT_IMPORT(void *,CreateSolidBrush,(u32))
NT_IMPORT(int,FillRect,(void *,const RECT *,void *))
NT_IMPORT(int,BitBlt,(void *,int,int,int,int,void *,int,int,u32))
NT_DEFINE(GetDC)
NT_DEFINE(ReleaseDC)
NT_DEFINE(CreateCompatibleDC)
NT_DEFINE(DeleteDC)
NT_DEFINE(CreateCompatibleBitmap)
NT_DEFINE(SelectObject)
NT_DEFINE(DeleteObject)
NT_DEFINE(SetPixel)
NT_DEFINE(GetPixel)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(BitBlt)
int main(const void *const *api,char **argv,char **envp) {
    (void)api;(void)argv;(void)envp;
    void *screen=NT_CALL(GetDC,(0));
    if(!screen)return (int)0xBADF7101u;
    void *mem=NT_CALL(CreateCompatibleDC,(screen));
    if(!mem)return (int)0xBADF7102u;
    void *bmp=NT_CALL(CreateCompatibleBitmap,(screen,16,16));
    if(!bmp)return (int)0xBADF7103u;
    void *stock=NT_CALL(SelectObject,(mem,bmp));
    if(!stock || NT_CALL(DeleteObject,(bmp))!=0)
        return (int)0xBADF7104u;
    RECT full={0,0,16,16};
    void *brush=NT_CALL(CreateSolidBrush,(0x00FFFFFFu));
    if(!brush || !NT_CALL(FillRect,(mem,&full,brush)))
        return (int)0xBADF7105u;
    if(NT_CALL(SetPixel,(mem,0,0,0x000000FFu))!=0x000000FFu ||
       NT_CALL(SetPixel,(mem,1,0,0x0000FF00u))!=0x0000FF00u ||
       NT_CALL(SetPixel,(mem,2,0,0x00FF0000u))!=0x00FF0000u ||
       NT_CALL(GetPixel,(mem,0,0))!=0x000000FFu ||
       NT_CALL(GetPixel,(mem,1,0))!=0x0000FF00u ||
       NT_CALL(GetPixel,(mem,2,0))!=0x00FF0000u)
        return (int)0xBADF7106u;
    if(NT_CALL(BitBlt,(screen,280,80,16,16,mem,0,0,0xFFFFFFFFu))!=0)
        return (int)0xBADF7107u;
    if(!NT_CALL(BitBlt,(screen,280,80,16,16,mem,0,0,0x00CC0020u)))
        return (int)0xBADF7108u;
    /* Restore the tiny patch to white with a second in-memory fill and
       blit, proving that the same DC/bitmap can be reused. */
    if(!NT_CALL(FillRect,(mem,&full,brush)) ||
       !NT_CALL(BitBlt,(screen,280,80,16,16,mem,0,0,0x00CC0020u)))
        return (int)0xBADF7109u;
    if(NT_CALL(SelectObject,(mem,stock))!=bmp ||
       !NT_CALL(DeleteObject,(bmp)) ||
       !NT_CALL(DeleteDC,(mem)) ||
       !NT_CALL(DeleteObject,(brush)) ||
       !NT_CALL(ReleaseDC,(0,screen)))
        return (int)0xBADF710Au;
    if(NT_CALL(GetPixel,(mem,0,0))!=0xFFFFFFFFu ||
       NT_CALL(DeleteObject,(bmp))!=0)
        return (int)0xBADF710Bu;
    return (int)0x47444931u;
}
