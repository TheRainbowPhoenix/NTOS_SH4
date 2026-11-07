/* GUIWIN.EXE - Win98-inspired settings window.
 * WinCE WNDCLASS/WM_PAINT/WM_KEYDOWN/WM_LBUTTONDOWN; no FPU. */
#include "wince_ui.h"
static const WCHAR16 cls[]={'G','U','I','W','I','N','9','8',0};
static const WCHAR16 title[]={'N','T','O','S',' ','D','e','s','k','t','o','p',0};
static const DWORD accent[4]={RGB(0,116,204),RGB(0,128,110),RGB(133,65,166),RGB(176,83,31)};
static int page=0,choose=0,check=1;
static void frame(void *dc,int l,int t,int r,int b)
{
    ui_fill(dc,l,t,r,b,RGB(105,105,105));
    ui_fill(dc,l+1,t+1,r-1,b-1,RGB(255,255,255));
    ui_fill(dc,l+3,t+3,r-3,b-3,RGB(192,192,192));
}
static void draw(void *dc)
{
    ui_fill(dc,0,0,320,528,RGB(192,192,192));
    ui_fill(dc,4,5,316,40,RGB(10,48,124));
    ui_fill(dc,5,6,10,39,RGB(18,132,181));
    ui_text(dc,18,8,"NTOS CONTROL",RGB(255,255,255),1);
    /* Raised Win98 caption-close control with an unmistakable hit target. */
    ui_fill(dc,278,10,309,34,RGB(255,255,255));
    ui_fill(dc,280,12,307,32,RGB(192,192,192));
    ui_fill(dc,306,12,309,35,RGB(79,79,79));
    for (int p=0;p<9;++p) {
        ui_fill(dc,289+p,17+p,291+p,19+p,RGB(0,0,0));
        ui_fill(dc,297-p,17+p,299-p,19+p,RGB(0,0,0));
    }
    ui_text(dc,11,49,"File",RGB(0,0,0),0);
    ui_text(dc,57,49,"View",RGB(0,0,0),0);
    ui_text(dc,106,49,"Help",RGB(0,0,0),0);
    ui_fill(dc,8,70,312,72,RGB(255,255,255));
    for (int i=0;i<3;++i) {
        int x=14+i*100;
        frame(dc,x,83,x+96,111);
        if (i==page) ui_fill(dc,x+3,85,x+93,109,RGB(236,236,236));
        ui_text(dc,x+8,92,i==0?"Display":i==1?"Colors":"About",RGB(0,0,0),0);
    }
    frame(dc,10,113,310,370);
    ui_fill(dc,14,117,306,366,RGB(240,240,240));
    if (page==0) {
        ui_text(dc,28,129,"Appearance preview",RGB(0,0,0),0);
        ui_fill(dc,28,151,292,276,RGB(255,255,255));
        ui_fill(dc,34,158,286,185,accent[choose]);
        ui_text(dc,45,165,"Sample window",RGB(255,255,255),0);
        ui_fill(dc,46,198,274,251,RGB(224,224,224));
        ui_text(dc,56,209,"Classic desktop theme",RGB(0,0,0),0);
        ui_classic_button(dc,142,230,250,249,"OK");
        ui_text(dc,30,288,"Enable decorations",RGB(0,0,0),0);
        ui_fill(dc,30,311,47,328,RGB(255,255,255));
        if (check) ui_text(dc,33,313,"X",accent[choose],0);
        ui_text(dc,59,315,check?"Enabled":"Disabled",RGB(60,60,60),0);
    } else if (page==1) {
        ui_text(dc,27,133,"Choose an accent color",RGB(0,0,0),0);
        for (int i=0;i<4;++i) {
            int x=27+(i%2)*137,y=168+(i/2)*70;
            ui_fill(dc,x,y,x+120,y+55,accent[i]);
            if (i==choose) { ui_fill(dc,x,y+48,x+120,y+55,RGB(255,255,255)); }
        }
        ui_text(dc,28,331,"Touch a swatch to apply",RGB(60,60,60),0);
    } else {
        ui_text(dc,28,142,"NTOS Windows CE layer",RGB(0,0,0),0);
        ui_text(dc,28,170,"SH4 big-endian / UTLB",RGB(35,35,35),0);
        ui_text(dc,28,194,"WinCE GDI compatibility",RGB(35,35,35),0);
        ui_text(dc,28,218,"Bitmap font / RGB565",RGB(35,35,35),0);
        ui_text(dc,28,250,"No FPU required",RGB(35,35,35),0);
    }
    ui_classic_button(dc,14,388,150,434,"< PREVIOUS");
    ui_classic_button(dc,170,388,307,434,"NEXT >");
    ui_fill(dc,0,473,320,475,RGB(255,255,255));
    ui_fill(dc,0,476,320,527,RGB(215,215,215));
    ui_text(dc,15,490,"Ready - WinCE message loop",RGB(55,55,55),0);
    ui_text(dc,15,509,"LEFT/RIGHT or touch to navigate",RGB(55,55,55),0);
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp)
{
    if (msg==WM_PAINT) {
        PAINTSTRUCT ps={0};
        void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if (dc) { draw(dc); NT_CALL(EndPaint,(hwnd,&ps)); }
        return 0;
    }
    if (msg==WM_KEYDOWN) {
        if (wp==VK_LEFT) page=(page+2)%3;
        else if (wp==VK_RIGHT || wp==VK_RETURN) page=(page+1)%3;
        else if (wp=='0') check=!check;
        else return 0;
    } else if (msg==WM_LBUTTONDOWN || msg==0x0200u) {
        int x=(short)(lp&65535u),y=(short)(lp>>16);
        /* The stylus should be able to hit the title close target even
         * if calibrated several pixels off. Do not depend on the nested
         * DestroyWindow -> WM_DESTROY path from inside input dispatch. */
        if (x>=228 && x<320 && y>=0 && y<58) {
            NT_CALL(PostQuitMessage,(0));
            return 0;
        }
        if (y>=83 && y<111 && x>=14 && x<310) page=(x-14)/100;
        else if (y>=388 && y<434) {
            if (x<160) page=(page+2)%3;
            else page=(page+1)%3;
        } else if (page==0 && y>=296 && y<=341) check=!check;
        else if (page==1 && y>=168 && y<293 && x>=27 && x<285) {
            int col=(x>=164),row=(y>=238);
            choose=row*2+col;
        } else return 0;
    } else return 0;
    NT_CALL(InvalidateRect,(hwnd,0,1));
    return 0;
}
int main(void) { ui_init(); return ui_run(cls,title,wndproc); }
