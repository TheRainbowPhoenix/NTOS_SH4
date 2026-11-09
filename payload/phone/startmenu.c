#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
IMP(const Catalog *,catalog_CatalogGet,(void))
IMP(int,catalog_CatalogReload,(void))
IMP(int,catalog_CatalogPin,(unsigned,unsigned))
IMP(void,catalog_CatalogDrawIcon,(void *,unsigned,int,int,int))
IMP(int,SipShowIM,(U32))
IMP(int,SipGetInfo,(SipInfo *))
static const Render *r;static const Catalog *cat;
static int mode,offset,list_offset,jump,search,selected=-1,opened=-1,query_len,save_failed;
static int drag_y,drag_scroll,drag_mode,drag_valid;
static char query[48];
static int lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int match(const char *s){if(!query_len)return 1;
 for(unsigned j=0;s[j];++j){int i=0;while(i<query_len&&s[j+i]&&lower((unsigned char)s[j+i])==lower((unsigned char)query[i]))++i;if(i==query_len)return 1;}return 0;}
static int sip_on(void){SipInfo info;info.size=sizeof(info);return CALL(SipGetInfo,(&info))&&(info.flags&1u);}
static int bottom(void){return sip_on()?271:528;}
static int index_id(unsigned id){for(unsigned i=0;i<cat->count;++i)if(cat->apps[i].id==id)return (int)i;return -1;}
static int pin_size(unsigned id){for(unsigned i=0;i<cat->pin_count;++i)if(cat->pins[i].id==id)return cat->pins[i].size;return 0;}
static int home_end(void){int row=0;for(unsigned i=0;i<cat->pin_count;++i){int last=cat->pins[i].y+cat->pins[i].size;if(last>row)row=last;}return 28+row*52+28;}
static void icon(void *dc,unsigned i,int x,int y,int size){CALL(catalog_CatalogDrawIcon,(dc,i,x,y,size));}
static void arrow(void *dc,int x,int y,int back){int d=back?-1:1;r->line(dc,x-9*d,y,x+9*d,y,GRAY);
 r->line(dc,x+9*d,y,x+2*d,y-7,GRAY);r->line(dc,x+9*d,y,x+2*d,y+7,GRAY);}
