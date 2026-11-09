#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
IMP(const Controls *,controls_UIXGetSchemaFactory,(void))
/* Adjacent groups remain contiguous in the SH4 PE's writable IAT. */
static const Render *r;
static const Controls *ctl;
typedef struct {int x,y,w,h,icon;U32 color;const char *label;} Tile;
/* Compact compiled records, in the reference screenshot's pixel grid. */
static const Tile tiles[]={
 {6,28,48,48,ICON_PHONE,BLUE,0},{6,80,48,48,ICON_MESSAGE,BLUE,0},
 {58,80,48,48,ICON_EDGE,BLUE,0},{110,28,100,100,-1,BLUE,0},
 {214,28,94,100,ICON_MAIL,BLUE,"Outlook Mail"},
 {6,132,100,100,ICON_PEOPLE,BLUE,"People"},
 {110,132,198,100,ICON_CORTANA,BLUE,"Cortana"},
 {6,236,204,100,ICON_PHOTO,BLUE,"Photos"},
 {214,236,94,100,ICON_STORE,BLUE,"Store"},
 {6,340,100,100,ICON_MUSIC,BLUE,"Groove Music"},
 {110,340,100,100,ICON_VIDEO,BLUE,"Movies & TV"},
 {6,444,100,100,ICON_WEATHER,RGB_(0,100,177),"Weather"},
 {110,444,100,100,ICON_NEWS,RGB_(24,91,73),"News"},
 {214,444,94,100,ICON_MONEY,RGB_(112,121,112),"Money"},
 {6,548,100,100,ICON_CALC,BLUE,"Calculator"},
 {110,548,100,100,ICON_MAP,BLUE,"Maps"},
 {214,548,48,48,ICON_WORD,RGB_(40,83,167),0},
 {266,548,42,48,ICON_POWERPOINT,RGB_(197,66,31),0},
 {214,600,48,48,ICON_EXCEL,RGB_(28,111,66),0},
 {266,600,42,48,ICON_NOTE,RGB_(129,45,120),0}
};
typedef struct {const char *name;unsigned char icon;U32 color;} AppItem;
static const AppItem apps[]={
 {"Alarms & Clock",ICON_ALARM,BLUE},{"Calculator",ICON_CALC,BLUE},
 {"Camera",ICON_CAMERA,BLUE},{"Contact Support",ICON_SUPPORT,BLUE},
 {"Cortana",ICON_CORTANA,BLUE},{"Excel",ICON_EXCEL,RGB_(28,111,66)},
 {"File Explorer",ICON_MAP,BLUE},{"Groove Music",ICON_MUSIC,BLUE},
 {"Maps",ICON_MAP,BLUE},{"Messaging",ICON_MESSAGE,BLUE},
 {"Microsoft Edge",ICON_EDGE,BLUE},{"Money",ICON_MONEY,RGB_(112,121,112)},
 {"Movies & TV",ICON_VIDEO,BLUE},{"News",ICON_NEWS,RGB_(24,91,73)},
 {"OneNote",ICON_NOTE,RGB_(129,45,120)},{"Outlook Mail",ICON_MAIL,BLUE},
 {"People",ICON_PEOPLE,BLUE},{"Phone",ICON_PHONE,BLUE},
 {"Photos",ICON_PHOTO,BLUE},{"PowerPoint",ICON_POWERPOINT,RGB_(197,66,31)},
 {"Settings",ICON_SETTINGS,BLUE},{"Store",ICON_STORE,BLUE},
 {"Weather",ICON_WEATHER,RGB_(0,100,177)},{"Word",ICON_WORD,RGB_(40,83,167)}
};
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
static int mode,offset,list_offset,jump,search,selected=-1;
static int drag_x,drag_y,drag_scroll,drag_mode;
static const char *opened;
static char query[24];static int query_len;
static int lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int match(const char *s){
 if(!query_len)return 1;
 for(int j=0;s[j];++j){int i=0;while(i<query_len&&s[j+i]&&lower(s[j+i])==lower(query[i]))++i;
  if(i==query_len)return 1;}
 return 0;
}
static void arrow(void *dc,int x,int y,int back){int d=back?-1:1;
 r->line(dc,x-9*d,y,x+9*d,y,GRAY);r->line(dc,x+9*d,y,x+2*d,y-7,GRAY);
 r->line(dc,x+9*d,y,x+2*d,y+7,GRAY);}
