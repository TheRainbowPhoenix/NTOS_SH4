/* Native EX-word SH4BE PE loader. Reuses NTOS PE32 parser, streamed
 * headers, byte-order-aware relocations, and the real COREDLL source.
 *
 * Images execute only in Edelweiss's firmware-fingerprinted P1 arenas.
 * The OS and gint retain ownership of MMUCR/TLB/VBR and of filesystem
 * calls. Missing DLLs/exports, missing .reloc, oversized images and unknown
 * ROMs fail closed rather than patching arbitrary firmware mappings.
 */
#include "pe_runner.hpp"
#include "storage.h"
#include "profile.hpp"
#include "../src/pe.hpp"
#include "../src/pe_rebase.hpp"
#include "../src/pe_stream.hpp"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdint.h>
#include <stddef.h>

extern "C" {
    uint32_t ntos_exword_lookup_coredll(const char *);
    int ntos_exword_dll_main(void *,uint32_t,void *);
    int ntos_exword_invoke(uint32_t,const void *const *,uint32_t);
    int ntos_exword_invoke_dll(uint32_t,uint32_t,uint32_t,uint32_t);
    /* coredll.c (NTOS_CORE_HOST_TEST) uses the same first 28 NtosApi
     * gates as the original Hollyhock loader. */
    const void *ntos_host_table[28]={};
}

static const char *g_error="not started";
static uint8_t *g_heap_ptr;
static uint32_t g_heap_base,g_heap_end;
static unsigned g_print_line;
struct Allocation {uint32_t addr,bytes;bool live;};
static Allocation g_alloc[32];

static void set_error(const char *s) {g_error=s;}
const char *ntos_exword_pe_error(void) {return g_error;}
static bool same_case(const char *a,const char *b) {
    if(!a || !b)return false;
    for(;;++a,++b) {
        char x=*a,y=*b;
        if(x>='a'&&x<='z')x=(char)(x-'a'+'A');
        if(y>='a'&&y<='z')y=(char)(y-'a'+'A');
        if(x!=y)return false;
        if(!x)return true;
    }
}
static void *heap_alloc(uint32_t size)
{
    if(!size || size>g_heap_end-g_heap_base)return nullptr;
    size=(size+7u)&~7u;
    if(!size)return nullptr;
    /* Reuse exact freed blocks; otherwise allocate monotonically.
     * A process gets a fresh P1 arena on each launch. */
    for(Allocation &a:g_alloc)
        if(!a.live && a.addr && a.bytes>=size) {
            a.live=true;
            return (void *)(uintptr_t)a.addr;
        }
    if(g_heap_base>g_heap_end || size>g_heap_end-g_heap_base)return nullptr;
    for(Allocation &a:g_alloc)if(!a.addr) {
        a={g_heap_base,size,true};
        uint32_t allocated=g_heap_base;
        g_heap_base+=size;
        return (void *)(uintptr_t)allocated;
    }
    return nullptr;
}
static void heap_free(void *p)
{
    const uint32_t addr=(uint32_t)(uintptr_t)p;
    for(Allocation &a:g_alloc)if(a.addr==addr && a.live) {
        a.live=false;
        return;
    }
}
static void host_puts(const char *str)
{
    if(!str)return;
    if(g_print_line>=12u)g_print_line=0u;
    const int y=29+(int)g_print_line*19;
    drect(5,y,DWIDTH-6,y+17,C_WHITE);
    char msg[70];
    unsigned i=0u;
    while(i+1u<sizeof(msg) && str[i] && str[i]!='\n'){
        msg[i]=str[i];++i;
    }
    msg[i]=0;
    dtext(7,y,C_BLACK,msg);
    ++g_print_line;
    dupdate();
}
static void host_refresh(void) {dupdate();}
static void host_clear(uint32_t c) {dclear((int)(c&0xffffu));}
static void host_pixel(uint32_t x,uint32_t y,uint32_t c) {
    if(x>=DWIDTH || y>=DHEIGHT)return;
    uint16_t *vram=nullptr;
    dgetvram(&vram,nullptr);
    if(vram)vram[y*DWIDTH+x]=(uint16_t)c;
}
static void host_line(uint32_t x1,uint32_t y1,uint32_t x2,uint32_t y2,uint32_t c)
{
    /* Endpoints outside native display are clipped by the pixel gate. */
    int x=(int)x1,y=(int)y1,dx=(int)x2-(int)x1,dy=(int)y2-(int)y1;
    if(dx>2048 || dx< -2048 || dy>2048 || dy< -2048)return;
    int sx=dx<0?-1:1,sy=dy<0?-1:1;
    if(dx<0)dx=-dx;
    if(dy<0)dy=-dy;
    int err=dx-dy;
    for(unsigned n=0;n<4097u;++n){
        if(x>=0 && y>=0)host_pixel((uint32_t)x,(uint32_t)y,c);
        if(x==(int)x2 && y==(int)y2)break;
        int e2=err*2;
        if(e2> -dy){err-=dy;x+=sx;}
        if(e2<dx){err+=dx;y+=sy;}
    }
}
static void host_fillrect(uint32_t x,uint32_t y,uint32_t right,uint32_t bottom,uint32_t c)
{
    if(x>=DWIDTH || y>=DHEIGHT || right<=x || bottom<=y)return;
    if(right>DWIDTH)right=DWIDTH;
    if(bottom>DHEIGHT)bottom=DHEIGHT;
    drect((int)x,(int)y,(int)right-1,(int)bottom-1,(int)(c&0xffffu));
}
static uint32_t host_getkey(void)
{
    for(;;) {
        key_event_t e=getkey();
        if(e.type==KEYEV_DOWN)return (uint32_t)e.key;
    }
}
static uint32_t host_input(uint32_t *x,uint32_t *y)
{
    for(;;) {
        key_event_t e=getkey();
        if(e.type==KEYEV_TOUCH_DOWN) {
            if(x)*x=(uint32_t)e.x;
            if(y)*y=(uint32_t)e.y;
            return 1u;
        }
        if(e.type==KEYEV_DOWN &&
           (e.key==KEY_EXIT || e.key==KEY_ACON))return 2u;
    }
}
static void host_draw_text(uint32_t x,uint32_t y,const char *s)
{
    if(x<DWIDTH && y<DHEIGHT && s)dtext((int)x,(int)y,C_BLACK,s);
}
static void host_free(void *p){heap_free(p);}
static void *host_malloc(uint32_t n){return heap_alloc(n);}
/* Each EXE gets its own bounded descriptor table. The original WinCE
 * \fls0 path aliases the currently selected EX-word volume. Other native
 * paths can name \drv0 and \crd0 explicitly. Native disk writes are OFF.
 */
