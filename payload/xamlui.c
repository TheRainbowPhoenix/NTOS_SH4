/* XAMLUI.DLL - pen-oriented Windows 10 Mobile style component layer.
 * Independent SH4BE PE module; imports ONLY real WinCE COREDLL APIs.
 * No floating-point, CRT, hidden SDK draws, XAML parser or OS resources.
 * 320x528 ClassPad viewport, actual drawing through GDI -> NTOS.
 *
 * Exports below are XAMLUI's own API, NOT fictitious COREDLL methods.
 */
#include "ntimport.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef struct { int left,top,right,bottom; } RECT;
typedef struct {
 int lfHeight,lfWidth,lfEscapement,lfOrientation,lfWeight;
 unsigned char lfItalic,lfUnderline,lfStrikeOut,lfCharSet;
 unsigned char lfOutPrecision,lfClipPrecision,lfQuality,lfPitchAndFamily;
 u16 lfFaceName[32];
} LOGFONTW;
typedef struct {int x,y;} POINT;
#define RGB(r,g,b) ((u32)((r)|((g)<<8)|((b)<<16)))
#define BG RGB(0,0,0)
#define SURFACE RGB(27,27,27)
#define PANEL RGB(43,43,43)
#define WHITE RGB(244,244,244)
#define DIM RGB(161,161,161)
#define BLUE RGB(51,103,241)
NT_IMPORT(void *,CreateSolidBrush,(u32))
NT_IMPORT(int,FillRect,(void *,const RECT *,void *))
NT_IMPORT(int,DeleteObject,(void *))
NT_IMPORT(int,TextOutW,(void *,int,int,const u16 *,int))
NT_IMPORT(u32,SetTextColor,(void *,u32))
NT_IMPORT(int,SetBkMode,(void *,int))
NT_IMPORT(void *,CreateFontIndirectW,(const LOGFONTW *))
NT_IMPORT(void *,SelectObject,(void *,void *))
NT_IMPORT(int,MoveToEx,(void *,int,int,POINT *))
NT_IMPORT(int,LineTo,(void *,int,int))
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(TextOutW)
NT_DEFINE(SetTextColor)
NT_DEFINE(SetBkMode)
NT_DEFINE(CreateFontIndirectW)
NT_DEFINE(SelectObject)
NT_DEFINE(MoveToEx)
NT_DEFINE(LineTo)

static void *font_normal;
static void *font_large;
static void *font_title;
static void ensure_fonts(void) {
    static LOGFONTW lf;
    if(font_normal) return;
    lf.lfHeight=-14;
    font_normal=NT_CALL(CreateFontIndirectW,(&lf));
    lf.lfHeight=-22;
    font_large=NT_CALL(CreateFontIndirectW,(&lf));
    lf.lfHeight=-24;
    font_title=NT_CALL(CreateFontIndirectW,(&lf));
}
void xamlui_tile(void *dc,int l,int t,int r,int b,u32 color) {
    RECT rect={l,t,r,b};
    if(!dc || r<=l || b<=t) return;
    void *brush=NT_CALL(CreateSolidBrush,(color));
    if(!brush) return;
    NT_CALL(FillRect,(dc,&rect,brush));
    NT_CALL(DeleteObject,(brush));
}
void xamlui_text(void *dc,int x,int y,const u16 *txt,u32 clr,int large) {
    int n=0;
    if(!dc || !txt) return;
    ensure_fonts();
    while(n<80 && txt[n]) ++n;
    NT_CALL(SelectObject,(dc,large ? font_large : font_normal));
    NT_CALL(SetBkMode,(dc,1));  /* WinCE TRANSPARENT */
    NT_CALL(SetTextColor,(dc,clr));
    NT_CALL(TextOutW,(dc,x,y,txt,n));
}
void xamlui_label(void *dc,int x,int y,const u16 *txt,u32 clr) {
    xamlui_text(dc,x,y,txt,clr,0);
}
void xamlui_line(void *dc,int x0,int y0,int x1,int y1) {
    if (!dc) return;
    NT_CALL(MoveToEx,(dc,x0,y0,0));
    NT_CALL(LineTo,(dc,x1,y1));
}
void xamlui_switch(void *dc,int x,int y,int on,u32 accent) {
    xamlui_tile(dc,x,y,x+57,y+24,on?accent:RGB(89,89,89));
    xamlui_tile(dc,x+(on?34:3),y+3,x+(on?54:23),y+21,WHITE);
}
/* Draw a three-bar hamburger, back-chevron, Windows logo and search icon
 * with actual GDI primitives. No fictitious syscall exports. */
