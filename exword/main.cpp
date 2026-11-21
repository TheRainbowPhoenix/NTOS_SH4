/* NTOSH EX-word: compact landscape launcher and SH4BE PE picker.
 * ACON exits immediately; EXIT goes up one folder or exits at root.
 * EXE enters directories or runs a PE from the selected native disk.
 */
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdint.h>
#include "storage.h"
#include "pe_runner.hpp"
#include "../src/nt/process.hpp"

static nt::Process g_kernel;
static ntos_exword_listing g_listing;
static unsigned g_volume,g_selected,g_top,g_skip,g_more;
static char g_dir[NTOS_EXWORD_PATH_MAX];
static char g_note[72];
static bool g_ready;

alignas(8) static unsigned char g_boot_scratch[64];
static bool g_boot_busy;
static void *pool_alloc(void *,size_t n) {
    if(!n || n>sizeof(g_boot_scratch) || g_boot_busy)return nullptr;
    g_boot_busy=true;return g_boot_scratch;
}
static void pool_free(void *,void *p) {
    if(p==g_boot_scratch)g_boot_busy=false;
}
static bool init_kernel() {
    if(!g_kernel.begin())return false;
    if(g_kernel.ke.begin(0x70000080u,0x20000u)!=nt::SUCCESS)return false;
    uint32_t handle=0u;
    if(g_kernel.create_event(true,false,&handle)!=nt::SUCCESS)return false;
    if(g_kernel.event_set(handle)!=nt::SUCCESS ||
       g_kernel.event_poll(handle,0u)!=nt::SUCCESS ||
       g_kernel.handles.close(handle,nt::Type::event)!=nt::SUCCESS)
        return false;
    g_kernel.pool.configure(nullptr,pool_alloc,pool_free);
    const uint32_t tag=nt::tag('E','X','W','D');
    void *scratch=g_kernel.pool.alloc(32u,tag);
    return scratch && g_kernel.pool.free(scratch,tag)==nt::SUCCESS;
}
static unsigned length(const char *s) {
    unsigned i=0u;while(s && s[i])++i;return i;
}
static void copy(char *to,unsigned cap,const char *from) {
    if(!cap)return;
    unsigned i=0u;
    for(;i+1u<cap && from && from[i];++i)to[i]=from[i];
    to[i]=0;
}
static bool append(char *to,unsigned cap,const char *from) {
    unsigned n=length(to),i=0u;
    while(from && from[i] && n+1u<cap)to[n++]=from[i++];
    to[n]=0;
    return !from || from[i]==0;
}
static void hex8(char out[9],uint32_t value) {
    const char *digits="0123456789ABCDEF";
    for(unsigned i=0;i<8;i++)out[i]=digits[(value>>(28u-4u*i))&15u];
    out[8]=0;
}
static int compare(const char *a,const char *b) {
    for(;*a&&*b;++a,++b) {
        char x=*a,y=*b;
        if(x>='a'&&x<='z')x=(char)(x-'a'+'A');
        if(y>='a'&&y<='z')y=(char)(y-'a'+'A');
        if(x!=y)return x<y?-1:1;
    }
    return *a==*b?0:*a?1:-1;
}
static void sort_list() {
    for(unsigned i=0u;i<g_listing.count;++i)
        for(unsigned j=i+1u;j<g_listing.count;++j) {
            bool adir=g_listing.kinds[i]==5u,bdir=g_listing.kinds[j]==5u;
            if((!adir&&bdir)||(adir==bdir&&compare(g_listing.names[i],g_listing.names[j])>0)){
                char tmp[NTOS_EXWORD_NAME_MAX];
                copy(tmp,sizeof tmp,g_listing.names[i]);
                copy(g_listing.names[i],sizeof g_listing.names[i],g_listing.names[j]);
                copy(g_listing.names[j],sizeof g_listing.names[j],tmp);
                uint32_t k=g_listing.kinds[i];
                g_listing.kinds[i]=g_listing.kinds[j];
                g_listing.kinds[j]=k;
            }
        }
}
static void root() {
    copy(g_dir,sizeof g_dir,g_volume==0u?"\\\\drv0":"\\\\crd0");
    g_selected=g_top=g_skip=0u;g_more=0u;
}
static void parent() {
    unsigned n=length(g_dir);
    while(n>2u && g_dir[n-1u]!='\\')--n;
    if(n>2u && g_dir[n-1u]=='\\')g_dir[n-1u]=0;
    if(length(g_dir)<7u)root();
    g_selected=g_top=g_skip=0u;g_more=0u;
}
static bool enter(const char *name) {
    unsigned len=length(g_dir);
    if(len<6u || len+length(name)+2u>=sizeof g_dir)return false;
    if(!append(g_dir,sizeof g_dir,"\\") ||
       !append(g_dir,sizeof g_dir,name))return false;
    g_selected=g_top=g_skip=0u;g_more=0u;
    return true;
}
static bool is_exe(const char *s) {
    unsigned n=length(s);
    return n>=4u && compare(s+n-4u,".EXE")==0;
}
static void scan() {
    char mask[NTOS_EXWORD_PATH_MAX];
    copy(mask,sizeof mask,g_dir);
    if(!append(mask,sizeof mask,"\\*")) {
        copy(g_note,sizeof g_note,"path too long");g_listing.count=0u;return;
    }
    int rc=ntos_exword_scan_page(mask,g_skip,&g_listing,&g_more);
    if(rc<0) {
        char hex[9];hex8(hex,(uint32_t)rc);
        copy(g_note,sizeof g_note,"FS error 0x");
        append(g_note,sizeof g_note,hex);
    } else {
        sort_list();
        copy(g_note,sizeof g_note,"EXE run  |  EXIT parent  |  ACON quit");
    }
    if(g_selected>=g_listing.count)g_selected=0u;
    g_top=0u;
}
static void draw() {
    dclear(C_WHITE);
    drect(0,0,DWIDTH-1,28,0x2945);
    dtext(8,8,C_WHITE,"NTOSH / SH4BE PE");
    dtext(9,40,C_BLACK,g_dir);
    dtext(9,58,C_BLACK,g_ready?"UP/DOWN select  EXE open/run  LEFT/RIGHT disk" :
                                       "Kernel initialization failed");
    drect(5,79,DWIDTH-6,260,0xef7d);
    if(!g_listing.count) {
        dtext(13,94,C_BLACK,"(empty or unavailable)");
    } else {
        for(unsigned line=0;line<9u && g_top+line<g_listing.count;++line){
            unsigned id=g_top+line;
            int y=86+(int)line*19;
            if(id==g_selected)drect(8,y-2,DWIDTH-9,y+17,0x2945);
            char row[NTOS_EXWORD_NAME_MAX+8];
            copy(row,sizeof row,g_listing.kinds[id]==5u?"[DIR] ":"      ");
            append(row,sizeof row,g_listing.names[id]);
            dtext(14,y,id==g_selected?C_WHITE:C_BLACK,row);
        }
    }
    dtext(9,275,C_BLACK,g_note);
    drect(0,298,DWIDTH-1,DHEIGHT-1,0x2945);
    dtext(9,304,C_WHITE,"ACON: exit app   |   EXIT: back / quit");
    dupdate();
}
static void run_selected() {
    if(g_selected>=g_listing.count)return;
    char name[NTOS_EXWORD_NAME_MAX];
    copy(name,sizeof name,g_listing.names[g_selected]);
    if(g_listing.kinds[g_selected]==5u) {
        if(!enter(name))copy(g_note,sizeof g_note,"directory path too long");
        else scan();
        return;
    }
    if(!is_exe(name)) {
        copy(g_note,sizeof g_note,"Not an EXE");return;
    }
    char path[NTOS_EXWORD_PATH_MAX];
    copy(path,sizeof path,g_dir);
    if(!append(path,sizeof path,"\\") ||
       !append(path,sizeof path,name)) {
        copy(g_note,sizeof g_note,"EXE path too long");return;
    }
    dclear(C_WHITE);
    dtext(9,8,C_BLACK,"Loading SH4BE PE...");
    dtext(9,28,C_BLACK,name);
    dupdate();
    uint32_t result=0;
    int rc=ntos_exword_run_pe(path,&result);
    if(rc<0)copy(g_note,sizeof g_note,ntos_exword_pe_error());
    else {
        char value[9];hex8(value,result);
        copy(g_note,sizeof g_note,"EXE returned 0x");
        append(g_note,sizeof g_note,value);
    }
    scan();
    /* Preserve the result (scan only reset its descriptive status). */
    if(rc<0)copy(g_note,sizeof g_note,ntos_exword_pe_error());
    else {
        char value[9];hex8(value,result);
        copy(g_note,sizeof g_note,"EXE returned 0x");
        append(g_note,sizeof g_note,value);
    }
}
int main(void)
{
    g_ready=init_kernel();
    root();
    if(g_ready)scan();
    else copy(g_note,sizeof g_note,"Ke/Ob/Ex failed");
    draw();
    for(;;) {
        key_event_t e=getkey();
        if(e.type!=KEYEV_DOWN && e.type!=KEYEV_HOLD)continue;
        if(e.key==KEY_ACON)break;
        if(e.key==KEY_EXIT) {
            if(length(g_dir)>6u){parent();scan();draw();continue;}
            break;
        }
        if(e.type!=KEYEV_DOWN || !g_ready)continue;
        if(e.key==KEY_LEFT || e.key==KEY_RIGHT) {
            g_volume^=1u;root();scan();
        } else if(e.key==KEY_UP) {
            if(g_selected>0u)--g_selected;
            else if(g_skip>=NTOS_EXWORD_LIST_MAX) {
                g_skip-=NTOS_EXWORD_LIST_MAX;
                scan();
                if(g_listing.count)g_selected=g_listing.count-1u;
            }
        } else if(e.key==KEY_DOWN) {
            if(g_selected+1u<g_listing.count)++g_selected;
            else if(g_more && g_listing.count) {
                g_skip+=g_listing.count;
                scan();
                g_selected=0u;
            }
        } else if(e.key==KEY_EXE) {
            run_selected();
        } else continue;
        if(g_selected<g_top)g_top=g_selected;
        if(g_selected>=g_top+9u)g_top=g_selected-8u;
        draw();
    }
    /* Main-return goes through gint's native state restoration. */
    g_kernel.finish();
    return 0;
}
