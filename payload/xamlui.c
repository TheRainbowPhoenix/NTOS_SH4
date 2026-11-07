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
static int poly_idiv(int a,int b); /* defined with the polygon helpers */
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
/* Convex/concave filled polygons via integer scanline intersection.
 * No floating point, polygon fill and strokes are implemented within
 * XAMLUI.DLL and ultimately use genuine WinCE GDI FillRect. */
/* SH4 freestanding: variable signed / would emit __divsi3.
 * Division below is explicitly shift/subtract, no libgcc needed. */
static unsigned poly_udiv(unsigned num,unsigned den) {
    unsigned quotient=0,remainder=0;
    if(!den)return 0;
    for(int i=31;i>=0;--i) {
        unsigned carry=remainder>>31;
        remainder=(remainder<<1)|((num>>(unsigned)i)&1u);
        if(carry || remainder>=den) {
            remainder-=den;
            quotient|=1u<<(unsigned)i;
        }
    }
    return quotient;
}
static int poly_idiv(int a,int b) {
    int minus=(a<0)^(b<0);
    unsigned n=a<0?(unsigned)(-(a+1))+1u:(unsigned)a;
    unsigned d=b<0?(unsigned)(-(b+1))+1u:(unsigned)b;
    unsigned q=poly_udiv(n,d);
    return minus?-(int)q:(int)q;
}
static void stroke(void *dc,int x0,int y0,int x1,int y1,u32 color) {
    int dx=x1-x0,dy=y1-y0,sx=1,sy=1,err,e2;
    if(dx<0){dx=-dx;sx=-1;}
    if(dy<0){dy=-dy;sy=-1;}
    err=dx-dy;
    for(;;) {
        xamlui_tile(dc,x0,y0,x0+1,y0+1,color);
        if(x0==x1 && y0==y1)break;
        e2=2*err;
        if(e2>-dy){err-=dy;x0+=sx;}
        if(e2<dx){err+=dx;y0+=sy;}
    }
}
void xamlui_polygon(void *dc,const POINT *pts,int n,u32 fill,u32 border) {
    if(!dc || !pts || n<3 || n>12)return;
    int lo=527,hi=0;
    for(int i=0;i<n;++i) {
        if(pts[i].y<lo)lo=pts[i].y;
        if(pts[i].y>hi)hi=pts[i].y;
    }
    if(lo<0)lo=0;
    if(hi>527)hi=527;
    for(int y=lo;y<=hi;++y) {
        int inter[12],count=0;
        for(int i=0;i<n;++i) {
            const POINT a=pts[i],b=pts[(i+1==n)?0:i+1];
            int y0=a.y,y1=b.y;
            if(y0==y1)continue;
            if(y0>y1){int t=y0;y0=y1;y1=t;}
            if(y>=y0 && y<y1 && count<12)
                inter[count++]=a.x+poly_idiv((y-a.y)*(b.x-a.x),(b.y-a.y));
        }
        for(int i=1;i<count;++i) {
            int t=inter[i],j=i-1;
            while(j>=0 && inter[j]>t){inter[j+1]=inter[j];--j;}
            inter[j+1]=t;
        }
        for(int i=0;i+1<count;i+=2) {
            int l=inter[i],r=inter[i+1];
            if(l<0)l=0;
            if(r>320)r=320;
            if(r>l)xamlui_tile(dc,l,y,r,y+1,fill);
        }
    }
    if(border!=0xffffffffu)
        for(int i=0;i<n;++i) {
            const POINT a=pts[i],b=pts[(i+1==n)?0:i+1];
            stroke(dc,a.x,a.y,b.x,b.y,border);
        }
}
void xamlui_folder(void *dc,int x,int y,int width,int height,u32 color) {
    POINT p[8];
    p[0]=(POINT){x,y+poly_idiv(height,5)};
    p[1]=(POINT){x+poly_idiv(width,3),y+poly_idiv(height,5)};
    p[2]=(POINT){x+poly_idiv(width*2,5),y};
    p[3]=(POINT){x+width-7,y};
    p[4]=(POINT){x+width-4,y+poly_idiv(height,5)};
    p[5]=(POINT){x+width,y+poly_idiv(height,5)+3};
    p[6]=(POINT){x+width,y+height};
    p[7]=(POINT){x,y+height};
    xamlui_polygon(dc,p,8,color,RGB(220,164,64));
    xamlui_tile(dc,x+2,y+poly_idiv(height,4),x+width-2,y+poly_idiv(height,4)+1,
                RGB(255,244,182));
}
void xamlui_start(void *dc,int offset) {
    /* Two vertical tile screens; caller owns the scroll/page state. */
    static const u16 title[]={'S','t','a','r','t',0};
    static const u16 a[]={'P','h','o','n','e',0};
    static const u16 b[]={'P','e','o','p','l','e',0};
    static const u16 c[]={'M','u','s','i','c',0};
    static const u16 d[]={'P','h','o','t','o','s',0};
    static const u16 e[]={'S','t','o','r','a','g','e',0};
    static const u16 f[]={'M','a','p','s',0};
    xamlui_tile(dc,0,0,320,488,BG);
    xamlui_text(dc,14,22,title,WHITE,1);
    for(int i=0;i<6;++i) {
        int x=14+(i&1)*147, y=79+(i>>1)*129-offset*270;
        if(y+116<=57 || y>=488)continue;
        u32 col=((i==0 || i==3))?RGB(51,103,241):
                ((i==1 || i==4))?RGB(0,143,146):RGB(116,72,178);
        xamlui_tile(dc,x,y,x+137,y+117,col);
        const u16 *labels[6]={a,b,c,d,e,f};
        if(i==0) {
            for(int j=0;j<3;++j)
                xamlui_tile(dc,x+53,y+23+j*7,x+83,y+26+j*7,WHITE);
        } else if(i==3)
            xamlui_folder(dc,x+44,y+16,47,51,RGB(255,235,151));
        else {
            POINT sq[4]={{x+48,y+20},{x+88,y+20},
                         {x+88,y+65},{x+48,y+65}};
            xamlui_polygon(dc,sq,4,RGB(255,255,255),0xffffffffu);
        }
        xamlui_text(dc,x+11,y+92,labels[i],WHITE,0);
    }
    xamlui_tile(dc,0,488,320,528,BG);
    nav_icon(dc,1,68,509);
    nav_icon(dc,2,160,509);
    nav_icon(dc,3,255,509);
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
    /* Windows 10 Mobile: thick total-capacity bar, thin category bars. */
    int h=(y>=220) ? 7 : 11;
    xamlui_tile(dc,x,y,x+width,y+h,RGB(54,54,54));
    xamlui_tile(dc,x,y,x+poly_idiv(width*percent,100),y+h,BLUE);
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
    static const u16 h[]={'A','L','L',' ','A','P','P','S',0};
    static const u16 a[]={'S','t','o','r','a','g','e',' ','u','s','a','g','e',0};
    static const u16 b[]={'C','l','e','a','r',' ','b','r','o','w','s','i','n','g',0};
    static const u16 c[]={'M','y',' ','m','u','s','i','c',0};
    static const u16 d[]={'O','f','f','l','i','n','e',' ','m','a','p','s',0};
    static const u16 e[]={'F','i','l','e',' ','b','r','o','w','s','e','r',0};
    const u16 *labels[5]={a,b,c,d,e};
    xamlui_tile(dc,0,0,320,488,BG);
    xamlui_tile(dc,0,0,320,50,SURFACE);
    xamlui_text(dc,18,17,h,WHITE,0);
    for(int i=0;i<5;++i) {
        int y=76+i*73;
        xamlui_tile(dc,14,y+7,18,y+34,BLUE);
        xamlui_text(dc,29,y+13,labels[i],WHITE,0);
        xamlui_tile(dc,14,y+57,306,y+58,RGB(38,38,38));
    }
}

int main(void) { return 0; }