static unsigned g_guest_disk;
static char g_guest_root[NTOS_EXWORD_PATH_MAX];
static NtosExwordArena g_guest_area;
struct GuestFile {int os_fd;bool live;};
static GuestFile g_guest_files[8];
static bool guest_path(const char *input,char out[NTOS_EXWORD_PATH_MAX])
{
    if(!input)return false;
    unsigned pre=0u;
    while(input[pre]=='\\' && pre<2u)++pre;
    const char *name=input+pre;
    const char *root=nullptr,*tail=nullptr;
    char x=name[0],y=name[1],z=name[2];
    if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');
    if(y>='A'&&y<='Z')y=(char)(y-'A'+'a');
    if(z>='A'&&z<='Z')z=(char)(z-'A'+'a');
    if(x=='f'&&y=='l'&&z=='s'&&name[3]=='0'&&name[4]=='\\') {
        root=g_guest_root[0]?g_guest_root:
             (g_guest_disk?"\\\\crd0":"\\\\drv0");tail=name+4;
    } else if(x=='d'&&y=='r'&&z=='v'&&name[3]=='0'&&name[4]=='\\') {
        root="\\\\drv0";tail=name+4;
    } else if(x=='c'&&y=='r'&&z=='d'&&name[3]=='0'&&name[4]=='\\') {
        root="\\\\crd0";tail=name+4;
    } else return false;
    unsigned n=0u,i=0u;
    while(root[i] && n+1u<NTOS_EXWORD_PATH_MAX)out[n++]=root[i++];
    i=0u;
    while(tail[i] && n+1u<NTOS_EXWORD_PATH_MAX)out[n++]=tail[i++];
    out[n]=0;
    return !tail[i] && n>=7u;
}
static int guest_open(const char *path,uint32_t flags)
{
    if(flags!=1u)return -5; /* No write/create until hardware tested. */
    char native[NTOS_EXWORD_PATH_MAX];
    if(!guest_path(path,native))return -2;
    unsigned i=0u;
    while(i<8u && g_guest_files[i].live)++i;
    if(i>=8u)return -4;
    int fd=ntos_exword_open_read(native);
    if(fd<0)return fd;
    g_guest_files[i]={fd,true};
    return (int)(i+1u);
}
static GuestFile *guest_fd(int id) {
    return id>0 && id<=8 && g_guest_files[id-1].live ?
           &g_guest_files[id-1]:nullptr;
}
static bool guest_buffer(void *p,uint32_t n)
{
    uint32_t at=(uint32_t)(uintptr_t)p;
    return (at>=g_guest_area.image_start && at<=g_guest_area.image_end &&
            n<=g_guest_area.image_end-at) ||
           (at>=g_guest_area.stack_start && at<=g_guest_area.stack_end &&
            n<=g_guest_area.stack_end-at);
}
static int guest_read(int id,void *to,uint32_t bytes)
{
    GuestFile *f=guest_fd(id);
    if(!f || !guest_buffer(to,bytes))return -2;
    return ntos_exword_read_fd(f->os_fd,to,bytes);
}
static int guest_close(int id)
{
    GuestFile *f=guest_fd(id);
    if(!f)return -1;
    int result=ntos_exword_close_fd(f->os_fd);
    if(result>=0)*f={};
    return result;
}
static int guest_size(int id)
{
    GuestFile *f=guest_fd(id);
    return f?ntos_exword_size_fd(f->os_fd):-1;
}
static int guest_seek(int id,int offset,uint32_t method)
{
    GuestFile *f=guest_fd(id);
    return f?ntos_exword_seek_fd(f->os_fd,offset,method):-1;
}
static int guest_stat(const char *path)
{
    char native[NTOS_EXWORD_PATH_MAX];
    if(!guest_path(path,native))return -2;
    uint32_t size=0u;
    return ntos_exword_file_size(native,&size);
}
static void guest_close_all()
{
    for(GuestFile &f:g_guest_files)if(f.live) {
        (void)ntos_exword_close_fd(f.os_fd);
        f={};
    }
}

