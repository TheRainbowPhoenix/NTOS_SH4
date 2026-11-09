/* Prepared native Settings views; values are local previews, not hardware configuration. */
#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
IMP(int,svf_SVFDraw,(void *,const void *,unsigned,int,int,int,int))
IMP(int,SipShowIM,(U32))
#include "settings_icons.h"
static int view,category,scroll_y,drag_y,drag_scroll,search,len;
static int brightness=50,automatic=1,rotation,scale=100;
static int saved_brightness=50,saved_automatic=1,saved_rotation,saved_scale=100;
static char query[40];
static const char *names[]={"System","Devices","Network & wireless","Personalization","Accounts","Time & language","Ease of Access","Privacy"};
static const char *details[]={"Display, notifications, battery","Bluetooth, camera","Wi-Fi, airplane mode, cellular","Start, lock screen, sounds","Your accounts, email, sync","Speech, region, keyboard","Vision, hearing, interaction","Location, camera, permissions"};
static const char *system_names[]={"Display","Notifications & actions","Phone","Messaging","Storage","Battery","Offline maps"};
static void icon(void *dc,unsigned which,int x,int y,int size){const SettingsIcon *i=&settings_icons[which];CALL(svf_SVFDraw,(dc,i->data,i->length,x,y,size,size));}
static int matches(const char *s){if(!len)return 1;for(unsigned j=0;s[j];++j){int i=0;while(i<len&&s[j+i]){int a=s[j+i],b=query[i];if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)break;++i;}if(i==len)return 1;}return 0;}
static void toggle(void *dc,const Render *r,int y,int on){U32 c=on?BLUE:GRAY;
 r->ellipse(dc,22,y+10,10,10,c,on?0:2);r->ellipse(dc,45,y+10,10,10,c,on?0:2);
 r->fill(dc,22,y,45,y+2,c);r->fill(dc,22,y+18,45,y+20,c);
 if(on)r->fill(dc,22,y,45,y+20,c);
 r->ellipse(dc,on?45:22,y+10,5,5,WHITE,0);r->text(dc,68,y+2,on?"On":"Off",WHITE,14);
}
static void slider(void *dc,const Render *r,int y,int value){int x=12+div_(value*276,100);
 r->fill(dc,12,y,292,y+2,RGB_(100,100,100));r->fill(dc,12,y,x,y+2,BLUE);
 r->fill(dc,x-4,y-8,x+4,y+11,BLUE);r->ellipse(dc,x,y-8,4,4,BLUE,0);r->ellipse(dc,x,y+10,4,4,BLUE,0);}