static void people(void *dc,int x,int y){
 /* Flat silhouettes replace the reference's decorative live imagery. */
 r->ellipse(dc,x+26,y+20,25,25,RGB_(49,80,204),0);
 r->ellipse(dc,x+73,y+18,25,25,RGB_(49,80,204),0);
 r->ellipse(dc,x+49,y+50,26,27,RGB_(94,127,255),0);
 r->ellipse(dc,x+9,y+73,25,29,RGB_(94,127,255),0);
 r->ellipse(dc,x+91,y+73,25,29,RGB_(94,127,255),0);
}
static void tile_paint(void *dc,const Tile *t,int idx){
 int y=t->y-offset;if(y+t->h<=24||y>=528)return;
 r->clip(t->x,y<24?24:y,t->x+t->w,y+t->h>528?528:y+t->h);
 r->fill(dc,t->x,y,t->x+t->w,y+t->h,t->color);
 if(idx==3){r->text(dc,t->x+30,y+19,"Sunday",WHITE,11);
  r->text(dc,t->x+37,y+40,"8",WHITE,43);}
 else if(idx==4){r->text(dc,t->x+17,y+14,"Get all your",WHITE,11);
  r->text(dc,t->x+18,y+30,"mail in one",WHITE,11);
  r->text(dc,t->x+31,y+46,"place",WHITE,11);}
 else if(idx==5)people(dc,t->x,y);
 else if(idx==6){r->ellipse(dc,t->x+99,y+48,20,20,RGB_(112,144,255),0);
  r->ellipse(dc,t->x+99,y+48,15,15,WHITE,3);
  r->ellipse(dc,t->x+99,y+48,12,12,BLUE,0);}
 else if(idx==13){r->text(dc,t->x+7,y+8,"Claude",WHITE,11);
  r->text(dc,t->x+7,y+24,"personal",WHITE,11);
  r->text(dc,t->x+7,y+40,"finance",WHITE,11);}
 else if(idx==14){ctl->icon(dc,ICON_ALARM,t->x+8,y+8,18,WHITE);
  ctl->icon(dc,ICON_SETTINGS,t->x+8,y+40,18,WHITE);
  ctl->icon(dc,ICON_CALC,t->x+58,y+58,18,WHITE);}
 else if(idx==15){r->text(dc,t->x+7,y+7,"Airport maps",WHITE,11);
  r->text(dc,t->x+7,y+23,"Find gates and",GRAY,11);
  r->text(dc,t->x+7,y+39,"shops",GRAY,11);}
 else {int sz=t->h<=48?26:34;
  ctl->icon(dc,t->icon,t->x+((t->w-sz)>>1),y+((t->h-sz)>>1)-(t->label?4:0),sz,WHITE);}
 if(t->label)r->text(dc,t->x+7,y+t->h-20,t->label,WHITE,idx==14?9:12);
 if(selected==idx){r->fill(dc,t->x,y,t->x+t->w,y+2,WHITE);
  r->fill(dc,t->x,y,t->x+2,y+t->h,WHITE);}
 r->clip(0,24,320,528);
}
static void home(void *dc){for(int i=0;i<COUNT(tiles);++i)tile_paint(dc,&tiles[i],i);
 int y=674-offset;r->text(dc,232,y-8,"All apps",WHITE,14);arrow(dc,306,y,0);
 /* Reference scroll rail, only in the right-hand gutter. */
 r->fill(dc,314,28,320,528,RGB_(28,28,28));
 r->fill(dc,314,28+div_(offset*250,172),320,278+div_(offset*250,172),RGB_(90,90,90));
}
static int rows(void *dc,int hitx,int hity){
 int y=80-list_offset;char letter=0;int hit=-1,found=0;
 for(int i=0;i<COUNT(apps);++i){if(!match(apps[i].name))continue;++found;
  char initial=apps[i].name[0];if(initial!=letter){letter=initial;
   if(dc){char s[2]={letter,0};r->text(dc,6,y+4,s,GRAY,22);}
   if(hitx>=0&&hity>=y&&hity<y+40)hit=-2;
   y+=40;}
  if(dc&&y+44>76&&y<528){r->fill(dc,6,y,44,y+40,apps[i].color);
   ctl->icon(dc,apps[i].icon,10,y+4,30,WHITE);
   r->text(dc,57,y+8,apps[i].name,WHITE,18);}
  if(hitx>=0&&hity>=y&&hity<y+44)hit=i;
  y+=44;
 }
 if(dc&&!found)r->text(dc,8,95,"No results",GRAY,16);
 return hitx<0?y+list_offset:hit;
}
static void list(void *dc){r->clip(0,76,314,528);rows(dc,-1,-1);
 r->clip(0,24,320,528);
 r->fill(dc,6,29,314,61,GRAY);r->fill(dc,8,31,312,59,0);
 r->text(dc,17,36,query_len?query:"Search",query_len?WHITE:GRAY,14);
 ctl->icon(dc,ICON_SEARCH,291,39,12,GRAY);
 r->fill(dc,308,76,320,528,RGB_(30,30,30));
 int total=rows(0,-1,-1);if(total>448){int h=div_(448*448,total);
  int pos=div_(list_offset*(448-h),total-448);
  r->fill(dc,310,80+pos,318,80+pos+h,RGB_(80,80,80));}
 if(search){r->fill(dc,18+query_len*7,52,24+query_len*7,54,WHITE);}
}
static void jump_paint(void *dc){
 for(int i=0;i<26;++i){int x=6+(i%4)*77,y=72+div_(i,4)*61;
  char s[2]={(char)('A'+i),0};r->fill(dc,x,y,x+70,y+54,BLUE);
  r->text(dc,x+23,y+10,s,WHITE,28);}
}
static void paint(void *dc){if(!r||!ctl)return;
 r->clip(0,0,320,528);r->fill(dc,0,0,320,528,0);r->clip(0,24,320,528);
 if(opened){arrow(dc,21,48,1);r->text(dc,45,38,opened,WHITE,20);
  r->text(dc,14,112,"App is not installed.",GRAY,16);}
 else if(jump)jump_paint(dc);else if(mode)list(dc);else home(dc);
 r->clip(0,0,320,528);
}
static void jump_to(int letter){int y=0;char last=0;
 for(int i=0;i<COUNT(apps);++i){char c=apps[i].name[0];if(last!=c){
  if(c>=letter){list_offset=y;return;}last=c;y+=40;}y+=44;}
}
static void move_scroll(int delta){if(!mode){offset=clampi(offset+delta,0,172);return;}
 int height=rows(0,-1,-1)-80;list_offset=clampi(list_offset+delta,0,height>448?height-448:0);}
