/* METROWIN.EXE - Windows 10 Mobile-inspired demo, 320x528.
 * WinCE WNDCLASS + WNDPROC, COREDLL GDI and a separate PE XAMLUI.DLL.
 * XAMLUI renders the framework shell/controls. No floats or CRT.
 * All storage/audio/maps content is illustrative, not real user data. */
#include "wince_ui.h"
NT_IMPORT(void,xamlui_tile,(void *,int,int,int,int,DWORD))
NT_IMPORT(void,xamlui_label,(void *,int,int,const WCHAR16 *,DWORD))
NT_IMPORT(void,xamlui_switch,(void *,int,int,int,DWORD))
NT_IMPORT(void,xamlui_text,(void *,int,int,const WCHAR16 *,DWORD,int))
NT_IMPORT(void,xamlui_shell,(void *,const WCHAR16 *,int))
NT_IMPORT(void,xamlui_pivot,(void *,int,const WCHAR16 *,const WCHAR16 *,const WCHAR16 *))
NT_IMPORT(void,xamlui_row,(void *,int,const WCHAR16 *,const WCHAR16 *,int))
NT_IMPORT(void,xamlui_checkbox,(void *,int,int,const WCHAR16 *,int))
NT_IMPORT(void,xamlui_progress,(void *,int,int,int,int))
NT_IMPORT(void,xamlui_actionbar,(void *,const WCHAR16 *,const WCHAR16 *))
NT_IMPORT(void,xamlui_dialog,(void *,const WCHAR16 *,const WCHAR16 *))
NT_IMPORT(void,xamlui_menu,(void *))
NT_DEFINE(xamlui_tile)
NT_DEFINE(xamlui_label)
NT_DEFINE(xamlui_switch)
NT_DEFINE(xamlui_text)
NT_DEFINE(xamlui_shell)
NT_DEFINE(xamlui_pivot)
NT_DEFINE(xamlui_row)
NT_DEFINE(xamlui_checkbox)
NT_DEFINE(xamlui_progress)
NT_DEFINE(xamlui_actionbar)
NT_DEFINE(xamlui_dialog)
NT_DEFINE(xamlui_menu)
#define C(r,g,b) RGB(r,g,b)
#define WHITE C(240,240,240)
#define BLUE C(51,103,241)
#define DIM C(158,158,158)
#define PANEL C(31,31,31)
static const WCHAR16 cls[]={'M','E','T','R','O','W','I','N',0};
static const WCHAR16 title[]={'X','A','M','L','U','I',' ','D','e','m','o',0};
static int screen=0,tab=0,menu_open=0,dialog_open=0,checked=2;
static int percentages[5]={99,57,15,11,8};
static const char *names[]={"This Device","Clear browsing data",
                            "MY MUSIC","Offline maps","Music folder"};
