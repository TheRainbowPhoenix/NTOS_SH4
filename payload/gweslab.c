/* GWESLAB.EXE: white-theme, no-AA WinCE windowing + TouchKB demo.
 * Foreground process-local logical panes. A normal WinCE parent WNDPROC
 * hosts one real COREDLL Edit child; SipShowIM and keyboard WM_CHAR work
 * with that edit. Split panes are NOT separate kernel user processes.
 * Tap [=] tile, [<>] swap and [X] close. Tiling cycles FULL -> top/bottom
 * -> left/right -> FULL; tap the edit field to type. CLEAR exits.
 * Expected return 0x47574C31 (GWL1). No new kernel privileges/MMIO.
 */
#include <stdint.h>
#include "wince_ui.h"
typedef uint32_t U;
NT_IMPORT(void *,GetDC,(void *))
NT_IMPORT(int,ReleaseDC,(void *,void *))
NT_IMPORT(int,SipShowIM,(U))
NT_IMPORT(int,GetWindowTextW,(void *,WCHAR16 *,int))
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
NT_DEFINE(GetDC)
NT_DEFINE(ReleaseDC)
NT_DEFINE(SipShowIM)
NT_DEFINE(GetWindowTextW)
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
static U window_id[2];
static void *edit;
static const WCHAR16 cls[]={'N','T','W','M',0};
static const WCHAR16 title[]={'N','T','O','S',' ','G','W','E','S',0};
static const WCHAR16 editname[]={'E','d','i','t',0};
static const DWORD ink=RGB(20,30,48);
static void draw_panel(void *dc,U id,int n) {
    RECT a;
    if(!id || !NT_CALL(NtosGwesGetRect,(id,&a)))return;
    DWORD gray=n?RGB(248,250,252):RGB(255,255,255);
    ui_fill(dc,a.left,a.top,a.right,a.bottom,gray);
    ui_fill(dc,a.left,a.top,a.right,a.top+24,RGB(238,244,251));
    ui_fill(dc,a.left,a.top+23,a.right,a.top+24,RGB(185,194,206));
    ui_text(dc,a.left+5,a.top+4,n?"Gallery":"Notes",ink,0);
    ui_text(dc,a.right-68,a.top+4,"[]",RGB(0,88,160),0);
    ui_text(dc,a.right-45,a.top+4,"<>",RGB(0,88,160),0);
    ui_text(dc,a.right-18,a.top+4,"X",RGB(160,35,40),0);
    int top=a.top+32;
    if(n) {
        ui_text(dc,a.left+10,top,"PIXEL-ART",ink,0);
        const DWORD palette[4]={
            RGB(21,93,151),RGB(134,176,214),
            RGB(219,225,232),RGB(251,255,255)
        };
        for(int k=0;k<4;++k) {
            int x=a.left+10+(k%2)*50;
            int y=top+24+(k/2)*45;
            if(x+40<a.right && y+34<a.bottom)
                ui_fill(dc,x,y,x+40,y+34,palette[k]);
        }
    } else {
        ui_text(dc,a.left+10,top,"NTOS / COREDLL",RGB(10,82,144),0);
        if(NT_CALL(NtosGwesGetMode,())==0u) {
            ui_text(dc,12,114,"Touch the field and type",ink,0);
            ui_text(dc,12,137,"F2: keyboard  CLEAR: exit",ink,0);
        } else {
            ui_text(dc,a.left+10,top+22,"Tap [ ] to resize",ink,0);
        }
    }
    if(a.bottom-1>a.top+24)
        ui_fill(dc,a.left,a.bottom-1,a.right,a.bottom,
                RGB(202,211,223));
}
static void repaint(void *hwnd) {
    void *dc=NT_CALL(GetDC,(hwnd));
    if(!dc)return;
    ui_fill(dc,0,0,320,528,RGB(255,255,255));
    draw_panel(dc,window_id[0],0);
    draw_panel(dc,window_id[1],1);
    U mode=NT_CALL(NtosGwesGetMode,());
    if(edit) {
        NT_CALL(ShowWindow,(edit,mode==0u?1:0));
        if(mode==0u)NT_CALL(UpdateWindow,(edit));
    }
    NT_CALL(ReleaseDC,(hwnd,dc));
}
static void changed(void *hwnd) {
    NT_CALL(InvalidateRect,(hwnd,0,0));
    NT_CALL(UpdateWindow,(hwnd));
}
static int wndproc(void *hwnd,DWORD msg,DWORD w,DWORD lp) {
    (void)w;
    if(msg==0x0001u) { /* WM_CREATE */
        edit=NT_CALL(CreateWindowExW,(0u,editname,0,0u,
                    12,69,296,32,hwnd,0,0,0));
        if(edit)NT_CALL(ShowWindow,(edit,1));
        return 0;
    }
    if(msg==WM_PAINT) {
        repaint(hwnd);
        return 0;
    }
    if(msg==0x0201u) { /* WM_LBUTTONDOWN */
        int x=(short)(lp&65535u),y=(short)((lp>>16)&65535u);
        U action=0u,id=NT_CALL(NtosGwesHit,(x,y,&action));
        if(action==2u) {
            U mode=NT_CALL(NtosGwesGetMode,());
            if(NT_CALL(NtosGwesSetMode,((mode+1u)%3u)))
                changed(hwnd);
        } else if(action==3u) {
            if(NT_CALL(NtosGwesSwap,()))changed(hwnd);
        } else if(action==4u) {
            if(NT_CALL(NtosGwesClose,(id))) {
                if(window_id[0]==id)window_id[0]=0u;
                if(window_id[1]==id)window_id[1]=0u;
                changed(hwnd);
            }
        } else if(id && action==5u) {
            NT_CALL(NtosGwesFocus,(id));
        }
        return 0;
    }
    if(msg==WM_KEYDOWN) {
        if(w==VK_UP && NT_CALL(NtosGwesSetMode,(1u)))
            changed(hwnd);
        else if(w==VK_DOWN && NT_CALL(NtosGwesSetMode,(2u)))
            changed(hwnd);
        else if(w==VK_LEFT || w==VK_RIGHT) {
            if(NT_CALL(NtosGwesSwap,()))changed(hwnd);
        } else if(w==0x71u)changed(hwnd); /* hardware SIP key */
        return 0;
    }
    if(msg==0x0111u || msg==0x001Au) { /* edit EN_CHANGE / SIP changed */
        changed(hwnd);return 0;
    }
    if(msg==0x0002u) {
        if(edit){NT_CALL(DestroyWindow,(edit));edit=0;}
        return 0;
    }
    return 0;
}
int main(const void *const *api,char **argv,char **envp) {
    (void)api;(void)argv;(void)envp;
    ui_init();
    if(!NT_CALL(NtosGwesReset,()))
        return (int)0xBADF8201u;
    window_id[0]=NT_CALL(NtosGwesCreate,());
    window_id[1]=NT_CALL(NtosGwesCreate,());
    if(!window_id[0] || !window_id[1])
        return (int)0xBADF8202u;
    /* App-created top-level HWND drives the usual WinCE message loop. */
    int err=ui_run(cls,title,wndproc);
    NT_CALL(SipShowIM,(0u));
    if(window_id[0])NT_CALL(NtosGwesClose,(window_id[0]));
    if(window_id[1])NT_CALL(NtosGwesClose,(window_id[1]));
    NT_CALL(NtosGwesReset,());
    return err ? (int)(0xBADF8200u+(U)err) : (int)0x47574C31u;
}