static void init_host_api()
{
    for(unsigned i=0;i<28u;++i)ntos_host_table[i]=nullptr;
    ntos_host_table[0]=(const void *)&host_puts;
    ntos_host_table[2]=(const void *)&host_refresh;
    ntos_host_table[3]=(const void *)&host_malloc;
    ntos_host_table[4]=(const void *)&host_free;
    ntos_host_table[5]=(const void *)&host_getkey;
    ntos_host_table[6]=(const void *)&host_clear;
    ntos_host_table[7]=(const void *)&host_pixel;
    ntos_host_table[8]=(const void *)&host_line;
    ntos_host_table[9]=(const void *)&host_fillrect;
    ntos_host_table[10]=(const void *)&host_input;
    ntos_host_table[11]=(const void *)&guest_open;
    ntos_host_table[12]=(const void *)&guest_read;
    ntos_host_table[14]=(const void *)&guest_close;
    ntos_host_table[16]=(const void *)&host_draw_text;
    ntos_host_table[17]=(const void *)&guest_size;
    ntos_host_table[18]=(const void *)&guest_seek;
    ntos_host_table[22]=(const void *)&guest_stat;
}
struct Source {const char *path;};
static bool file_read(void *context,uint32_t off,uint8_t *to,uint32_t n)
{
    const Source *s=(const Source *)context;
    return ntos_exword_read_at(s->path,off,to,n)==0;
}
static void clear_area(uint8_t *where,uint32_t bytes)
{
    for(uint32_t i=0u;i<bytes;++i)where[i]=0u;
}
static bool copy_sections(const Source &source,const pe::Image &hdr,
                          uint8_t *mem,uint32_t size)
{
    if(hdr.oh.size_of_headers>size)return false;
    if(!file_read((void *)&source,0u,mem,hdr.oh.size_of_headers))
        return false;
    for(unsigned i=0u;i<hdr.section_count;++i){
        const pe::SectionHeader &sh=hdr.sections[i];
        if(sh.rva>size || sh.raw_size>size-sh.rva)return false;
        if(!sh.raw_size)continue;
        /* Physical image layout includes initialized bytes even if a
         * misleading UNINITIALIZED_DATA flag is present. */
        if(!file_read((void *)&source,sh.raw_offset,mem+sh.rva,sh.raw_size))
            return false;
    }
    return true;
}
static bool snap_imports(const pe::Image &hdr,uint8_t *image,uint32_t size)
{
    if(!hdr.oh.import_rva)return true;
    const uint32_t descriptors=hdr.oh.import_size/20u;
    if(!descriptors || descriptors>64u){set_error("invalid import directory");return false;}
    for(uint32_t d=0u;d<descriptors;++d){
        pe::ImportDesc desc{};
        if(!pe::read_import_desc(hdr,image,size,d,&desc))return true;
        char module[48]{};
        if(!pe::rva_string(image,size,desc.name_rva,module,sizeof module) ||
            !desc.ft_rva || !same_case(module,"COREDLL.DLL")) {
            set_error("needs another DLL (unsupported)");return false;
        }
        uint32_t names=desc.oft_rva?desc.oft_rva:desc.ft_rva;
        for(unsigned i=0u;i<8192u;++i){
            if(names>size || desc.ft_rva>size ||
               i>size/4u || 4u>size-names || 4u>size-desc.ft_rva ||
               4u*i>size-names-4u || 4u*i>size-desc.ft_rva-4u){
                set_error("invalid import thunk");return false;
            }
            uint32_t slot=pe::rd32le(image+names+4u*i);
            if(!slot)break;
            if(slot&0x80000000u){set_error("ordinal COREDLL imports unsupported");return false;}
            char symbol[80]{};
            if(slot>size || size-slot<3u ||
               !pe::rva_string(image,size,slot+2u,symbol,sizeof symbol)) {
                set_error("invalid import name");return false;
            }
            uint32_t target=ntos_exword_lookup_coredll(symbol);
            if(!target){set_error("COREDLL export not implemented");return false;}
            pe::wr32be(image+desc.ft_rva+4u*i,target);
        }
    }
    return true;
}
static void sync_exec(uint32_t start,uint32_t size)
{
    for(uint32_t off=0u;off<size;off+=32u){
        void *p=(void *)(uintptr_t)(start+off);
        __asm__ volatile("ocbp @%0"::"r"(p):"memory");
    }
    __asm__ volatile("synco":::"memory");
    for(uint32_t off=0u;off<size;off+=32u){
        void *p=(void *)(uintptr_t)(start+off);
        __asm__ volatile("icbi @%0"::"r"(p):"memory");
    }
    __asm__ volatile("synco":::"memory");
}
#include "modules.inc"

