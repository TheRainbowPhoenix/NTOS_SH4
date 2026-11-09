#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
IMP(int,svf_SVFDraw,(void *,const void *,unsigned,int,int,int,int))
IMP(int,SipShowIM,(U32))
IMP(int,SipGetInfo,(SipInfo *))
#include "system_icons.h"
static int nav,panel,tracking,origin;static unsigned toggles=(1u<<5)|(1u<<13);static char notification[64];
static const char *labels[16]={"Connect","Battery saver","Flashlight","Note","VPN","Location","Airplane","Camera","Cellular data","Mobile hotspot","Disabled","Suggested","Wi-Fi","Not connected","Rotation lock","All settings"};
static const unsigned icons[16]={SYS_CONNECT,SYS_BATTERY,SYS_FLASH,SYS_NOTE,SYS_VPN,SYS_LOCATION,SYS_AIRPLANE,SYS_CAMERA,SYS_CELL,SYS_HOTSPOT,SYS_MOON,SYS_SUN,SYS_WIFI,SYS_BLUETOOTH,SYS_LOCK,SYS_SETTINGS};
static void bar(void *dc,const Render *r){r->fill(dc,0,0,320,24,0);
 for(int i=0;i<5;++i)r->fill(dc,13+3*i,16-2*i,15+3*i,18,GRAY);
 r->line(dc,249,7,266,7,GRAY);r->line(dc,249,16,266,16,GRAY);
 r->line(dc,249,7,249,16,GRAY);r->line(dc,266,7,266,16,GRAY);
 r->fill(dc,251,9,263,15,GRAY);r->fill(dc,267,10,269,14,GRAY);
 r->text(dc,283,4,"8:02",GRAY,12);
}
static void paint(void *dc){const Render *r=CALL(render_SpGetThreadContext,());r->clip(0,0,320,528);
 if(panel){r->fill(dc,0,0,320,528,0);r->text(dc,10,30,"Demo GSM Network LTE+",GRAY,9);r->text(dc,244,30,"100%   11/9",GRAY,9);
  for(unsigned i=0;i<16;++i){int x=4+(int)(i%4)*79,y=65+(int)(i/4)*64;U32 c=toggles&(1u<<i)?BLUE:RGB_(48,48,48);
   r->fill(dc,x,y,x+75,y+59,c);const SysIcon *icon=&system_icons[icons[i]];
   CALL(svf_SVFDraw,(dc,icon->data,icon->length,x+5,y+4,18,18));
   if(i==6){r->text(dc,x+5,y+28,"Airplane",WHITE,12);r->text(dc,x+5,y+43,"mode",WHITE,12);}
   else if(i==9){r->text(dc,x+5,y+28,"Mobile",WHITE,12);r->text(dc,x+5,y+43,"hotspot",WHITE,12);}
   else if(i==13){r->text(dc,x+5,y+28,"Not",WHITE,12);r->text(dc,x+5,y+43,"connected",WHITE,12);}
   else r->text(dc,x+5,y+40,labels[i],WHITE,i==1||i==8||i==14||i==15?11:12);
  }
  r->text(dc,262,337,"Collapse",GRAY,12);
  if(notification[0]){r->text(dc,12,382,"Shell",WHITE,14);r->text(dc,12,407,notification,GRAY,12);r->text(dc,255,452,"Clear all",GRAY,12);}
  else r->text(dc,12,382,"No notifications",GRAY,16);
  r->fill(dc,0,512,320,528,BLUE);r->fill(dc,152,518,168,519,WHITE);r->fill(dc,152,521,168,522,WHITE);
 }
 bar(dc,r);
}
static int input(U32 msg,U32 wp,U32 lp){int x=(int)(short)(lp&65535u),y=(int)(short)(lp>>16);
 if(msg==WM_KEY_&&panel&&(wp==0x1b||wp==0x25)){panel=tracking=0;return 1;}
 if(msg==WM_DOWN_){if(panel){tracking=1;origin=y;
   if(y>=65&&y<321){int col=div_(x-4,79),row=div_(y-65,64);if(x>=4&&col<4&&row<4){unsigned i=(unsigned)(row*4+col);
    if(i==15){panel=tracking=0;return 2;}toggles^=1u<<i;
    const char *s=labels[i];unsigned n=0;while(s[n]&&n<45){notification[n]=s[n];++n;}
    const char *tail=toggles&(1u<<i)?" enabled (demo)":" disabled (demo)";unsigned j=0;while(tail[j]&&n<63)notification[n++]=tail[j++];notification[n]=0;}
   }else if(y>=330&&y<365){panel=tracking=0;}else if(y>=440&&y<490)notification[0]=0;else if(y>=505)panel=tracking=0;return 1;}
  tracking=y<24;origin=y;return tracking;}
 if(msg==WM_MOVE_&&tracking){if(!panel&&y-origin>25){panel=1;CALL(SipShowIM,(0));return 1;}
  if(panel&&origin-y>60){panel=tracking=0;return 1;}return panel||origin<24;}
 return panel;
}
static int expanded(void){return panel;}
static const Chrome chrome={PROFILE,paint,input,expanded};
const Chrome *UIXGetSchemaFactory(void){return &chrome;}
void UIXSchemaUnloaded(void){panel=tracking=0;}
int InitializeShellChromeComponents(void){panel=tracking=nav=0;return 0;}
int GetSipVisibility(void){SipInfo s;s.size=sizeof(s);return CALL(SipGetInfo,(&s))?(int)(s.flags&1u):0;}
int SetSipVisibility(int visible){return CALL(SipShowIM,(visible?1u:0u))?0:-1;}
int GetNavigationBarHeight(void){return nav?40:0;}
int GetIsNavigationBarVisible(void){return nav;}
int SetIsNavigationBarVisible(int visible){nav=visible!=0;return 0;}
int GetLockScreenVisible(void){return 0;}
int GetWaitCursorVisible(void){return 0;}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
