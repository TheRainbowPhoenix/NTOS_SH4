/* METROWIN.EXE - Windows Phone / Windows 8.1-inspired settings UI.
 * Built entirely from WinCE COREDLL GDI and message-loop functions.
 * Flat RGB565 colors, bitmap font, integer coordinates, no FPU.
 * Toggle controls are local demo state; no hardware settings are changed. */
#include "wince_ui.h"
static const WCHAR16 cls[]={'M','E','T','R','O','W','I','N',0};
static const WCHAR16 title[]={'N','T','O','S',' ','S','e','t','t','i','n','g','s',0};
static int page=0,accent=0;
static int flags[3]={1,0,1};
static const DWORD accents[3]={RGB(0,120,215),RGB(0,169,157),RGB(134,67,179)};
static DWORD ink(void) { return accents[accent]; }
static void toggle(void *dc,int x,int y,int on)
{
    ui_fill(dc,x,y,x+68,y+29,on?ink():RGB(105,105,110));
    if (on) ui_fill(dc,x+40,y+3,x+65,y+26,RGB(255,255,255));
    else ui_fill(dc,x+3,y+3,x+28,y+26,RGB(255,255,255));
}
static void row(void *dc,int y,const char *head,const char *desc,int on)
{
    ui_fill(dc,16,y,304,y+76,RGB(38,38,43));
    ui_fill(dc,16,y,20,y+76,ink());
    ui_text(dc,34,y+13,head,RGB(255,255,255),0);
    ui_text(dc,34,y+35,desc,RGB(178,181,190),0);
    toggle(dc,218,y+17,on);
}
static void draw(void *dc)
{
    const DWORD white=RGB(245,245,248),muted=RGB(157,160,171);
    ui_fill(dc,0,0,320,528,RGB(19,19,22));
    ui_fill(dc,0,0,320,37,RGB(10,10,13));
    ui_text(dc,14,13,"NTOS  /  WINDOWS CE",muted,0);
    ui_text(dc,286,13,"[+]",ink(),0);
    ui_fill(dc,15,49,20,93,ink());
    ui_text(dc,31,55,"settings",white,1);
    ui_text(dc,31,81,"PERSONALIZATION  /  DEMO",muted,0);
    ui_fill(dc,16,112,304,148,RGB(39,39,44));
    ui_text(dc,31,125,"Search settings",RGB(132,135,144),0);
    for(int i=0;i<3;++i) {
        int x=16+i*101;
        if (page==i) ui_fill(dc,x,180,x+95,183,ink());
        ui_text(dc,x+4,162,i==0?"SYSTEM":i==1?"PERSONAL":"ABOUT",
                i==page?white:muted,0);
    }
    if(page==0) {
        ui_text(dc,17,207,"Display and input",white,1);
        row(dc,240,"Dark mode","High contrast colors",flags[0]);
        row(dc,328,"Motion","Reduce UI effects",flags[1]);
        ui_text(dc,24,421,"Touch / keypad enabled",muted,0);
    } else if(page==1) {
        ui_text(dc,17,207,"Look and feel",white,1);
        ui_fill(dc,16,243,304,345,RGB(38,38,43));
        ui_text(dc,28,258,"Accent color",white,0);
        for(int i=0;i<3;++i) {
            int x=28+i*92;
            ui_fill(dc,x,283,x+78,325,accents[i]);
            if(i==accent) ui_fill(dc,x,325,x+78,330,white);
        }
        row(dc,358,"Touch hints","Show helper text",flags[2]);
    } else {
        ui_text(dc,17,211,"About this system",white,1);
        ui_fill(dc,16,248,304,425,RGB(38,38,43));
        ui_fill(dc,29,263,35,314,ink());
        ui_text(dc,49,267,"NTOS 0.2",white,1);
        ui_text(dc,29,329,"SH-4 Big Endian",white,0);
        ui_text(dc,29,352,"WinCE COREDLL facade",muted,0);
        ui_text(dc,29,376,"GDI + WNDPROC + input",muted,0);
        ui_text(dc,29,398,"Integer math only",muted,0);
    }
    ui_fill(dc,0,474,320,527,RGB(12,12,15));
    ui_fill(dc,page*106,473,page*106+106,476,ink());
    ui_text(dc,17,488,"<  LEFT / RIGHT: categories",white,0);
    ui_text(dc,17,508,"DEMO SETTINGS - NOT PERSISTED",muted,0);
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp)
{
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps={0};
        void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if(dc) { draw(dc); NT_CALL(EndPaint,(hwnd,&ps)); }
        return 0;
    }
    if(msg==WM_KEYDOWN) {
        if(wp==VK_LEFT) page=(page+2)%3;
        else if(wp==VK_RIGHT) page=(page+1)%3;
        else if(wp==VK_RETURN && page==0) flags[0]=!flags[0];
        else if(wp=='0' && page==0) flags[1]=!flags[1];
        else if(wp==VK_UP && page==1) accent=(accent+2)%3;
        else if(wp==VK_DOWN && page==1) accent=(accent+1)%3;
        else return 0;
    } else if(msg==WM_LBUTTONDOWN) {
        int x=(short)(lp&65535u),y=(short)(lp>>16);
        if(y>=154 && y<188 && x>=16 && x<309) page=(x-16)/101;
        else if(page==0 && y>=240 && y<316) flags[0]=!flags[0];
        else if(page==0 && y>=328 && y<406) flags[1]=!flags[1];
        else if(page==1 && y>=283 && y<341 && x>=28 && x<299)
            accent=(x-28)/92;
        else if(page==1 && y>=358 && y<434) flags[2]=!flags[2];
        else return 0;
    } else return 0;
    NT_CALL(InvalidateRect,(hwnd,0,1));
    return 0;
}
int main(void) { ui_init(); return ui_run(cls,title,wndproc); }
