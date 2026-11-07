/* Minimal WinCE SH4-BE GUI SDK for NTOS. All functions are genuine WinCE
 * COREDLL export names; NTOS implements an intentionally limited subset.
 * No floats, CRT, windows.h, or vendor-specific imported APIs. */
#ifndef NTOS_WINCE_UI_H
#define NTOS_WINCE_UI_H
#include "ntimport.h"
typedef unsigned int DWORD;
typedef unsigned short WCHAR16;
typedef struct { int left,top,right,bottom; } RECT;
typedef struct { int x,y; } POINT;
typedef struct { void *hwnd; DWORD message,wParam,lParam,time; POINT pt; } MSG;
typedef struct {
    void *hdc; int erase; RECT rcPaint; int restore,incUpdate;
    unsigned char reserved[32];
} PAINTSTRUCT;
typedef int (*WNDPROC)(void *,DWORD,DWORD,DWORD);
typedef struct {
    DWORD style; WNDPROC wndproc; int class_extra,window_extra;
    void *instance,*icon,*cursor,*background;
    const WCHAR16 *menu_name,*class_name;
} WNDCLASSW;
typedef struct {
    int lfHeight,lfWidth,lfEscapement,lfOrientation,lfWeight;
    unsigned char lfItalic,lfUnderline,lfStrikeOut,lfCharSet;
    unsigned char lfOutPrecision,lfClipPrecision,lfQuality,lfPitchAndFamily;
    WCHAR16 lfFaceName[32];
} LOGFONTW;
#define WM_PAINT 0x000Fu
#define WM_KEYDOWN 0x0100u
#define WM_LBUTTONDOWN 0x0201u
#define WM_QUIT 0x0012u
#define VK_LEFT 0x25u
#define VK_UP 0x26u
#define VK_RIGHT 0x27u
#define VK_DOWN 0x28u
#define VK_RETURN 0x0Du
#define TRANSPARENT 1
#define OPAQUE 2
#define RGB(r,g,b) ((DWORD)((r)|((g)<<8)|((b)<<16)))

NT_IMPORT(DWORD,RegisterClassW,(const WNDCLASSW *))
NT_IMPORT(void *,CreateWindowExW,(DWORD,const WCHAR16 *,const WCHAR16 *,DWORD,int,int,int,int,void *,void *,void *,void *))
NT_IMPORT(int,ShowWindow,(void *,int))
NT_IMPORT(int,UpdateWindow,(void *))
NT_IMPORT(int,InvalidateRect,(void *,const RECT *,int))
NT_IMPORT(int,GetMessageW,(MSG *,void *,DWORD,DWORD))
NT_IMPORT(int,TranslateMessage,(const MSG *))
NT_IMPORT(int,DispatchMessageW,(const MSG *))
NT_IMPORT(int,DestroyWindow,(void *))
NT_IMPORT(void,PostQuitMessage,(int))
NT_IMPORT(int,GetClientRect,(void *,RECT *))
NT_IMPORT(void *,BeginPaint,(void *,PAINTSTRUCT *))
NT_IMPORT(int,EndPaint,(void *,const PAINTSTRUCT *))
NT_IMPORT(void *,CreateSolidBrush,(DWORD))
NT_IMPORT(int,FillRect,(void *,const RECT *,void *))
NT_IMPORT(int,DeleteObject,(void *))
NT_IMPORT(int,TextOutW,(void *,int,int,const WCHAR16 *,int))
NT_IMPORT(int,DrawTextW,(void *,const WCHAR16 *,int,RECT *,DWORD))
NT_IMPORT(DWORD,SetTextColor,(void *,DWORD))
NT_IMPORT(DWORD,SetBkColor,(void *,DWORD))
NT_IMPORT(int,SetBkMode,(void *,int))
NT_IMPORT(void *,CreateFontIndirectW,(const LOGFONTW *))
NT_IMPORT(void *,SelectObject,(void *,void *))

NT_DEFINE(RegisterClassW)
NT_DEFINE(CreateWindowExW)
NT_DEFINE(ShowWindow)
NT_DEFINE(UpdateWindow)
NT_DEFINE(InvalidateRect)
NT_DEFINE(GetMessageW)
NT_DEFINE(TranslateMessage)
NT_DEFINE(DispatchMessageW)
NT_DEFINE(DestroyWindow)
NT_DEFINE(PostQuitMessage)
NT_DEFINE(GetClientRect)
NT_DEFINE(BeginPaint)
NT_DEFINE(EndPaint)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(TextOutW)
NT_DEFINE(DrawTextW)
NT_DEFINE(SetTextColor)
NT_DEFINE(SetBkColor)
NT_DEFINE(SetBkMode)
NT_DEFINE(CreateFontIndirectW)
NT_DEFINE(SelectObject)

