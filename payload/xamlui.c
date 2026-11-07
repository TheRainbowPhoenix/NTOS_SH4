/* XAMLUI.DLL - optional WinCE UI helper module for NTOS SH4BE.
 * Real second PE module. Imports GDI primitives from COREDLL.DLL and
 * exposes small flat-style drawing helpers to METROWIN.EXE.
 * No floating-point, CRT, private NTOS API, or XAML parser. */
#include "ntimport.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef struct { int left,top,right,bottom; } RECT;

NT_IMPORT(void *,CreateSolidBrush,(u32))
NT_IMPORT(int,FillRect,(void *,const RECT *,void *))
NT_IMPORT(int,DeleteObject,(void *))
NT_IMPORT(int,TextOutW,(void *,int,int,const u16 *,int))
NT_IMPORT(u32,SetTextColor,(void *,u32))
NT_IMPORT(int,SetBkMode,(void *,int))

NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(TextOutW)
NT_DEFINE(SetTextColor)
NT_DEFINE(SetBkMode)

void xamlui_tile(void *dc,int l,int t,int r,int b,u32 color)
{
    RECT rect={l,t,r,b};
    if (!dc || l>=r || t>=b) return;
    void *brush=NT_CALL(CreateSolidBrush,(color));
    if (!brush) return;
    NT_CALL(FillRect,(dc,&rect,brush));
    NT_CALL(DeleteObject,(brush));
}
void xamlui_label(void *dc,int x,int y,const u16 *label,u32 color)
{
    int len=0;
    if (!dc || !label) return;
    while (len<46 && label[len]) ++len;
    NT_CALL(SetTextColor,(dc,color));
    NT_CALL(SetBkMode,(dc,1));
    NT_CALL(TextOutW,(dc,x,y,label,len));
}
void xamlui_switch(void *dc,int x,int y,int on,u32 accent)
{
    xamlui_tile(dc,x,y,x+68,y+30,on?accent:0x00696969u);
    xamlui_tile(dc,x+(on?39:3),y+3,x+(on?65:29),y+27,0x00FFFFFFu);
}
int main(void) { return 0; } /* PE entry required by current packer */
