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
NT_IMPORT(void,xamlui_folder,(void *,int,int,int,int,DWORD))
NT_IMPORT(void,xamlui_start,(void *,int))
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
NT_DEFINE(xamlui_folder)
NT_DEFINE(xamlui_start)
#define C(r,g,b) RGB(r,g,b)
#define WHITE C(240,240,240)
#define BLUE C(51,103,241)
#define DIM C(158,158,158)
#define PANEL C(31,31,31)
static const WCHAR16 cls[]={'M','E','T','R','O','W','I','N',0};
static const WCHAR16 title[]={'X','A','M','L','U','I',' ','D','e','m','o',0};
/* screen 0..4 = apps, 5 = Start (home). */
static int screen=0,tab=0,menu_open=0,dialog_open=0,checked=4;
static int expanded=0,start_page=0,search_mode=0,music_filter=0;
static int prior_screen=0,contact_selected=0,folder_menu=0;
static int touch_origin_y=-1,touch_origin_x=-1;
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
    label(dc,14,70,"Clear browsing data",WHITE);
    const char *items[9]={
        "Browsing history","Cookies and website data",
        "Cached data and files","Recovery data","Download history",
        "Form data","Passwords","Media licenses","Site permissions"
    };
    int visible=expanded ? 9 : 7;
    /* Extra controls are laid out as scrollable rows once expanded. */
    int step=expanded ? 33 : 39;
    for(int i=0;i<visible;++i)
        checkbox(dc,14,109+i*step,items[i],(checked>>i)&1);
    int sy=expanded ? 418 : 398;
    label(dc,14,sy,expanded?"Show less  ^":"Show more  v",BLUE);
    tile(dc,14,446,126,33,PANEL);
    label(dc,61,453,"Clear",WHITE);
}
static void draw_music(void *dc) {
    WCHAR16 a[64],b[64],c[64];
    to_wide("Songs",a);to_wide("Artists",b);to_wide("Albums",c);
    NT_CALL(xamlui_pivot,(dc,tab,a,b,c));
    label(dc,14,103,music_filter?"Refine: On this device":"Refine: A to Z",BLUE);
    if(tab==0) {
        label(dc,14,159,"No songs found on this device.",WHITE);
        label(dc,14,191,"Try a different filter.",DIM);
    } else if(tab==1) {
        txt(dc,14,138,"Artists",WHITE,1);
        xrow(dc,205,"Unknown Artist","No local artist artwork",1);
        label(dc,14,302,"Tap artist to view albums",DIM);
    } else {
        txt(dc,14,138,"Albums",WHITE,1);
        xrow(dc,207,"Unknown Album","2015 - Unknown Artist",1);
        xrow(dc,288,"Sample Playlist","No playable files found",0);
    }
    tile(dc,0,449,320,39,BLUE);
    label(dc,16,458,music_filter?"Local collection":"Unknown Artist",WHITE);
    label(dc,222,458,"|<  >  >|",WHITE);
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
    label(dc,14,78,"Sort by: Name   v",DIM);
    for(int i=0;i<3;++i) {
        int x=26+i*101;
        NT_CALL(xamlui_folder,(dc,x,131,57,65,C(255,221,139)));
    }
    label(dc,18,215,"Audio",WHITE);
    label(dc,120,215,"Artists",WHITE);
    label(dc,220,215,"Albums",WHITE);
    if(folder_menu) {
        tile(dc,85,235,225,207,C(40,40,40));
        const char *items[5]={"Delete","Move to","Copy to",
                              "Rename","Properties"};
        for(int i=0;i<5;++i)label(dc,109,244+i*39,items[i],WHITE);
    }
    action(dc,"Select","More  ...");
}
static void draw(void *dc) {
    if(screen==5) {
        NT_CALL(xamlui_start,(dc,start_page));
    } else {
        WCHAR16 name[64];to_wide(names[screen],name);
        NT_CALL(xamlui_shell,(dc,name,screen));
        if(screen==0)draw_storage(dc);
        else if(screen==1)draw_clear(dc);
        else if(screen==2)draw_music(dc);
        else if(screen==3)draw_maps(dc);
        else draw_files(dc);
    }
    if(search_mode) {
        tile(dc,0,49,320,74,PANEL);
        label(dc,12,67,"Search settings and apps",WHITE);
        label(dc,12,92,"Tap to close search",DIM);
    }
    if(menu_open)NT_CALL(xamlui_menu,(dc));
    if(dialog_open) {
        WCHAR16 caption[64],body[64];
        to_wide("Are you sure?",caption);
        to_wide("This is a demo action.",body);
        NT_CALL(xamlui_dialog,(dc,caption,body));
    }
}
static void redraw(void *hwnd) {NT_CALL(InvalidateRect,(hwnd,0,1));}
static void select_menu(int y) {
    int idx=(y-65)/66;
    if(idx>=0 && idx<5)screen=idx;
    menu_open=0;tab=0;
}
static void back_action(void) {
    if(dialog_open){dialog_open=0;return;}
    if(menu_open){menu_open=0;return;}
    if(search_mode){search_mode=0;return;}
    if(screen==5){screen=prior_screen;return;}
    prior_screen=screen;screen=5;
}
static void home_action(void) {
    if(screen!=5)prior_screen=screen;
    screen=5;menu_open=0;dialog_open=0;search_mode=0;
}
static void select_start_tile(int x,int y) {
    int col=x>=160, row=(y-79+start_page*270)/129;
    int which=row*2+col;
    static const int route[6]={1,0,2,4,0,3};
    if(which>=0 && which<6){screen=route[which];tab=0;}
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps={0};
        void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if(dc){draw(dc);NT_CALL(EndPaint,(hwnd,&ps));}
        return 0;
    }
    if(msg==WM_KEYDOWN) {
        if(wp==VK_RIGHT) {
            if(screen==5)start_page=1;
            else if(screen==2)tab=(tab+1)%3;
            else screen=(screen+1)%5;
        } else if(wp==VK_LEFT) {
            if(screen==5)start_page=0;
            else if(screen==2)tab=(tab+2)%3;
            else screen=(screen+4)%5;
        } else if(wp==VK_UP && screen==5)start_page=0;
        else if(wp==VK_DOWN && screen==5)start_page=1;
        else if(wp==VK_RETURN)menu_open=!menu_open;
        else if(wp=='0')home_action();
        else return 0;
    } else if(msg==WM_LBUTTONDOWN || msg==0x0200u) {
        int x=(short)(lp&65535u),y=(short)(lp>>16);
        if(msg==0x0200u) {
            if(screen==5 && touch_origin_y>=0) {
                if(touch_origin_y-y>55)start_page=1;
                if(y-touch_origin_y>55)start_page=0;
            }
            redraw(hwnd);
            return 0;
        }
        touch_origin_x=x;touch_origin_y=y;
        if(dialog_open) {
            if(y>=258 && y<310)dialog_open=0;
        } else if(menu_open) {
            if(y>=76 && y<447)select_menu(y);
            else menu_open=0;
        } else if(y>=488) {
            if(x<110)back_action();
            else if(x<211)home_action();
            else search_mode=!search_mode;
        } else if(search_mode)search_mode=0;
        else if(screen==5) {
            if(y>=58 && y<455) {
                if(x>=275 && y>=430)start_page=!start_page;
                else select_start_tile(x,y);
            }
        } else if(y<50) {
            if(x<54)menu_open=1;
            else if(x>=267)search_mode=1;
        } else if(screen==1) {
            int step=expanded?33:39;
            int count=expanded?9:7;
            if(y>=109 && y<109+count*step) {
                int i=(y-109)/step;
                if(i>=0 && i<count)checked^=1<<i;
            } else if(y>=394 && y<444)expanded=!expanded;
            else if(y>=445 && y<488)dialog_open=1;
        } else if(screen==2) {
            if(y>=52 && y<90) {
                tab=x/106;if(tab>2)tab=2;
            } else if(y>=100 && y<126)music_filter=!music_filter;
            else if(y>=199 && y<393 && tab!=0)dialog_open=1;
        } else if(screen==3 && y>=399 && y<488)dialog_open=1;
        else if(screen==4 && y>=115 && y<488) {
            folder_menu=!folder_menu;
        } else if(screen==0 && y>=230 && y<450)screen=1;
    } else return 0;
    redraw(hwnd);
    return 0;
}
int main(void) {ui_init();return ui_run(cls,title,wndproc);}