int ntos_exword_run_pe(const char *path,const char *sysroot,uint32_t *exit_code)
{
    if(exit_code)*exit_code=0u;
    set_error("invalid EXE path");
    if(!path || !exit_code)return -1;
    NtosExwordArena area{};
    set_error("no verified executable P1 RAM on this ROM");
    if(!ntos_exword_arena_check(&area))return -2;

    /* A single process and its complete dependency graph live in the
     * same P1 arena. No firmware TLB edit or unsafe OS scratch reuse. */
    g_guest_area=area;
    g_guest_disk=path[2]=='c'?1u:0u;
    unsigned n=0u;
    while(sysroot && sysroot[n] && n+1u<sizeof g_guest_root) {
        g_guest_root[n]=sysroot[n];++n;
    }
    g_guest_root[n]=0;
    for(GuestFile &f:g_guest_files)f={};
    for(Allocation &a:g_alloc)a={};
    using namespace ntos_exword_modules;
    /* Place the metadata at the end of the P1 image range. The aligned
     * gap beneath it is unavailable to both DLL images and guest heap. */
    const uint32_t module_bytes=sizeof(Module)*MAX_MODULES;
    if(area.image_end<=area.image_start+0x10000u ||
       module_bytes>area.image_end-area.image_start-0x10000u) {
        set_error("P1 arena has no module table space");return -2;
    }
    const uint32_t metadata=(area.image_end-module_bytes)&~7u;
    limit=metadata&~4095u;
    if(limit<=area.image_start+4096u) {
        set_error("P1 metadata overlaps code");return -2;
    }
    modules=(Module *)(uintptr_t)metadata;
    for(unsigned i=0u;i<MAX_MODULES;++i)modules[i]={};
    count=attach_count=0u;
    cursor=area.image_start;
    g_guest_area.image_end=limit;

    unsigned exe=0u;
    if(!load(path,"@EXE",false,0u,&exe)) {
        ntos_exword_arena_release(area);
        return -3;
    }
    if(exe!=0u) {
        set_error("incorrect EXE image slot");
        ntos_exword_arena_release(area);
        return -3;
    }
    for(unsigned i=0u;i<count;++i)
        if(!bind_imports(modules[i])) {
            ntos_exword_arena_release(area);
            return -4;
        }
    uint32_t image_bytes=cursor-area.image_start;
    g_heap_base=(cursor+7u)&~7u;
    g_heap_end=limit;
    if(g_heap_base>g_heap_end) {
        set_error("no heap after dependency DLLs");
        ntos_exword_arena_release(area);
        return -5;
    }
    g_print_line=0u;
    init_host_api();
    sync_exec(area.image_start,image_bytes);
    if(!init_one(exe,0u)) {
        unload();
        ntos_exword_dll_main(nullptr,0u,nullptr);
        guest_close_all();
        ntos_exword_arena_release(area);
        return -6;
    }

    dclear(C_WHITE);
    dtext(7,7,C_BLACK,"NTOSH  |  Running SH4BE EXE");
    dupdate();
    int rc=ntos_exword_invoke(
        modules[exe].base+modules[exe].pe.oh.entry_rva,
        ntos_host_table,area.stack_end-16u);
    unload();
    /* DLL and COREDLL teardown happens BEFORE releasing guest heap. */
    ntos_exword_dll_main(nullptr,0u,nullptr);
    guest_close_all();
    *exit_code=(uint32_t)rc;
    set_error("ok");
    ntos_exword_arena_release(area);
    return 0;
}
