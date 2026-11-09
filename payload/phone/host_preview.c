/* Native QA executes the SAME C renderer, menu and icon paths as the SH4
 * build. Only WinCE GDI FillRect is replaced by an RGB565 memory surface. */
#include "phone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[320*528];
const Render *render_SpGetThreadContext(void);
const Menu *menu_UIXGetSchemaFactory(void);
const Chrome *chrome_UIXGetSchemaFactory(void);
void menu_StartMenuInitialize(void);
void menu_StartMenuUninitialize(void);
static unsigned fills;
void *CreateSolidBrush(U32 c){unsigned rgb=((c&255u)>>3)<<11|(((c>>8)&255u)>>2)<<5|(((c>>16)&255u)>>3);
 return (void *)(uintptr_t)(0x1000000u|rgb);}
int DeleteObject(void *b){return b!=0;}
int FillRect(void *dc,const Rect *rect,void *b){assert(dc&&b);
 assert(rect->left>=0&&rect->top>=0&&rect->right<=320&&rect->bottom<=528);
 assert(rect->left<rect->right&&rect->top<rect->bottom);++fills;
 for(int y=rect->top;y<rect->bottom;++y){for(int x=rect->left;x<rect->right;++x){
  pixels[y*320+x]=(uint16_t)(uintptr_t)b;}}
 return 1;}
/* Minimal test-only window server. Device builds use COREDLL.DLL instead. */
typedef int (*Proc)(void *,U32,U32,U32);
typedef struct {U32 style;Proc proc;int a,b;void *i,*icon,*cursor,*brush;const W16 *menu,*name;} HostClass;
static Proc wndproc;static int alive;
U32 RegisterClassW(const HostClass *wc){assert(wc&&wc->proc);wndproc=wc->proc;return 1;}
void *CreateWindowExW(U32 a,const W16 *b,const W16 *c,U32 d,int x,int y,int w,int h,void *e,void *f,void *g,void *i){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)i;
 assert(x==0&&y==0&&w==320&&h==528);assert(!alive);alive=1;return (void *)1;}
int ShowWindow(void *w,int c){assert(w&&c);return 1;}
int UpdateWindow(void *w){assert(alive);return wndproc(w,WM_PAINT_,0,0);}
int InvalidateRect(void *w,const Rect *a,int b){(void)a;(void)b;return w!=0;}
int GetMessageW(void *m,void *w,U32 a,U32 b){(void)m;(void)w;(void)a;(void)b;return 0;}
int TranslateMessage(const void *m){return m!=0;}
int DispatchMessageW(const void *m){return m!=0;}
int DestroyWindow(void *w){assert(alive&&w);alive=0;return 1;}
void *BeginPaint(void *w,void *p){(void)p;return w;}
int EndPaint(void *w,const void *p){(void)p;return w!=0;}
int phone_main(void);
static void snap(const Menu *m,const Chrome *ch,const char *path){
 m->paint((void *)1);ch->paint((void *)1);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n320 528\n255\n");
 for(int i=0;i<320*528;++i){unsigned p=pixels[i];unsigned char rgb[3]={
  (unsigned char)((p>>11)*255/31),(unsigned char)(((p>>5)&63)*255/63),
  (unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
}
static U32 pos(int x,int y){return (U32)x|((U32)y<<16);}
int main(int argc,char **argv){assert(argc==2);char path[1024];
 const Menu *m=menu_UIXGetSchemaFactory();const Chrome *ch=chrome_UIXGetSchemaFactory();
 menu_StartMenuInitialize();assert(m->profile==PROFILE);assert(m->page()==0&&m->scroll()==0);
 snprintf(path,sizeof(path),"%s/home.ppm",argv[1]);snap(m,ch,path);
 assert(pixels[29*320+7]==(uint16_t)(uintptr_t)CreateSolidBrush(BLUE));
 assert(pixels[29*320+55]==0);assert(pixels[130*320+20]==0);
 for(int i=0;i<4;++i)m->input(WM_KEY_,0x28,0);
 assert(m->scroll()==172);snprintf(path,sizeof(path),"%s/home-scrolled.ppm",argv[1]);snap(m,ch,path);
 m->input(WM_KEY_,0x27,0);assert(m->page()==1);
 snprintf(path,sizeof(path),"%s/all-apps.ppm",argv[1]);snap(m,ch,path);
 /* Drag the list from an app row: selection must not block scrolling. */
 m->input(WM_DOWN_,0,pos(80,350));m->input(WM_MOVE_,0,pos(80,160));assert(m->scroll()==190);
 m->input(WM_KEY_,0x25,0);m->input(WM_KEY_,0x27,0);
 m->input(WM_DOWN_,0,pos(20,44));const char *q="Camera";
 for(;*q;++q)m->input(WM_CHAR_,(unsigned char)*q,0);
 assert(m->scroll()==0);snprintf(path,sizeof(path),"%s/search.ppm",argv[1]);snap(m,ch,path);
 m->input(WM_KEY_,0x25,0);m->input(WM_KEY_,0x27,0);
 m->input(WM_DOWN_,0,pos(10,90));snprintf(path,sizeof(path),"%s/letters.ppm",argv[1]);snap(m,ch,path);
 m->input(WM_DOWN_,0,pos(247,265));assert(m->scroll()>0);
 /* Unknown input and huge drags cannot draw outside the framebuffer. */
 m->input(WM_DOWN_,0,pos(80,350));m->input(WM_MOVE_,0,pos(80,32000));
 snprintf(path,sizeof(path),"%s/list-reset.ppm",argv[1]);snap(m,ch,path);
 menu_StartMenuUninitialize();menu_StartMenuInitialize();assert(m->page()==0&&m->scroll()==0);
 assert(phone_main()==0);assert(!alive);
 printf("PASS actual C paint, RGB565, clipping, scrolling, search, alphabet jump, reinit (%u fills)\n",fills);
 return 0;
}
