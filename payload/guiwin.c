/* GUIWIN.EXE: WinCE window-procedure and WM_PAINT demo.
 * One window, click its two buttons or use LEFT/RIGHT to change palette.
 * CLEAR exits through the WinCE-style GetMessageW loop.
 * All imported API names are WinCE APIs, no coredll_* private imports. */
#include "ntimport.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x,y; } POINT;
typedef struct {
    void *hwnd; u32 message,wParam,lParam,time; POINT pt;
} MSG;
typedef struct {
    void *hdc; int erase; RECT rcPaint; int restore,incUpdate;
    unsigned char reserved[32];
} PAINTSTRUCT;
typedef int (*WNDPROC)(void *,u32,u32,u32);
typedef struct {
    u32 style; WNDPROC wndproc; int class_extra,window_extra;
    void *instance,*icon,*cursor,*background;
    const u16 *menu_name,*class_name;
} WNDCLASSW;

NT_IMPORT(u32, RegisterClassW, (const WNDCLASSW *))
NT_IMPORT(void *, CreateWindowExW, (u32,const u16 *,const u16 *,u32,
                                   int,int,int,int,void *,void *,void *,void *))
NT_IMPORT(int, ShowWindow, (void *,int))
NT_IMPORT(int, UpdateWindow, (void *))
NT_IMPORT(int, InvalidateRect, (void *,const RECT *,int))
NT_IMPORT(int, GetMessageW, (MSG *,void *,u32,u32))
NT_IMPORT(int, TranslateMessage, (const MSG *))
NT_IMPORT(int, DispatchMessageW, (const MSG *))
NT_IMPORT(int, GetClientRect, (void *,RECT *))
NT_IMPORT(void *, BeginPaint, (void *,PAINTSTRUCT *))
NT_IMPORT(int, EndPaint, (void *,const PAINTSTRUCT *))
NT_IMPORT(void *, CreateSolidBrush, (u32))
NT_IMPORT(int, FillRect, (void *,const RECT *,void *))
NT_IMPORT(int, DeleteObject, (void *))
NT_IMPORT(int, TextOutW, (void *,int,int,const u16 *,int))
NT_IMPORT(int, DestroyWindow, (void *))

NT_DEFINE(RegisterClassW)
NT_DEFINE(CreateWindowExW)
NT_DEFINE(ShowWindow)
NT_DEFINE(UpdateWindow)
NT_DEFINE(InvalidateRect)
NT_DEFINE(GetMessageW)
NT_DEFINE(TranslateMessage)
NT_DEFINE(DispatchMessageW)
NT_DEFINE(GetClientRect)
NT_DEFINE(BeginPaint)
NT_DEFINE(EndPaint)
NT_DEFINE(CreateSolidBrush)
NT_DEFINE(FillRect)
NT_DEFINE(DeleteObject)
NT_DEFINE(TextOutW)
NT_DEFINE(DestroyWindow)

static const u16 cls[]={'N','T','O','S','G','U','I',0};
static const u16 title[]={'N','T','O','S',' ','W','i','n','C','E',0};
static const u16 intro[]={'W','i','n','C','E',' ','W','N','D','P','R','O','C',0};
static const u16 left_text[]={'P','R','E','V',0};
static const u16 right_text[]={'N','E','X','T',0};
static const u16 hint[]={'T','a','p',' ','b','u','t','t','o','n','s',';',
 ' ','L','E','F','T','/','R','I','G','H','T',' ','k','e','y','s',0};
static const u32 palette[] = {
    0x00203060u,0x00206030u,0x00603020u,0x00402060u
};
static u32 selected;

static void fill(void *dc,int x1,int y1,int x2,int y2,u32 rgb)
{
    RECT rect={x1,y1,x2,y2};
    void *brush=NT_CALL(CreateSolidBrush,(rgb));
    if (brush) {
        NT_CALL(FillRect,(dc,&rect,brush));
        NT_CALL(DeleteObject,(brush));
    }
}
static int window_proc(void *hwnd,u32 msg,u32 wparam,u32 lparam)
{
    (void)lparam;
    if (msg==0x000Fu) { /* WM_PAINT */
        RECT client={0};
        PAINTSTRUCT ps={0};
        void *dc;
        NT_CALL(GetClientRect,(hwnd,&client));
        dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if (!dc) return 0;
        fill(dc,0,0,client.right,client.bottom,0x00161622u);
        fill(dc,20,30,300,260,palette[selected]);
        fill(dc,14,305,150,362,0x00505050u);
        fill(dc,168,305,306,362,0x00505050u);
        NT_CALL(TextOutW,(dc,16,13,intro,12));
        NT_CALL(TextOutW,(dc,58,326,left_text,4));
        NT_CALL(TextOutW,(dc,214,326,right_text,4));
        NT_CALL(TextOutW,(dc,10,400,hint,27));
        NT_CALL(EndPaint,(hwnd,&ps));
        return 0;
    }
    if (msg==0x0201u) { /* WM_LBUTTONDOWN */
        int x=(short)(lparam & 0xFFFFu);
        int y=(short)((lparam>>16)&0xFFFFu);
        if (y>=305 && y<362) {
            if (x>=14 && x<150) selected=(selected+3u)%4u;
            if (x>=168 && x<306) selected=(selected+1u)%4u;
            NT_CALL(InvalidateRect,(hwnd,0,1));
        }
        return 0;
    }
    if (msg==0x0100u) { /* WM_KEYDOWN */
        if (wparam==0x25u) selected=(selected+3u)%4u;
        else if (wparam==0x27u) selected=(selected+1u)%4u;
        else return 0;
        NT_CALL(InvalidateRect,(hwnd,0,1));
        return 0;
    }
    return 0;
}
int main(void)
{
    WNDCLASSW wc={0};
    void *window;
    MSG msg={0};
    wc.wndproc=window_proc;
    wc.class_name=cls;
    if (!NT_CALL(RegisterClassW,(&wc))) return 1;
    window=NT_CALL(CreateWindowExW,(0,cls,title,0,0,0,320,528,0,0,0,0));
    if (!window) return 2;
    NT_CALL(ShowWindow,(window,1));
    NT_CALL(UpdateWindow,(window));
    for (;;) {
        int result=NT_CALL(GetMessageW,(&msg,window,0,0));
        if (result<=0) break;
        NT_CALL(TranslateMessage,(&msg));
        NT_CALL(DispatchMessageW,(&msg));
    }
    NT_CALL(DestroyWindow,(window));
    return 0;
}