static void nav_icon(void *dc,int what,int cx,int cy) {
    if(what==0) {
        for(int i=0;i<3;++i)
            xamlui_tile(dc,cx-9,cy-8+i*6,cx+9,cy-7+i*6,WHITE);
    } else if(what==1) {
        xamlui_tile(dc,cx-9,cy,cx+9,cy+1,WHITE);
        for(int i=0;i<8;i++) {
            xamlui_tile(dc,cx-9+i,cy-i,cx-8+i,cy-i+1,WHITE);
            xamlui_tile(dc,cx-9+i,cy+i,cx-8+i,cy+i+1,WHITE);
        }
    } else if(what==2) {
        xamlui_tile(dc,cx-9,cy-8,cx-1,cy,WHITE);
        xamlui_tile(dc,cx+1,cy-8,cx+9,cy,WHITE);
        xamlui_tile(dc,cx-9,cy+2,cx-1,cy+10,WHITE);
        xamlui_tile(dc,cx+1,cy+2,cx+9,cy+10,WHITE);
    } else {
        /* Thin square-outline magnifier with diagonal handle */
        for(int y=-7;y<=4;++y) for(int x=-7;x<=4;++x) {
            int rr=x*x+y*y;
            if(rr>=40 && rr<=66)
                xamlui_tile(dc,cx+x,cy+y,cx+x+1,cy+y+1,WHITE);
        }
        for(int i=0;i<7;++i)
            xamlui_tile(dc,cx+3+i,cy+4+i,cx+4+i,cy+5+i,WHITE);
    }
}
void xamlui_shell(void *dc,const u16 *title,int pivot) {
    xamlui_tile(dc,0,0,320,528,BG);
    xamlui_tile(dc,0,0,320,50,SURFACE);
    nav_icon(dc,0,21,25);
    xamlui_text(dc,51,17,title,WHITE,0);
    nav_icon(dc,3,294,25);
    /* Navigation bar is a genuine bottom application affordance. */
    xamlui_tile(dc,0,488,320,528,BG);
    nav_icon(dc,1,68,509);
    nav_icon(dc,2,160,509);
    nav_icon(dc,3,255,509);
    (void)pivot;
}
void xamlui_pivot(void *dc,int selected,
                  const u16 *a,const u16 *b,const u16 *c) {
    const u16 *labels[3]={a,b,c};
    xamlui_tile(dc,0,50,320,88,BG);
    for(int i=0;i<3;++i) {
        if(labels[i]) xamlui_text(dc,14+i*102,59,labels[i],
                               selected==i?WHITE:DIM,0);
        if(i==selected) xamlui_tile(dc,14+i*102,85,100+i*102,88,BLUE);
    }
    xamlui_tile(dc,12,88,308,89,RGB(45,45,45));
}
void xamlui_row(void *dc,int top,const u16 *primary,
                const u16 *secondary,int focused) {
    if(focused) xamlui_tile(dc,7,top,313,top+66,SURFACE);
    xamlui_text(dc,15,top+9,primary,WHITE,0);
    if(secondary) xamlui_text(dc,15,top+34,secondary,DIM,0);
}
void xamlui_checkbox(void *dc,int x,int y,const u16 *label,int checked) {
    xamlui_tile(dc,x,y,x+15,y+15,checked?BLUE:WHITE);
    xamlui_tile(dc,x+2,y+2,x+13,y+13,checked?BLUE:BG);
    if(checked) {
        xamlui_tile(dc,x+4,y+8,x+7,y+10,WHITE);
        xamlui_tile(dc,x+7,y+6,x+10,y+8,WHITE);
        xamlui_tile(dc,x+10,y+4,x+12,y+6,WHITE);
    }
    xamlui_text(dc,x+25,y-1,label,WHITE,0);
}
void xamlui_progress(void *dc,int x,int y,int width,int percent) {
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    xamlui_tile(dc,x,y,x+width,y+11,RGB(54,54,54));
    xamlui_tile(dc,x,y,x+(width*percent)/100,y+11,BLUE);
}
void xamlui_actionbar(void *dc,
                      const u16 *left,const u16 *right) {
    xamlui_tile(dc,0,449,320,488,SURFACE);
    if(left) xamlui_text(dc,26,463,left,WHITE,0);
    if(right) xamlui_text(dc,190,463,right,WHITE,0);
}
void xamlui_dialog(void *dc,const u16 *title,const u16 *message) {
    /* Straight Windows 10 Mobile style dialog - blue stroke, no alpha. */
    xamlui_tile(dc,0,156,320,325,BLUE);
    xamlui_tile(dc,2,158,318,323,BG);
    xamlui_text(dc,18,176,title,WHITE,0);
    xamlui_text(dc,18,212,message,WHITE,0);
    xamlui_tile(dc,19,265,154,299,PANEL);
    xamlui_tile(dc,165,265,301,299,PANEL);
    static const u16 yes[]={'O','K',0};
    static const u16 no[]={'C','a','n','c','e','l',0};
    xamlui_text(dc,77,274,yes,WHITE,0);
    xamlui_text(dc,200,274,no,WHITE,0);
}
void xamlui_menu(void *dc) {
    static const u16 a[]={'S','e','t','t','i','n','g','s',0};
    static const u16 b[]={'S','t','o','r','a','g','e',0};
    static const u16 c[]={'M','u','s','i','c',0};
    static const u16 d[]={'O','f','f','l','i','n','e',' ','m','a','p','s',0};
    xamlui_tile(dc,0,50,223,448,PANEL);
    xamlui_text(dc,20,78,a,WHITE,0);
    xamlui_text(dc,20,135,b,WHITE,0);
    xamlui_text(dc,20,192,c,WHITE,0);
    xamlui_text(dc,20,249,d,WHITE,0);
}
int main(void) { return 0; }
