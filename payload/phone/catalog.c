#include "phone.h"
IMP(int,svf_SVFValidate,(const void *,unsigned))
IMP(int,svf_SVFDraw,(void *,const void *,unsigned,int,int,int,int))
IMP(const Render *,render_SpGetThreadContext,(void))
#include "phone_io.h"
static Catalog cat;
static unsigned char work[4608];
static const char root[]="\\fls0\\Apps";
static int suffix(const char *s,const char *ext){unsigned a=len_(s),b=len_(ext);if(a<b)return 0;
 for(unsigned i=0;i<b;++i){char c=s[a-b+i];if(c>='A'&&c<='Z')c+=32;if(c!=ext[i])return 0;}return 1;}
static int safe(const W16 *s){unsigned i=0;for(;s[i];++i)if(i>=40||s[i]>126||s[i]<32||s[i]=='\\'||s[i]=='/'||s[i]==':')return 0;
 return i&&s[0]!='.';}
static void join(char *out,const char *a,const char *b){copy_(out,a,96);unsigned n=len_(out);if(n+1<96)out[n++]='\\';copy_(out+n,b,96-n);}
static int read_at(void *f,U32 offset,void *dest,U32 n){if(n>4608||offset>0x7fffffffu)return 0;
 if(CALL(SetFilePointer,(f,(int)offset,0,0))!=offset)return 0;
 U32 done=0;while(done<n){U32 got=0,step=n-done;if(step>4096)step=4096;
  if(!CALL(ReadFile,(f,(unsigned char *)dest+done,step,&got,0))||got!=step)return 0;done+=got;}return 1;}
typedef struct {U32 va,size,raw,bytes;} Section;
static U32 map_rva(U32 va,const Section *s,unsigned count){for(unsigned i=0;i<count;++i)
 if(va>=s[i].va&&va-s[i].va<s[i].bytes)return s[i].raw+va-s[i].va;return 0;}
