#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
IMP(int,svf_SVFDraw,(void *,const void *,unsigned,int,int,int,int))
#include "system_icons.h"
static int layout,shift,caps,dock_y;
static void dock(int y){dock_y=clampi(y,0,40);}
static unsigned kb_len(const char *s){unsigned n=0;while(s[n])++n;return n;}
static void key(void *dc,const Render *r,int l,int t,int rr,const char *label,int active){
 t-=dock_y;r->fill(dc,l,t,rr,t+50,active?RGB_(72,77,72):RGB_(47,47,47));
 int width=0;for(const char *p=label;*p;++p)width+=*p==' '?4:9;
 r->text(dc,l+((rr-l-width)>>1),t+14,label,WHITE,kb_len(label)>2?12:18);
}
static void symbol(void *dc,unsigned id,int x,int y,int size){const SysIcon *s=&system_icons[id];
 CALL(svf_SVFDraw,(dc,s->data,s->length,x,y-dock_y,size,size));}
static void paint(void *dc){const Render *r=CALL(render_SpGetThreadContext,());
 r->clip(0,271-dock_y,320,528-dock_y);r->fill(dc,0,271-dock_y,320,528-dock_y,RGB_(26,29,24));
 const char *alpha[2]={"qwertyuiop","asdfghjkl"};
 const char *num[2]={"@#$%&*()-\\","^[]{}<>ELY"};
 for(int row=0;row<2;++row){int count=layout?10:(row?9:10);
  for(int col=0;col<count;++col){char s[2]={layout?(row?num[layout-1][col]:"1234567890"[col]):alpha[row][col],0};
   if(!layout&&(shift||caps))s[0]=(char)(s[0]-32);
   int x=layout?col*32+2:col*32+(row?18:2);
   if(layout==2&&row==1&&col>=7){const char *currency=col==7?"\xe2\x82\xac":col==8?"\xc2\xa3":"\xc2\xa5";
    key(dc,r,x,317+row*53,x+28,"",0);r->text(dc,x+8,384-dock_y,currency,WHITE,18);}
   else key(dc,r,x,317+row*53,x+28,s,0);}}
 key(dc,r,2,423,47,layout?(layout==1?">":"<"):"",!layout&&(shift||caps));
 if(!layout)symbol(dc,SYS_SHIFT,18,439,16);else symbol(dc,layout==1?SYS_NEXT:SYS_PREV,17,440,16);
 const char *last=layout?(layout==1?"!;:'\"?/":"-+=_~|\\"):"zxcvbnm";
 for(int i=0;i<7;++i){char s[2]={last[i],0};if(!layout&&(shift||caps))s[0]=(char)(s[0]-32);
  key(dc,r,50+i*32,423,78+i*32,s,0);}
 key(dc,r,276,423,318,"",0);symbol(dc,SYS_DELETE,288,440,17);
 key(dc,r,2,477,47,layout?"abc":"&123",1);
 if(!layout){key(dc,r,51,477,79,":)",1);key(dc,r,83,477,111,",",0);key(dc,r,115,477,240,"",0);}
 else {key(dc,r,51,477,79,",",0);key(dc,r,83,477,240,"",0);}
 key(dc,r,244,477,272,".",0);r->fill(dc,276,477-dock_y,318,527-dock_y,WHITE);
 /* The search magnifier is a separate black VEC1 asset. */
 symbol(dc,SYS_SEARCH_BLACK,289,491,18);r->clip(0,0,320,528);
}
static int input(U32 msg,U32 wp,U32 lp){(void)wp;if(msg!=WM_DOWN_)return 0;
 int x=(int)(short)(lp&65535u),y=(int)(short)(lp>>16)+dock_y;if(x<0||x>=320||y<317||y>=528)return 0;
 if(y>=477){if(x<48){layout=layout?0:1;return 0;}if(x>=276)return 13;if(x>=244)return '.';
  if(x>=83&&x<112&&!layout)return ',';if(x>=51&&x<80&&layout)return ',';
  if((!layout&&x>=115&&x<240)||(layout&&x>=83&&x<240))return ' ';return 0;}
 int row=div_(y-317,53);if(y-(317+row*53)>=50)return 0;
 if(row==2){if(x>=276)return 8;if(x<48){if(layout)layout=layout==1?2:1;
   else if(!shift&&!caps)shift=1;else if(shift&&!caps)caps=1;else shift=caps=0;return 0;}
  int col=div_(x-50,32);if(x<50||col<0||col>=7||(x-50)%32>=28)return 0;
  int c=(layout?(layout==1?"!;:'\"?/":"-+=_~|\\"):"zxcvbnm")[col];
  if(!layout&&(shift||caps))c-=32;if(shift&&!caps)shift=0;return c;}
 int start=(!layout&&row==1)?18:2;int col=div_(x-start,32),count=(!layout&&row==1)?9:10;
 if(x<start||col<0||col>=count||(x-start)%32>=28)return 0;
 const char *s=layout?(row?(layout==1?"@#$%&*()-\\":"^[]{}<>ELY"):"1234567890"):(row?"asdfghjkl":"qwertyuiop");
 int c=s[col];if(layout==2&&row==1&&col>=7)c=col==7?0x20ac:col==8?0xa3:0xa5;
 if(!layout&&(shift||caps))c-=32;if(shift&&!caps)shift=0;return c;
}
static int page(void){return layout?layout:(shift||caps?3:0);}
static const Keyboard keyboard={PROFILE,paint,input,page,dock};
const Keyboard *UIXGetSchemaFactory(void){return &keyboard;}
void UIXSchemaUnloaded(void){layout=shift=caps=0;}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
