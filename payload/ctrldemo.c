/* CTRLDEMO.EXE: native WinCE common control and system SIP smoke UI.
 * The Edit fields accept TouchKB WM_CHAR; BUTTON/LISTBOX/COMBOBOX and
 * PROGRESS are native child HWNDs, not app-drawn approximations. */
#include "wince_ui.h"
NT_IMPORT(int, SendMessageW, (void *, DWORD, DWORD, DWORD))
NT_IMPORT(int, SetWindowTextW, (void *, const WCHAR16 *))
NT_IMPORT(int, SipShowIM, (DWORD))
NT_DEFINE(SendMessageW)
NT_DEFINE(SetWindowTextW)
NT_DEFINE(SipShowIM)
static const WCHAR16 classname[]={'N','t','C','o','n','t','r','o','l','s',0};
static const WCHAR16 title[]={'N','T','O','S',' ','C','o','n','t','r','o','l','s',0};
static const WCHAR16 editname[]={'E','D','I','T',0};
static const WCHAR16 button[]={'B','U','T','T','O','N',0};
static const WCHAR16 label[]={'S','T','A','T','I','C',0};
static const WCHAR16 barname[]={'m','s','c','t','l','s','_','p','r','o','g','r','e','s','s','3','2',0};
static const WCHAR16 listbox[]={'L','I','S','T','B','O','X',0};
static const WCHAR16 combo[]={'C','O','M','B','O','B','O','X',0};
static const WCHAR16 press[]={'A','p','p','l','y',0};
static const WCHAR16 check[]={'E','n','a','b','l','e',0};
static const WCHAR16 text1[]={'F','i','r','s','t',' ','n','a','m','e',0};
static const WCHAR16 text2[]={'L','a','s','t',' ','n','a','m','e',0};
static const WCHAR16 option1[]={'D','e','f','a','u','l','t',0};
static const WCHAR16 option2[]={'B','l','u','e',0};
static const WCHAR16 option3[]={'W','h','i','t','e',0};
static const WCHAR16 done[]={'C','o','m','m','a','n','d',' ','d','e','l','i','v','e','r','e','d',0};
static void *edit_a,*edit_b,*apply,*checkbox,*status,*progress,*list,*choose;
static unsigned clicks;
static void *child(const WCHAR16 *cl,const WCHAR16 *name,DWORD style,
                   int x,int y,int w,int h,void *parent,unsigned id)
{
    void *hwnd=NT_CALL(CreateWindowExW,
        (0,cl,name,style,x,y,w,h,parent,(void *)id,0,0));
    if(hwnd)NT_CALL(ShowWindow,(hwnd,1));
    return hwnd;
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp)
{
    (void)lp;
    if(msg==0x0001u) { /* WM_CREATE */
        status=child(label,text1,0,8,62,280,28,hwnd,10u);
        edit_a=child(editname,0,0,8,96,300,34,hwnd,11u);
        edit_b=child(editname,0,0,8,140,300,34,hwnd,12u);
        apply=child(button,press,0,8,186,140,40,hwnd,20u);
        checkbox=child(button,check,3,161,186,147,40,hwnd,21u);
        list=child(listbox,0,0,8,238,144,98,hwnd,30u);
        choose=child(combo,0,0,161,238,147,38,hwnd,31u);
        progress=child(barname,0,0,8,365,300,26,hwnd,40u);
        if(list) {
            NT_CALL(SendMessageW,(list,0x0180u,0,(DWORD)option1));
            NT_CALL(SendMessageW,(list,0x0180u,0,(DWORD)option2));
            NT_CALL(SendMessageW,(list,0x0180u,0,(DWORD)option3));
        }
        if(choose) {
            NT_CALL(SendMessageW,(choose,0x0143u,0,(DWORD)option1));
            NT_CALL(SendMessageW,(choose,0x0143u,0,(DWORD)option2));
            NT_CALL(SendMessageW,(choose,0x0143u,0,(DWORD)option3));
            NT_CALL(SendMessageW,(choose,0x014Eu,0,0));
        }
        return 0;
    }
    if(msg==0x0111u) { /* WM_COMMAND */
        unsigned id=wp&65535u;
        if(id==20u || id==21u) {
            clicks=(clicks+10u)%110u;
            if(progress)NT_CALL(SendMessageW,(progress,0x0402u,clicks,0));
            if(status)NT_CALL(SetWindowTextW,(status,done));
        }
        NT_CALL(InvalidateRect,(hwnd,0,0));
        return 0;
    }
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps={0};
        void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if(dc) {
            ui_fill(dc,0,24,320,528,RGB(255,255,255));
            ui_text(dc,10,32,"Native WinCE controls",RGB(25,51,79),0);
            ui_text(dc,10,400,"Tap an EDIT for TouchKB",RGB(74,95,114),0);
            ui_text(dc,10,425,"Apply: WM_COMMAND + progress",RGB(74,95,114),0);
            ui_text(dc,10,450,"ESC/CLEAR: return to shell",RGB(74,95,114),0);
            NT_CALL(EndPaint,(hwnd,&ps));
        }
        return 0;
    }
    if(msg==WM_KEYDOWN && wp==27u) {
        NT_CALL(SipShowIM,(0u));
        NT_CALL(PostQuitMessage,(0));
        return 0;
    }
    return 0;
}
int main(void)
{
    ui_init();
    return ui_run(classname,title,wndproc);
}