static void home(void *dc){for(unsigned i=0;i<cat->pin_count;++i){const Pin *p=&cat->pins[i];int idx=index_id(p->id);if(idx<0)continue;
 const AppEntry *a=&cat->apps[idx];int x=6+p->x*52,y=28+p->y*52-offset,size=p->size*52-4;
 if(y+size<=24||y>=528)continue;r->clip(x,y<24?24:y,x+size,y+size>528?528:y+size);
 r->fill(dc,x,y,x+size,y+size,a->color);
 if((a->flags&1u)&&p->size==2){r->text(dc,x+7,y+9,"Latest news",WHITE,14);r->text(dc,x+7,y+32,"Hello from",WHITE,12);r->text(dc,x+7,y+49,"your phone",WHITE,12);}
 else {int s=p->size==1?27:36;icon(dc,(unsigned)idx,x+((size-s)>>1),y+((size-s)>>1)-(p->size==2?5:0),s);}
 if(p->size==2)r->text(dc,x+7,y+size-20,a->name,WHITE,12);
 if(selected==idx){r->fill(dc,x,y,x+size,y+2,WHITE);r->fill(dc,x,y,x+2,y+size,WHITE);}
 }
 r->clip(0,24,320,528);int y=home_end()-offset;r->text(dc,232,y-12,"All apps",WHITE,14);arrow(dc,306,y-4,0);
 if(!cat->count)r->text(dc,12,80,"No apps in Apps folder",GRAY,16);
}
static int rows(void *dc,int hx,int hy){int y=80-list_offset,last=0,hit=-1,found=0,limit=bottom();
 for(unsigned i=0;i<cat->count;++i){const AppEntry *a=&cat->apps[i];if(!match(a->name))continue;++found;int c=a->name[0];if(c>='a'&&c<='z')c-=32;
  if(c!=last){last=c;if(dc){char s[2]={(char)c,0};r->text(dc,6,y+4,s,GRAY,22);}if(hx>=0&&hy>=y&&hy<y+40)hit=-2;y+=40;}
  if(dc&&y+44>76&&y<limit){r->fill(dc,6,y,44,y+40,a->color);icon(dc,i,10,y+4,30);r->text(dc,57,y+8,a->name,WHITE,18);}
  if(hx>=0&&hy>=y&&hy<y+44)hit=(int)i;y+=44;
 }
 if(dc&&!found){r->text(dc,6,92,"No results",GRAY,14);r->text(dc,6,137,"Search Store",WHITE,14);}
 return hx<0?y+list_offset:hit;
}
static void list(void *dc){int b=bottom();r->clip(0,76,314,b);rows(dc,-1,-1);r->clip(0,24,320,b);
 r->fill(dc,6,29,314,61,search?BLUE:GRAY);r->fill(dc,8,31,312,59,search?WHITE:0);
 r->text(dc,17,36,query_len?query:(search?"":"Search"),search?0:GRAY,14);
 if(search){int caret=17+query_len*7;if(caret>280)caret=280;r->fill(dc,caret,36,caret+1,51,0);}
 r->line(dc,293,42,301,50,search?0:GRAY);r->line(dc,301,42,293,50,search?0:GRAY);
}
static int group_exists(int c){for(unsigned i=0;i<cat->count;++i)if(lower(cat->apps[i].name[0])==lower(c))return 1;return 0;}
static void jump_paint(void *dc){r->fill(dc,0,24,320,528,0);
 for(int i=0;i<28;++i){int x=48+(i%4)*64,y=54+div_(i,4)*64;char s[2]={i==0?'#':i==27?'@':(char)('A'+i-1),0};
  r->text(dc,x,y,s,group_exists(s[0])?WHITE:RGB_(53,53,53),28);}}
static void app_paint(void *dc){const AppEntry *a=&cat->apps[opened];arrow(dc,21,48,1);r->text(dc,45,38,a->name,WHITE,18);
 r->fill(dc,120,102,200,182,a->color);icon(dc,(unsigned)opened,137,119,46);
 if(a->flags&1u){r->text(dc,14,216,"Latest headlines",WHITE,22);r->text(dc,14,254,"Welcome to your phone.",GRAY,14);}
 else r->text(dc,14,224,"Ready when you are.",GRAY,18);
 r->fill(dc,14,352,306,392,RGB_(42,42,42));r->text(dc,27,363,pin_size(a->id)?"Unpin from Start":"Pin to Start",WHITE,14);
 r->fill(dc,14,404,306,444,RGB_(42,42,42));r->text(dc,27,415,pin_size(a->id)==1?"Tile size: small":"Tile size: medium",WHITE,14);
 if(save_failed)r->text(dc,14,470,"Could not save preferences",GRAY,14);
}
static void paint(void *dc){if(!r||!cat)return;r->clip(0,0,320,528);r->fill(dc,0,0,320,528,0);r->clip(0,24,320,528);
 if(opened>=0)app_paint(dc);else if(jump)jump_paint(dc);else if(mode)list(dc);else home(dc);r->clip(0,0,320,528);}
static void open_id(unsigned id){opened=index_id(id);search=jump=0;CALL(SipShowIM,(0));}
static void jump_to(int c){int y=0,last=0;for(unsigned i=0;i<cat->count;++i){int l=cat->apps[i].name[0];if(l>='a'&&l<='z')l-=32;
 if(last!=l){if(l>=c){list_offset=y;return;}last=l;y+=40;}y+=44;}}
static void scroll_by(int delta){if(!mode){offset=clampi(offset+delta,0,home_end()>512?home_end()-512:0);return;}
 int height=rows(0,-1,-1)-80,view=bottom()-80;list_offset=clampi(list_offset+delta,0,height>view?height-view:0);}