static void to_wide(const char *src,WCHAR16 *dst) {
    int i=0;
    if(!src) {dst[0]=0;return;}
    for(;i<63 && src[i];++i) dst[i]=(unsigned char)src[i];
    dst[i]=0;
}
static void txt(void *dc,int x,int y,const char *label,DWORD color,int big) {
    WCHAR16 buf[64];
    to_wide(label,buf);
    NT_CALL(xamlui_text,(dc,x,y,buf,color,big));
}
static void label(void *dc,int x,int y,const char *text,DWORD color) {
    txt(dc,x,y,text,color,0);
}
static void tile(void *dc,int x,int y,int w,int h,DWORD color) {
    NT_CALL(xamlui_tile,(dc,x,y,x+w,y+h,color));
}
static void progress(void *dc,int x,int y,int w,int p) {
    NT_CALL(xamlui_progress,(dc,x,y,w,p));
}
static void checkbox(void *dc,int x,int y,const char *text,int yes) {
    WCHAR16 buf[64];to_wide(text,buf);
    NT_CALL(xamlui_checkbox,(dc,x,y,buf,yes));
}
static void xrow(void *dc,int y,const char *a,const char *b,int active) {
    WCHAR16 p[64],s[64];
    to_wide(a,p);to_wide(b,s);
    NT_CALL(xamlui_row,(dc,y,p,b?s:0,active));
}
static void action(void *dc,const char *a,const char *b) {
    WCHAR16 p[64],s[64];to_wide(a,p);to_wide(b,s);
    NT_CALL(xamlui_actionbar,(dc,p,b?s:0));
}
static void draw_storage(void *dc) {
    txt(dc,14,82,"Storage usage",WHITE,0);
    label(dc,14,111,"Last refreshed: demo",BLUE);
    label(dc,14,147,"Total: 29.1 GB (example)",WHITE);
    progress(dc,14,178,290,99);
    label(dc,14,198,"Used: 28.9 GB",WHITE);
    const char *groups[5]={"Apps & games","System & reserved",
                           "Pictures","Music","Maps"};
    const char *sizes[5]={"16.3 GB","4.43 GB","4.26 GB","3.06 GB","406 MB"};
    for(int i=0;i<5;++i) {
        int y=235+i*46;
        tile(dc,14,y+2,11,11,BLUE);
        label(dc,34,y,groups[i],WHITE);
        label(dc,238,y,sizes[i],DIM);
        progress(dc,34,y+23,269,percentages[i]);
    }
}
static void draw_clear(void *dc) {
    label(dc,14,73,"Clear browsing data",WHITE);
    const char *items[7]={
        "Browsing history","Cookies and website data",
        "Cached data and files","Recovery data","Download history",
        "Form data","Passwords"
    };
    for(int i=0;i<7;++i)
        checkbox(dc,14,117+i*39,items[i],(checked>>i)&1);
    label(dc,14,405,"Show more  v",DIM);
    tile(dc,14,443,126,33,PANEL);
    label(dc,62,450,"Clear",WHITE);
}
static void draw_music(void *dc) {
    WCHAR16 a[64],b[64],c[64];
    to_wide("Songs",a);to_wide("Artists",b);to_wide("Albums",c);
    NT_CALL(xamlui_pivot,(dc,tab,a,b,c));
    label(dc,14,107,"Refine: A to Z",BLUE);
    label(dc,14,154,"Nothing to show here.",WHITE);
    label(dc,14,178,"Try a different filter.",WHITE);
    tile(dc,0,449,320,39,BLUE);
    label(dc,16,461,"Unknown Artist",WHITE);
    label(dc,211,461,"|<   >   >|",WHITE);
}
static void draw_maps(void *dc) {
    label(dc,14,87,"Maps",WHITE);
    label(dc,14,124,"Download offline maps for",WHITE);
    label(dc,14,147,"apps when you're offline.",WHITE);
    tile(dc,14,189,30,30,WHITE);
    tile(dc,16,191,26,26,C(0,0,0));
    label(dc,25,195,"+",WHITE);
    label(dc,57,194,"Download maps",WHITE);
    xrow(dc,244,"England","United Kingdom",0);
    xrow(dc,317,"Wales","United Kingdom",1);
    label(dc,196,383,"77.4 MB",WHITE);
    tile(dc,236,410,69,27,PANEL);
    label(dc,251,417,"Delete",WHITE);
    action(dc,"Delete all","Storage");
}
static void draw_files(void *dc) {
    label(dc,14,79,"Sort by: Name  v",DIM);
    for(int i=0;i<3;++i) {
        int x=30+i*102;
        tile(dc,x,132,48,60,C(255,221,139));
        tile(dc,x+6,128,32,11,C(255,221,139));
    }
    label(dc,20,207,"Audiobooks",WHITE);
    label(dc,123,207,"Artists",WHITE);
    label(dc,222,207,"Albums",WHITE);
    if(tab) {
        tile(dc,108,219,198,218,C(41,41,41));
        const char *actions[5]={"Delete","Move to","Copy to","Rename",
                                "Properties"};
        for(int i=0;i<5;++i) label(dc,122,236+i*39,actions[i],WHITE);
    }
    action(dc,"Select","More  ...");
}
static void draw(void *dc) {
    WCHAR16 name[64];to_wide(names[screen],name);
    NT_CALL(xamlui_shell,(dc,name,screen));
    if(screen==0)draw_storage(dc);
    else if(screen==1)draw_clear(dc);
    else if(screen==2)draw_music(dc);
    else if(screen==3)draw_maps(dc);
    else draw_files(dc);
    if(menu_open)NT_CALL(xamlui_menu,(dc));
    if(dialog_open) {
        WCHAR16 title_w[64],msg[64];
        to_wide("Are you sure?",title_w);
        to_wide("This is a demo action.",msg);
        NT_CALL(xamlui_dialog,(dc,title_w,msg));
    }
}
static void redraw(void *hwnd) {NT_CALL(InvalidateRect,(hwnd,0,1));}
static void select_menu(int y) {
    if(y<118)screen=0;
    else if(y<176)screen=1;
    else if(y<230)screen=2;
    else if(y<300)screen=3;
    menu_open=0;tab=0;
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps={0};
        void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if(dc) {draw(dc);NT_CALL(EndPaint,(hwnd,&ps));}
        return 0;
    }
    if(msg==WM_KEYDOWN) {
        if(wp==VK_RIGHT)screen=(screen+1)%5;
        else if(wp==VK_LEFT)screen=(screen+4)%5;
        else if(wp==VK_RETURN)menu_open=!menu_open;
        else if(wp=='0')dialog_open=!dialog_open;
        else return 0;
    } else if(msg==WM_LBUTTONDOWN) {
        int x=(short)(lp&0xffffu),y=(short)(lp>>16);
        if(dialog_open) {
            if(y>260 && y<310)dialog_open=0;
        } else if(menu_open) {
            if(x<225 && y>=50 && y<322)select_menu(y);
            else menu_open=0;
        } else if(y<50) {
            if(x<54)menu_open=1;
            else if(x>=267)screen=4;
        } else if(y>=488) {
            if(x<111) {screen=0;tab=0;}
            else if(x<211) {screen=0;menu_open=0;}
            else {screen=1;}
        } else if(screen==1) {
            if(y>=117 && y<390) {
                int k=(y-117)/39;
                if(k>=0 && k<7)checked^=1<<k;
            } else if(y>440 && y<482)dialog_open=1;
        } else if(screen==2 && y>=52 && y<90) {
            tab=x/106;if(tab>2)tab=2;
        } else if(screen==3 && y>400 && y<455) dialog_open=1;
        else if(screen==4 && y>115 && y<449) tab=!tab;
        else if(screen==0 && y>230 && y<450) screen=1;
    } else return 0;
    redraw(hwnd);
    return 0;
}
int main(void) {ui_init();return ui_run(cls,title,wndproc);}