static void paint(void *dc){const Render *r=CALL(render_SpGetThreadContext,());r->clip(0,24,320,528);
 r->fill(dc,0,24,320,528,view==2?0:RGB_(23,23,23));r->fill(dc,0,24,320,72,RGB_(23,23,23));
 icon(dc,8,12,36,24);r->text(dc,48,40,view==2?"Display":"Settings",WHITE,14);
 if(view!=2){r->fill(dc,12,80,308,112,BLUE);r->fill(dc,14,82,306,110,WHITE);
  r->text(dc,23,89,len?query:search?"":"Find a setting",search?0:GRAY,14);
  if(search)r->fill(dc,23+len*7,87,24+len*7,102,0);
  r->ellipse(dc,292,93,5,5,GRAY,1);r->line(dc,296,97,300,101,GRAY);}
 if(view==0){r->clip(0,122,320,528);int row=0;
  for(unsigned i=0;i<8;++i){if(!matches(names[i]))continue;int y=132+row++*64-scroll_y;icon(dc,i,12,y+4,32);
   r->text(dc,54,y+3,names[i],WHITE,14);r->text(dc,54,y+24,details[i],GRAY,12);}}
 else if(view==1){r->text(dc,12,143,"System",GRAY,14);r->clip(0,178,320,528);
  for(unsigned i=0;i<7;++i){int y=186+(int)i*48-scroll_y;icon(dc,9+i,12,y+2,17);r->text(dc,39,y+4,system_names[i],WHITE,14);}}
 else if(view==2){r->text(dc,12,98,"Display",WHITE,22);r->text(dc,12,147,"Adjust brightness level",WHITE,14);slider(dc,r,181,brightness);
  r->text(dc,12,211,"Change brightness automatically when",WHITE,14);r->text(dc,12,232,"lighting changes",WHITE,14);toggle(dc,r,257,automatic);
  r->text(dc,12,302,"Rotation lock",WHITE,14);toggle(dc,r,327,rotation);
  r->text(dc,12,371,"Size of text, apps, and items on this display",WHITE,14);slider(dc,r,413,scale);
  r->fill(dc,12,439,102,471,RGB_(46,46,46));r->text(dc,38,449,"Apply",WHITE,14);
  r->fill(dc,122,439,212,471,RGB_(46,46,46));r->text(dc,145,449,"Cancel",WHITE,14);
  r->text(dc,12,503,"Show a dot on the connected display when",WHITE,14);
 }
 else {r->text(dc,12,145,names[category],WHITE,22);r->text(dc,12,200,"Settings will appear here.",GRAY,14);}
 r->clip(0,0,320,528);
}
static int input(U32 msg,U32 wp,U32 lp){int x=(int)(short)(lp&65535u),y=(int)(short)(lp>>16);
 if(msg==WM_KEY_){if(wp==0x1b||wp==0x25){if(view==0)return -1;view=view==2?1:0;scroll_y=0;return 1;}
  if(wp==0x71){search=1;return 1;}
  if(wp==0x26||wp==0x28){scroll_y=clampi(scroll_y+(wp==0x28?48:-48),0,view==0?128:16);return 1;}
  if(search&&wp==8){if(len)query[--len]=0;return 1;}
  if(wp==13){search=0;CALL(SipShowIM,(0));return 1;}}
 if((msg==WM_CHAR_||msg==WM_KEY_)&&search&&wp>=32&&wp<127&&len<35){query[len++]=(char)wp;query[len]=0;scroll_y=0;return 1;}
 if(msg==WM_DOWN_){drag_y=y;drag_scroll=scroll_y;
  if(y>=24&&y<72&&x<44){if(!view)return -1;view=view==2?1:0;scroll_y=0;return 1;}
  if(view!=2&&y>=80&&y<112){search=1;CALL(SipShowIM,(1));return 1;}
  if(view==0&&y>=124){int selected=div_(y-132+scroll_y,64),row=0;
   for(unsigned i=0;i<8;++i)if(matches(names[i])&&row++==selected){category=(int)i;view=i==0?1:3;scroll_y=search=len=0;query[0]=0;CALL(SipShowIM,(0));break;}return 1;}
  if(view==1&&y>=178){int row=div_(y-186+scroll_y,48);if(row==0){view=2;CALL(SipShowIM,(0));}return 1;}
  if(view==2){if(y>=168&&y<=198)brightness=clampi(div_((x-12)*100,276),0,100);
   else if(y>=252&&y<=282)automatic=!automatic;
   else if(y>=322&&y<=352)rotation=!rotation;
   else if(y>=397&&y<=429)scale=clampi(div_((x-12)*100,276),0,100);
   else if(y>=439&&y<=471){if(x>=12&&x<102){saved_brightness=brightness;saved_automatic=automatic;saved_rotation=rotation;saved_scale=scale;}
    else if(x>=122&&x<212){brightness=saved_brightness;automatic=saved_automatic;rotation=saved_rotation;scale=saved_scale;}}return 1;}}
 if(msg==WM_MOVE_&&view!=2){scroll_y=clampi(drag_scroll+drag_y-y,0,view==0?128:16);return 1;}return 0;
}
static void reset(void){view=category=scroll_y=search=len=0;query[0]=0;}
static int page(void){return view;}
static const Settings settings={PROFILE,paint,input,reset,page};
const Settings *SettingsGetView(void){return &settings;}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