static void *ui_bigfont;
static void *ui_smallfont;
static void ui_init(void)
{
    /* Static zero-initialization avoids SH4 freestanding movmem helpers. */
    static LOGFONTW lf;
    lf.lfHeight=-24;
    ui_bigfont=NT_CALL(CreateFontIndirectW,(&lf));
    lf.lfHeight=-14;
    ui_smallfont=NT_CALL(CreateFontIndirectW,(&lf));
}
static void ui_fill(void *dc,int l,int t,int r,int b,DWORD color)
{
    RECT rect={l,t,r,b};
    if (r<=l || b<=t) return;
    void *brush=NT_CALL(CreateSolidBrush,(color));
    if (!brush) return;
    NT_CALL(FillRect,(dc,&rect,brush));
    NT_CALL(DeleteObject,(brush));
}
static void ui_text(void *dc,int x,int y,const char *s,DWORD color,int large)
{
    WCHAR16 w[48];
    int n=0;
    while (n<47 && s[n]) { w[n]=(unsigned char)s[n]; ++n; }
    w[n]=0;
    NT_CALL(SelectObject,(dc,large ? ui_bigfont : ui_smallfont));
    NT_CALL(SetTextColor,(dc,color));
    NT_CALL(SetBkMode,(dc,TRANSPARENT));
    NT_CALL(TextOutW,(dc,x,y,w,n));
}
/* UGUI 8x14 glyph advances, same widths as COREDLL raster.
 * Keep the UI helper independent of font file structures. */
static int ui_measure(const char *s)
{
    static const unsigned char advances[95]={3,6,7,8,8,8,8,6,7,8,8,8,6,8,6,8,8,8,8,7,8,7,8,8,8,8,6,6,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,6,8,8,8,8,5,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,5,8,8};
    int n=0;
    for (; *s; ++s) {
        unsigned c=(unsigned char)*s;
        n+=(c>=32 && c<127) ? advances[c-32] : 8;
    }
    return n;
}
static void ui_button(void *dc,int l,int t,int r,int b,
                      const char *label,DWORD bg,DWORD fg)
{
    int tw=ui_measure(label);
    ui_fill(dc,l,t,r,b,bg);
    ui_text(dc,l+(r-l-tw)/2,t+(b-t-14)/2,label,fg,0);
}
/* Faithful Windows 95/98 raised command button: light top/left edge,
 * dark right/bottom shadow, gray face, proportionally centered caption. */
static void ui_classic_button(void *dc,int l,int t,int r,int b,
                              const char *label)
{
    const DWORD face=RGB(192,192,192);
    ui_fill(dc,l,t,r,b,RGB(0,0,0));
    ui_fill(dc,l,t,r-1,b-1,RGB(255,255,255));
    ui_fill(dc,l+1,t+1,r-2,b-2,face);
    ui_fill(dc,r-2,t+1,r-1,b-1,RGB(128,128,128));
    ui_fill(dc,l+1,b-2,r-1,b-1,RGB(128,128,128));
    ui_fill(dc,l+2,t+2,r-3,b-3,face);
    int tw=ui_measure(label);
    ui_text(dc,l+(r-l-tw)/2,t+(b-t-16)/2,label,
            RGB(0,0,0),0);
}
static int ui_run(const WCHAR16 *class_name,const WCHAR16 *title,WNDPROC proc)
{
    WNDCLASSW cls={0};
    MSG msg={0};
    void *hwnd;
    cls.wndproc=proc;
    cls.class_name=class_name;
    if (!NT_CALL(RegisterClassW,(&cls))) return 1;
    hwnd=NT_CALL(CreateWindowExW,(0,class_name,title,0,0,0,320,528,0,0,0,0));
    if (!hwnd) return 2;
    NT_CALL(ShowWindow,(hwnd,1));
    NT_CALL(UpdateWindow,(hwnd));
    for (;;) {
        int rc=NT_CALL(GetMessageW,(&msg,hwnd,0,0));
        if (rc<=0) break;
        NT_CALL(TranslateMessage,(&msg));
        NT_CALL(DispatchMessageW,(&msg));
    }
    NT_CALL(DestroyWindow,(hwnd));
    return 0;
}
#endif