static int input(U32 msg,U32 wp,U32 lp){int x=(int)(short)(lp&65535u),y=(int)(short)(lp>>16);
 if(msg==WM_KEY_){if(wp==0x1bu||wp==0x25u){if(sip_on()){CALL(SipShowIM,(0));search=0;return 1;}
  if(opened>=0)opened=-1;else if(jump)jump=0;else {mode=search=query_len=0;query[0]=0;}return 1;}
  if(wp==0x27u){mode=1;selected=-1;return 1;}
  if(wp==0x26u||wp==0x28u){scroll_by(wp==0x26u?-52:52);return 1;}
  if(search&&wp==8){if(query_len)query[--query_len]=0;list_offset=0;return 1;}
  if(wp==13){if(search){CALL(SipShowIM,(0));search=0;}else if(selected>=0)open_id(cat->apps[selected].id);else mode=!mode;return 1;}}
 if((msg==WM_CHAR_||msg==WM_KEY_)&&search&&wp>=32&&wp<127&&query_len<47){query[query_len++]=(char)wp;query[query_len]=0;list_offset=0;return 1;}
 if(msg==WM_DOWN_){drag_y=y;drag_scroll=mode?list_offset:offset;drag_mode=mode;drag_valid=1;if(y<24)return 0;
  if(opened>=0){const AppEntry *a=&cat->apps[opened];if(y<80){opened=-1;return 1;}
   if(y>=352&&y<392)save_failed=!CALL(catalog_CatalogPin,(a->id,pin_size(a->id)?0u:2u));
   if(y>=404&&y<444)save_failed=!CALL(catalog_CatalogPin,(a->id,pin_size(a->id)==1?2u:1u));return 1;}
  if(jump){if(x>=32&&x<288&&y>=54&&y<502){int i=div_(y-54,64)*4+div_(x-32,64);if(i>=1&&i<=26&&group_exists('A'+i-1))jump_to('A'+i-1);}
   jump=0;return 1;}
  if(mode){if(y<65){if(x>286&&query_len){query_len=0;query[0]=0;list_offset=0;}search=1;CALL(SipShowIM,(1));return 1;}
   if(y>=bottom())return 0;int i=rows(0,x,y);if(i==-2){jump=1;search=0;CALL(SipShowIM,(0));return 1;}
   if(i>=0){if(selected==i)open_id(cat->apps[i].id);else selected=i;return 1;}}
  else {if(y+offset>=home_end()-24){mode=1;selected=-1;return 1;}
   for(unsigned i=0;i<cat->pin_count;++i){const Pin *p=&cat->pins[i];int xx=6+p->x*52,yy=28+p->y*52,sz=p->size*52-4;
    if(x>=xx&&x<xx+sz&&y+offset>=yy&&y+offset<yy+sz){int idx=index_id(p->id);if(selected==idx)open_id(p->id);else selected=idx;return 1;}}}}
 if(msg==WM_MOVE_&&drag_valid&&opened<0&&!jump&&drag_mode==mode){int target=drag_scroll+drag_y-y;
  scroll_by(target-(mode?list_offset:offset));selected=-1;return 1;}return 0;}
void StartMenuInitialize(void){r=CALL(render_SpGetThreadContext,());CALL(catalog_CatalogReload,());cat=CALL(catalog_CatalogGet,());
 mode=offset=list_offset=jump=search=query_len=save_failed=drag_valid=0;selected=opened=-1;query[0]=0;CALL(SipShowIM,(0));}
void StartMenuUninitialize(void){CALL(SipShowIM,(0));r=0;cat=0;}
static int page(void){return mode;}
static int scroll(void){return mode?list_offset:offset;}
static const Menu menu={PROFILE,paint,input,page,scroll,open_id};
const Menu *UIXGetSchemaFactory(void){return &menu;}
void UIXSchemaUnloaded(void){}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