static int input(U32 msg,U32 wp,U32 lp){
 int x=(int)(short)(lp&65535u),y=(int)(short)(lp>>16);
 if(msg==WM_KEY_){
  if(wp==0x1bu||wp==0x25u){if(opened)opened=0;else if(jump)jump=0;else {mode=0;search=0;query_len=0;query[0]=0;}return 1;}
  if(wp==0x27u){mode=1;selected=-1;return 1;}
  if(wp==0x26u||wp==0x28u){move_scroll(wp==0x26u?-52:52);return 1;}
  if(wp==0x0du){if(selected>=0){opened=mode?apps[selected].name:tiles[selected].label;
    if(!opened)opened="Phone";}else mode=!mode;return 1;}
  if(search&&wp==8&&query_len){query[--query_len]=0;list_offset=0;return 1;}
 }
 if((msg==WM_CHAR_||msg==WM_KEY_)&&search&&wp>=32&&wp<127&&query_len<23){query[query_len++]=(char)wp;query[query_len]=0;list_offset=0;return 1;}
 if(msg==WM_DOWN_){drag_x=x;drag_y=y;drag_scroll=mode?list_offset:offset;drag_mode=mode;
  if(y<24)return 0;
  if(opened){opened=0;return 1;}
  if(jump){int col=div_(x-6,77),row=div_(y-72,61);if(x>=6&&y>=72&&col>=0&&col<4&&row>=0&&row<7){int letter=row*4+col;
    if(letter<26)jump_to('A'+letter);}jump=0;return 1;}
  if(mode){if(y<65){search=1;return 1;}int i=rows(0,x,y);
   if(i==-2){jump=1;return 1;}if(i>=0){if(selected==i){opened=apps[i].name;search=0;}
    else selected=i;
    return 1;}}
  else {if(y+offset>=656){mode=1;selected=-1;return 1;}
   for(int i=0;i<COUNT(tiles);++i){const Tile *t=&tiles[i];if(x>=t->x&&x<t->x+t->w&&y+offset>=t->y&&y+offset<t->y+t->h){
    if(selected==i){opened=t->label?t->label:"Phone";}else selected=i;
    /* Two taps / Enter activates; a single pen down can begin a drag. */
    return 1;}}}
 }
 if(msg==WM_MOVE_&&!opened&&!jump&&drag_mode==mode){int target=drag_scroll+drag_y-y;
  if(mode){int h=rows(0,-1,-1)-80;list_offset=clampi(target,0,h>448?h-448:0);}
  else offset=clampi(target,0,172);
  selected=-1;return 1;}
 return 0;
}
void StartMenuInitialize(void){r=CALL(render_SpGetThreadContext,());ctl=CALL(controls_UIXGetSchemaFactory,());
 mode=offset=list_offset=jump=search=query_len=0;selected=-1;opened=0;query[0]=0;}
void StartMenuUninitialize(void){r=0;ctl=0;opened=0;}
static int page(void){return mode;}
static int scroll(void){return mode?list_offset:offset;}
static const Menu menu={PROFILE,paint,input,page,scroll};
const Menu *UIXGetSchemaFactory(void){return &menu;}
void UIXSchemaUnloaded(void){}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