static int resource(void *f,U32 base,U32 size,U32 type,U32 *rva,U32 *bytes){
 unsigned char h[24];U32 at=0;
 for(unsigned level=0;level<3;++level){if(at>size||size-at<16||!read_at(f,base+at,h,16))return 0;
  unsigned named=(unsigned)h[12]|((unsigned)h[13]<<8),ids=(unsigned)h[14]|((unsigned)h[15]<<8);
  unsigned count=named+ids;if(count>64)return 0;int found=0;
  for(unsigned i=0;i<count;++i){U32 entry=at+16+8*i;if(entry>size||size-entry<8||!read_at(f,base+entry,h,8))return 0;
   U32 id=le32_(h),value=le32_(h+4);
   if((level==0&&id!=type)||(level>0&&(id&0x80000000u)))continue;
   at=value&0x7fffffffu;found=1;if(level<2&&!(value&0x80000000u))return 0;
   if(level==2&&(value&0x80000000u))return 0;break;}
  if(!found)return 0;
 }
 if(at>size||size-at<16||!read_at(f,base+at,h,16))return 0;*rva=le32_(h);*bytes=le32_(h+4);return 1;
}
static void pe_icon(AppEntry *app){void *f=file_(app->path,0);if(f==(void *)(intptr_t)-1)return;
 unsigned char h[256];Section sec[16];U32 size=CALL(GetFileSize,(f,0));
 if(!read_at(f,0,h,64)||h[0]!='M'||h[1]!='Z')goto done;
 U32 pe=le32_(h+60);if(pe>size||size-pe<248||!read_at(f,pe,h,248))goto done;
 if(le32_(h)!=0x4550||h[24]!=0x0b||h[25]!=1)goto done;
 unsigned count=h[6]|((unsigned)h[7]<<8),opt=h[20]|((unsigned)h[21]<<8);
 U32 rva=le32_(h+24+112),rsz=le32_(h+24+116);if(!rva||count>16||opt<120)goto done;
 for(unsigned i=0;i<count;++i){if(!read_at(f,pe+24+opt+i*40,h,40))goto done;
  sec[i].size=le32_(h+8);sec[i].va=le32_(h+12);sec[i].bytes=le32_(h+16);sec[i].raw=le32_(h+20);}
 U32 base=map_rva(rva,sec,count),value,bytes;if(!base||base>size||rsz>size-base)goto done;
 if(resource(f,base,rsz,10,&value,&bytes)&&bytes<=ICON_BYTES){U32 raw=map_rva(value,sec,count);
  if(raw&&raw<=size&&bytes<=size-raw&&read_at(f,raw,app->icon,bytes)&&CALL(svf_SVFValidate,(app->icon,bytes))){app->icon_len=(unsigned short)bytes;goto done;}}
 if(resource(f,base,rsz,3,&value,&bytes)&&bytes<=sizeof(work)){U32 raw=map_rva(value,sec,count);
  if(raw&&raw<=size&&bytes<=size-raw){app->pe_icon_offset=raw;app->pe_icon_size=bytes;app->pe_icon_type=1;}}
done:CALL(CloseHandle,(f));
}
static void add(const char *path,const char *file){if(cat.count>=MAX_APPS)return;
 AppEntry *a=&cat.apps[cat.count];unsigned char *z=(unsigned char *)a;
 for(unsigned i=0;i<sizeof(*a);++i)z[i]=0;
 copy_(a->path,path,sizeof(a->path));copy_(a->name,file,sizeof(a->name));unsigned n=len_(a->name);if(n>=4)a->name[n-4]=0;
 U32 hash=2166136261u;for(unsigned i=0;path[i];++i)hash=(hash^(unsigned char)path[i])*16777619u;
 a->id=(unsigned short)(0x8000u|(hash&0x7fffu));a->color=BLUE;a->size=2;
 char meta[96];copy_(meta,path,sizeof(meta));n=len_(meta);copy_(meta+n,".manifest",sizeof(meta)-n);
 int got=load_(meta,work,1024);
 if(got>=16&&work[0]=='P'&&work[1]=='A'&&work[2]=='M'&&work[3]=='1'){
  unsigned name=work[12],icon=be16_(work+14);
  if(name>0&&name<sizeof(a->name)&&icon<=ICON_BYTES&&16u+name+icon==(unsigned)got&&
     (icon==0||CALL(svf_SVFValidate,(work+16+name,icon)))){
   a->id=(unsigned short)be16_(work+4);a->flags=work[6];a->size=work[7]==1?1:2;
   a->color=((U32)work[8]<<24)|((U32)work[9]<<16)|((U32)work[10]<<8)|work[11];
   for(unsigned i=0;i<name;++i)a->name[i]=(char)work[16+i];a->name[name]=0;
   a->icon_len=(unsigned short)icon;for(unsigned i=0;i<icon;++i)a->icon[i]=work[16+name+i];
  }
 }
 if(!a->icon_len)pe_icon(a);
 for(unsigned i=0;i<cat.count;++i)if(cat.apps[i].id==a->id)return;
 ++cat.count;
}
static void scan_dir(const char *folder,unsigned depth){if(depth>2||cat.count>=MAX_APPS)return;
 char pattern[96];join(pattern,folder,"*");W16 w[96];wide_(w,pattern);FindData d;
 void *find=CALL(FindFirstFileW,(w,&d));if(find==(void *)(intptr_t)-1)return;
 unsigned visited=0;
 do {if(++visited>128)break;if(!safe(d.name))continue;char name[48];unsigned i=0;
  while(d.name[i]&&i+1<sizeof(name)){name[i]=(char)d.name[i];++i;}name[i]=0;
  char path[96];join(path,folder,name);if(len_(folder)+len_(name)+2>=sizeof(path))continue;
  if(d.attrs&16u)scan_dir(path,depth+1);else if(suffix(name,".exe"))add(path,name);
 }while(cat.count<MAX_APPS&&CALL(FindNextFileW,(find,&d)));
 CALL(FindClose,(find));
}
static int compare(const char *a,const char *b){while(*a&&*b){int x=*a,y=*b;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return x-y;++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
static AppEntry *byid(unsigned id){for(unsigned i=0;i<cat.count;++i)if(cat.apps[i].id==id)return &cat.apps[i];return 0;}
static int pin_save(void){unsigned char bytes[8+MAX_APPS*8];bytes[0]='P';bytes[1]='I';bytes[2]='N';bytes[3]='1';put16_(bytes+4,cat.pin_count);bytes[6]=bytes[7]=0;
 for(unsigned i=0;i<cat.pin_count;++i){unsigned char *p=bytes+8+i*8;const Pin *pin=&cat.pins[i];put16_(p,pin->id);p[2]=pin->size;p[3]=pin->x;p[4]=pin->y;p[5]=p[6]=p[7]=0;}
 return save_("\\fls0\\home.dat",bytes,8+cat.pin_count*8);}
static void pack_pins(void){unsigned char used[64][6];for(unsigned y=0;y<64;++y)for(unsigned x=0;x<6;++x)used[y][x]=0;
 for(unsigned i=0;i<cat.pin_count;++i){Pin *p=&cat.pins[i];unsigned s=p->size;int placed=0;
  for(unsigned y=0;y+s<=64&&!placed;++y)for(unsigned x=0;x+s<=6;++x){int fits=1;
   for(unsigned yy=0;yy<s;++yy)for(unsigned xx=0;xx<s;++xx)if(used[y+yy][x+xx])fits=0;
   if(!fits)continue;p->x=(unsigned char)x;p->y=(unsigned char)y;
   for(unsigned yy=0;yy<s;++yy)for(unsigned xx=0;xx<s;++xx)used[y+yy][x+xx]=1;placed=1;break;}}
}
const Catalog *CatalogGet(void){return &cat;}
int CatalogReload(void){cat.count=cat.pin_count=0;scan_dir(root,0);
 for(unsigned i=1;i<cat.count;++i){unsigned j=i;while(j&&compare(cat.apps[j-1].name,cat.apps[j].name)>0){
   AppEntry tmp=cat.apps[j-1];cat.apps[j-1]=cat.apps[j];cat.apps[j]=tmp;--j;}}
 int n=load_("\\fls0\\home.dat",work,1024);
 if(n>=8&&work[0]=='P'&&work[1]=='I'&&work[2]=='N'&&work[3]=='1'){
  unsigned count=be16_(work+4);if(count<=MAX_APPS&&8+count*8==(unsigned)n)for(unsigned i=0;i<count;++i){
   unsigned id=be16_(work+8+i*8),size=work[10+i*8];int duplicate=0;
   for(unsigned j=0;j<cat.pin_count;++j)if(cat.pins[j].id==id)duplicate=1;
   if(!duplicate&&byid(id)&&(size==1||size==2)){Pin *p=&cat.pins[cat.pin_count++];p->id=(unsigned short)id;p->size=(unsigned char)size;}}}
 else {for(unsigned i=0;i<cat.count&&cat.pin_count<16;++i){Pin *p=&cat.pins[cat.pin_count++];p->id=cat.apps[i].id;p->size=cat.apps[i].size;}}
 pack_pins();return (int)cat.count;}
int CatalogPin(unsigned id,unsigned size){if(!byid(id)||size>2)return 0;
 unsigned i=0;while(i<cat.pin_count&&cat.pins[i].id!=id)++i;
 if(size==0){if(i==cat.pin_count)return 1;for(unsigned j=i+1;j<cat.pin_count;++j)cat.pins[j-1]=cat.pins[j];--cat.pin_count;}
 else {if(i==cat.pin_count){if(i>=MAX_APPS)return 0;++cat.pin_count;}cat.pins[i].id=(unsigned short)id;cat.pins[i].size=(unsigned char)size;}
 pack_pins();return pin_save();}
void CatalogDrawIcon(void *dc,unsigned index,int x,int y,int size){if(index>=cat.count)return;
 AppEntry *a=&cat.apps[index];if(a->icon_len&&CALL(svf_SVFDraw,(dc,a->icon,a->icon_len,x,y,size,size)))return;
 const Render *r=CALL(render_SpGetThreadContext,());
 if(a->pe_icon_type){void *f=file_(a->path,0);int ok=f!=(void *)(intptr_t)-1&&read_at(f,a->pe_icon_offset,work,a->pe_icon_size);
  if(f!=(void *)(intptr_t)-1)CALL(CloseHandle,(f));
  if(ok&&a->pe_icon_size>=40&&le32_(work)==40){unsigned w=le32_(work+4),h=le32_(work+8)>>1;
   if(w&&h&&w<=32&&h<=32&&work[14]==32&&work[15]==0&&le32_(work+16)==0&&40+w*h*4<=a->pe_icon_size){
    for(int yy=0;yy<size;++yy)for(int xx=0;xx<size;++xx){unsigned px=udiv_((unsigned)xx*w,(unsigned)size),py=udiv_((unsigned)yy*h,(unsigned)size);
     const unsigned char *p=work+40+((h-1-py)*w+px)*4;
     if(p[3]||p[0]||p[1]||p[2])r->fill(dc,x+xx,y+yy,x+xx+1,y+yy+1,RGB_(p[2],p[1],p[0]));}return;}}}
 char initial[2]={a->name[0],0};r->text(dc,x+size/5,y,initial,WHITE,size>=30?28:18);
}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
