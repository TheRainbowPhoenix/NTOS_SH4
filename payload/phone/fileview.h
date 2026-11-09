/* Small read-only WinCE directory browser, no heap or per-entry metadata opens. */
typedef struct {char name[40];U32 bytes;unsigned char folder;} FileItem;
static FileItem file_items[48];static unsigned file_count;static char file_path[96];
static int file_offset,file_selected=-1,file_drag,file_start,file_tracking,file_moved;
static void files_scan(void){file_count=0;file_offset=0;file_selected=-1;
 char pattern[96];copy_(pattern,file_path,sizeof(pattern));unsigned n=len_(pattern);copy_(pattern+n,"\\*",sizeof(pattern)-n);
 W16 w[96];wide_(w,pattern);FindData data;void *f=CALL(FindFirstFileW,(w,&data));if(f==(void *)(intptr_t)-1)return;
 do {if(data.name[0]=='.')continue;FileItem *a=&file_items[file_count];unsigned i=0;
  while(i<39&&data.name[i]&&data.name[i]<128){a->name[i]=(char)data.name[i];++i;}a->name[i]=0;
  if(!i||data.name[i])continue;a->folder=(data.attrs&16u)!=0;a->bytes=data.size_lo;++file_count;
 }while(file_count<48&&CALL(FindNextFileW,(f,&data)));CALL(FindClose,(f));
 for(unsigned i=1;i<file_count;++i){unsigned j=i;while(j){FileItem *a=&file_items[j-1],*b=&file_items[j];int order=0;
  if(a->folder!=b->folder)order=a->folder? -1:1;else {unsigned k=0;while(a->name[k]&&lower(a->name[k])==lower(b->name[k]))++k;order=lower(a->name[k])-lower(b->name[k]);}
  if(order<=0)break;unsigned char *aa=(unsigned char *)a,*bb=(unsigned char *)b;for(unsigned k=0;k<sizeof(*a);++k){unsigned char tmp=aa[k];aa[k]=bb[k];bb[k]=tmp;}--j;}}
}
static void files_reset(void){copy_(file_path,"\\fls0",sizeof(file_path));file_tracking=0;files_scan();}
static int files_back(void){unsigned n=len_(file_path);if(n<=5)return -1;while(n&&file_path[n-1]!='\\')--n;if(n)file_path[n-1]=0;files_scan();return 1;}
static int files_max(void){int n=(int)file_count*58-(view_bottom-150);return n>0?n:0;}
static void files_open(void){if(file_selected<0||file_selected>=(int)file_count)return;
 const FileItem *a=&file_items[file_selected];unsigned n=len_(file_path),m=len_(a->name);if(n+m+2>=sizeof(file_path))return;
 char path[96];copy_(path,file_path,sizeof(path));path[n++]='\\';copy_(path+n,a->name,sizeof(path)-n);
 if(a->folder){copy_(file_path,path,sizeof(file_path));files_scan();return;}
 if(m>=4&&lower(a->name[m-4])=='.'&&lower(a->name[m-3])=='e'&&lower(a->name[m-2])=='x'&&lower(a->name[m-1])=='e')
  if(CALL(coredll_launch,(path)))CALL(PostQuitMessage,(0));
}
static void files_paint(void *dc){r->fill(dc,0,24,320,72,RGB_(30,30,30));r->text(dc,14,40,"<",WHITE,18);
 r->text(dc,49,42,len_(file_path)<=5?"This Device":file_path+6,WHITE,14);r->text(dc,292,40,"^",WHITE,18);
 r->text(dc,12,88,"Sort by: Name",GRAY,14);r->clip(0,116,320,view_bottom-30);
 for(unsigned i=0;i<file_count;++i){int y=122+(int)i*58-file_offset;if(y+56<116||y>=view_bottom-30)continue;const FileItem *a=&file_items[i];
  if((int)i==file_selected)r->fill(dc,0,y,320,y+56,RGB_(35,35,35));
  if(a->folder){r->fill(dc,20,y+7,48,y+43,RGB_(255,221,95));r->fill(dc,20,y+2,37,y+10,RGB_(255,235,146));}
  else {r->fill(dc,22,y+6,46,y+42,GRAY);r->fill(dc,24,y+8,44,y+40,RGB_(42,42,42));}
  r->text(dc,65,y+9,a->name,WHITE,16);r->text(dc,65,y+32,a->folder?"Folder":"File",GRAY,12);}
 r->clip(0,24,320,528);r->fill(dc,0,view_bottom-28,320,view_bottom,RGB_(24,24,24));r->text(dc,12,view_bottom-21,"Clear: back     EXE: open",GRAY,12);
}
static int files_input(U32 msg,U32 wp,int x,int y){
 if(msg==WM_KEY_){if(wp==0x1b||wp==0x25)return files_back();if(wp==13){files_open();return 1;}
  if(wp==0x26||wp==0x28){file_selected=clampi(file_selected+(wp==0x28?1:-1),0,(int)file_count-1);
   int top=file_selected*58;if(top<file_offset)file_offset=top;if(top+58-file_offset>view_bottom-150)file_offset=top+58-(view_bottom-150);return 1;}}
 if(msg==WM_DOWN_){if(file_tracking){if(y-file_drag>8||file_drag-y>8){file_moved=1;file_offset=clampi(file_start+file_drag-y,0,files_max());}return 1;}
  file_tracking=1;file_moved=0;file_drag=y;file_start=file_offset;return 1;}
 if(msg==WM_MOVE_&&file_tracking){if(y-file_drag>8||file_drag-y>8)file_moved=1;if(file_moved)file_offset=clampi(file_start+file_drag-y,0,files_max());return 1;}
 if(msg==WM_UP_){int tap=file_tracking&&!file_moved&&y-file_drag<9&&file_drag-y<9;file_tracking=0;if(!tap)return 1;
  if(y>=24&&y<72&&(x<44||x>278))return files_back();
  if(y>=122&&y<view_bottom-30){int i=div_(y-122+file_offset,58);if(i<(int)file_count){file_selected=i;files_open();}}return 1;}return 0;
}
