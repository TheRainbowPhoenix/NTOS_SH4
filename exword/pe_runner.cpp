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
#include "shortname.h"
#include "p1_vm.hpp"
#include "rom32a_mmu.hpp"
#include "native_vm.hpp"
#include "../src/nt/process.hpp"
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
    /* Provide all tested NtosApi indices without touching the CP400-only
     * 0x08100000 P0 TLB API window. */
    const void *ntos_host_table[64]={};
}

static const char *g_error="not started";
static uint8_t *g_heap_ptr;
static uint32_t g_heap_base,g_heap_end;
static unsigned g_print_line;
struct Allocation {uint32_t addr,bytes;bool live;};
static constexpr unsigned MAX_HEAP_BLOCKS=192u;
struct GuestMetadata {
    nt::Process process;
    Allocation blocks[MAX_HEAP_BLOCKS];
};
static GuestMetadata *g_guest_metadata;
static nt::Process *g_guest_process;
static Allocation *g_alloc;
static void *heap_alloc(uint32_t size);
static void heap_free(void *p);
static void *pool_allocate(void *,size_t n) {
    return n && n<=0xffffffffu?heap_alloc((uint32_t)n):nullptr;
}
static void pool_deallocate(void *,void *p) {heap_free(p);}
static int32_t event_create(uint32_t manual,uint32_t initial) {
    if(!g_guest_process)return (int32_t)nt::DEVICE_BUSY;
    uint32_t handle=0u;
    nt::Status rc=g_guest_process->create_event(manual!=0u,initial!=0u,&handle);
    return rc==nt::SUCCESS?(int32_t)handle:(int32_t)rc;
}
static uint32_t event_set(uint32_t handle) {
    return g_guest_process?g_guest_process->event_set(handle):nt::DEVICE_BUSY;
}
static uint32_t event_reset(uint32_t handle) {
    return g_guest_process?g_guest_process->event_reset(handle):nt::DEVICE_BUSY;
}
static uint32_t event_wait(uint32_t handle,uint32_t timeout) {
    return g_guest_process?g_guest_process->event_poll(handle,timeout):nt::DEVICE_BUSY;
}
static uint32_t event_close(uint32_t handle) {
    return g_guest_process?
        g_guest_process->handles.close(handle,nt::Type::event):nt::DEVICE_BUSY;
}
static void *pool_tagged_alloc(uint32_t n,uint32_t tag) {
    return g_guest_process?g_guest_process->pool.alloc(n,tag):nullptr;
}
static uint32_t pool_tagged_free(void *p,uint32_t tag) {
    return g_guest_process?g_guest_process->pool.free(p,tag):nt::DEVICE_BUSY;
}
static uint32_t pool_peak(void) {
    return g_guest_process?g_guest_process->pool.peak:0u;
}
static uint32_t pool_live(void) {
    return g_guest_process?g_guest_process->pool.current:0u;
}
static uint32_t handle_count(void) {
    return g_guest_process?g_guest_process->handles.used:0u;
}
static NtosP1Vm g_vm;
static NtosExwordNativeVm g_native_vm;
static NtosExwordArena g_guest_area;
static uint32_t vm_allocate(uint32_t n,void *) {
    return (uint32_t)(uintptr_t)heap_alloc(n);
}
static void vm_deallocate(uint32_t raw,void *) {
    heap_free((void *)(uintptr_t)raw);
}
static void vm_zero(uint32_t address,uint32_t n,void *) {
    uint8_t *p=(uint8_t *)(uintptr_t)address;
    for(uint32_t i=0u;i<n;++i)p[i]=0u;
}
static uint32_t vm_reserve(uint32_t at,uint32_t pages) {
    return g_vm.reserve(at,pages);
}
static uint32_t vm_commit(uint32_t at,uint32_t pages,uint32_t protection) {
    return g_vm.change(at,pages,protection,true);
}
static uint32_t vm_protect(uint32_t at,uint32_t pages,uint32_t protection) {
    return g_vm.change(at,pages,protection,false);
}
static uint32_t vm_query(uint32_t at) {
    return g_vm.query(at);
}
static uint32_t vm_release(uint32_t at) {
    return g_vm.release(at);
}
static uint32_t vm_committed(void) {
    return g_vm.committed();
}
static int32_t vm_hint(uint32_t at) {
    uint32_t p=g_vm.query(at);
    if(p==1u || p==2u)return 0;
    if(p==0u)return -1; /* reserved, not committed */
    /* An Exword P1 image/stack never needs a UTLB fill. Reject every
     * unleased firmware P0 address, even if its alias is computable
     * from the ROM's pager algorithm. */
    return exword_rom32a::guest_hint(at,
        g_guest_area.image_start,g_guest_area.image_end,
        g_guest_area.stack_start,g_guest_area.stack_end);
}

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
/* Guest P1 allocator. Coalesce all freed blocks to support the 128-slot
 * tagged NTOS pool and its 512 KiB stress phase without resident BSS.
 * GuestImage/VM and DLL allocations share one verified P1 heap. */
static void *heap_alloc(uint32_t size)
{
    if(!g_alloc || !size || size>0xfffffff8u)return nullptr;
    size=(size+7u)&~7u;
    unsigned best=MAX_HEAP_BLOCKS;
    for(unsigned i=0u;i<MAX_HEAP_BLOCKS;++i) {
        const Allocation &a=g_alloc[i];
        if(!a.live && a.addr && a.bytes>=size &&
           (best==MAX_HEAP_BLOCKS || a.bytes<g_alloc[best].bytes))
            best=i;
    }
    if(best!=MAX_HEAP_BLOCKS) {
        Allocation &a=g_alloc[best];
        if(a.bytes>size && a.bytes-size>=8u) {
            for(unsigned i=0u;i<MAX_HEAP_BLOCKS;++i)
                if(!g_alloc[i].addr) {
                    g_alloc[i]={a.addr+size,a.bytes-size,false};
                    a.bytes=size;
                    break;
                }
        }
        a.live=true;
        return (void *)(uintptr_t)a.addr;
    }
    if(g_heap_base>g_heap_end || size>g_heap_end-g_heap_base)return nullptr;
    for(unsigned i=0u;i<MAX_HEAP_BLOCKS;++i)if(!g_alloc[i].addr) {
        uint32_t at=g_heap_base;
        g_alloc[i]={at,size,true};
        g_heap_base+=size;
        return (void *)(uintptr_t)at;
    }
    return nullptr;
}
static void heap_free(void *p)
{
    const uint32_t addr=(uint32_t)(uintptr_t)p;
    if(!g_alloc || !addr)return;
    for(unsigned i=0u;i<MAX_HEAP_BLOCKS;++i) {
        Allocation &a=g_alloc[i];
        if(a.addr!=addr || !a.live)continue;
        a.live=false;
        /* All regions are disjoint, so adjacency can be merged safely. */
        for(unsigned j=0u;j<MAX_HEAP_BLOCKS;++j) {
            if(i==j || !g_alloc[j].addr || g_alloc[j].live)continue;
            Allocation &b=g_alloc[j];
            if(a.addr+a.bytes==b.addr) {
                a.bytes+=b.bytes;b={};j=0u;continue;
            }
            if(b.addr+b.bytes==a.addr) {
                a.addr=b.addr;a.bytes+=b.bytes;b={};j=0u;
            }
        }
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
/* The read-only SH7305 registers are safe to inspect from P1. No
 * MMU/UTLB/VBR changes are made by this diagnostic. */
static uint32_t host_rom_mmucr(void) {
    return *(volatile const uint32_t *)(uintptr_t)0xFF000010u;
}
static uint32_t host_rom_vbr(void) {
    uint32_t v=0u;
    __asm__ volatile("stc vbr,%0" : "=r"(v));
    return v;
}
static uint32_t host_rom_sr(void) {
    uint32_t v=0u;
    __asm__ volatile("stc sr,%0" : "=r"(v));
    return v;
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
/* Serial foreground execution only. Each child starts AFTER parent Ob/Ex
 * teardown and native OS descriptor close; no shared P0 address space. */
static char g_next_program[NTOS_EXWORD_PATH_MAX];
static unsigned g_handoff_depth;
static uint32_t g_guest_cookie_seed=1u;
/* NTOS Ob issues stale-proof typed file handles; no raw fd escapes. */
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
    if(tail[i] || n<7u)return false;
    /* File APIs also see the flat 8.3 names used by the native loader.
     * Only rewrite a single EXE/DLL/CPL leaf; never synthesize folders. */
    if(g_guest_root[0] && root==g_guest_root) {
        unsigned k=0u;
        while(tail[k]=='\\')++k;
        unsigned leaf=k;
        for(;tail[k];++k)if(tail[k]=='\\')return true;
        char short_file[13];
        if(ntos_exword_shortname(tail+leaf,short_file)) {
            unsigned at=0u;
            while(root[at] && at+1u<NTOS_EXWORD_PATH_MAX) {
                out[at]=root[at];++at;
            }
            if(at+1u+12u>=NTOS_EXWORD_PATH_MAX)return false;
            out[at++]='\\';
            unsigned j=0u;
            while(short_file[j])out[at++]=short_file[j++];
            out[at]=0;
        }
    }
    return true;
}
static int guest_launch(const char *path)
{
    if(!g_guest_process || !g_guest_root[0] ||
       g_handoff_depth>=2u || g_next_program[0])return 0;
    char candidate[NTOS_EXWORD_PATH_MAX];
    if(!guest_path(path,candidate))return 0;
    unsigned i=0u;
    while(g_guest_root[i] && candidate[i]==g_guest_root[i])++i;
    if(g_guest_root[i] || candidate[i]!='\\')return 0;
    const char *leaf=candidate+i+1u;
    if(!*leaf)return 0;
    unsigned len=0u;
    while(leaf[len]) {
        if(leaf[len]=='\\' || leaf[len]=='/')return 0;
        ++len;
    }
    if(len<5u || !same_case(leaf+len-4u,".EXE"))return 0;
    uint32_t size=0u;
    if(ntos_exword_file_size(candidate,&size)<0 ||
       size<64u || size>8u*1024u*1024u)return 0;
    unsigned j=0u;
    while(candidate[j] && j+1u<sizeof g_next_program) {
        g_next_program[j]=candidate[j];++j;
    }
    g_next_program[j]=0;
    return candidate[j]==0?1:0;
}

static int guest_open(const char *path,uint32_t flags)
{
    if(!g_guest_process || flags!=1u)return -5;
    /* A second File_Open must never reach the firmware SDK. */
    if(g_guest_process->raw_file>=0)return -16;
    char native[NTOS_EXWORD_PATH_MAX];
    if(!guest_path(path,native))return -2;
    int fd=ntos_exword_open_read(native);
    if(fd<0)return fd;
    uint32_t token=0u;
    nt::Status status=g_guest_process->attach_file(fd,&token);
    if(status!=nt::SUCCESS) {
        (void)ntos_exword_close_fd(fd);
        return -4;
    }
    return (int)token;
}
static int guest_fd(int token) {
    return g_guest_process?
        g_guest_process->file_fd((uint32_t)token):-1;
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
    int fd=guest_fd(id);
    if(fd<0 || !guest_buffer(to,bytes))return -2;
    return ntos_exword_read_fd(fd,to,bytes);
}
static int guest_close(int id)
{
    int fd=guest_fd(id);
    if(fd<0)return -2;
    int result=ntos_exword_close_fd(fd);
    if(result>=0)
        (void)g_guest_process->drop_file((uint32_t)id);
    return result;
}
static int guest_size(int id)
{
    int fd=guest_fd(id);
    return fd>=0?ntos_exword_size_fd(fd):-1;
}
static int guest_seek(int id,int offset,uint32_t method)
{
    int fd=guest_fd(id);
    return fd>=0?ntos_exword_seek_fd(fd,offset,method):-1;
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
    if(g_guest_process && g_guest_process->raw_file>=0) {
        (void)ntos_exword_close_fd(g_guest_process->raw_file);
        g_guest_process->raw_file=-1;
    }
}

static uint32_t guest_rights(uint32_t at);
static bool nt_read_word(void *,uint32_t,uint32_t *);
static bool nt_write_word(void *,uint32_t,uint32_t);
static uint32_t nt_alloc(uint32_t process,uint32_t *base,uint32_t zero,
                         uint32_t *size,uint32_t flags,uint32_t protection) {
    return g_native_vm.allocate(process,(uint32_t)(uintptr_t)base,zero,
                                (uint32_t)(uintptr_t)size,flags,protection);
}
static uint32_t nt_protect(uint32_t process,uint32_t *base,uint32_t *size,
                           uint32_t protection,uint32_t *old) {
    return g_native_vm.protect(process,(uint32_t)(uintptr_t)base,
                               (uint32_t)(uintptr_t)size,protection,
                               (uint32_t)(uintptr_t)old);
}
static uint32_t nt_free(uint32_t process,uint32_t *base,uint32_t *size,
                        uint32_t flags) {
    return g_native_vm.free(process,(uint32_t)(uintptr_t)base,
                            (uint32_t)(uintptr_t)size,flags);
}
static void init_host_api()
{
    for(unsigned i=0;i<64u;++i)ntos_host_table[i]=nullptr;
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
    ntos_host_table[23]=(const void *)&vm_hint;
    ntos_host_table[24]=(const void *)&host_rom_mmucr;
    ntos_host_table[25]=(const void *)&host_rom_vbr;
    ntos_host_table[26]=(const void *)&host_rom_sr;
    ntos_host_table[27]=(const void *)&guest_launch;
    ntos_host_table[28]=(const void *)&event_create;
    ntos_host_table[29]=(const void *)&event_set;
    ntos_host_table[30]=(const void *)&event_reset;
    ntos_host_table[31]=(const void *)&event_wait;
    ntos_host_table[32]=(const void *)&event_close;
    ntos_host_table[33]=(const void *)&pool_tagged_alloc;
    ntos_host_table[34]=(const void *)&pool_tagged_free;
    ntos_host_table[35]=(const void *)&pool_peak;
    ntos_host_table[36]=(const void *)&pool_live;
    ntos_host_table[37]=(const void *)&handle_count;
    ntos_host_table[38]=(const void *)&guest_rights;
    ntos_host_table[41]=(const void *)&vm_reserve;
    ntos_host_table[42]=(const void *)&vm_commit;
    ntos_host_table[43]=(const void *)&vm_protect;
    ntos_host_table[44]=(const void *)&vm_query;
    ntos_host_table[45]=(const void *)&vm_release;
    ntos_host_table[46]=(const void *)&vm_committed;
    ntos_host_table[51]=(const void *)&nt_alloc;
    ntos_host_table[52]=(const void *)&nt_protect;
    ntos_host_table[53]=(const void *)&nt_free;
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

/* Software classification of guest PE section rights. ROM32A's
 * normal add-in P0 mappings do enforce 0x15c (RO) / 0x17c (RW), but our
 * private PE images are P1 SRAM and HAVE NO hardware page protection.
 * This query is safe for MMROTEST; it is not an MMU guard or trap. */
static uint32_t guest_rights(uint32_t at)
{
    using namespace ntos_exword_modules;
    for(unsigned i=0u;i<count;++i) {
        const Module &m=modules[i];
        if(at<m.base || at-m.base>=m.size)continue;
        uint32_t rva=at-m.base;
        for(unsigned j=0u;j<m.pe.section_count;++j) {
            const pe::SectionHeader &sh=m.pe.sections[j];
            uint32_t span=sh.vsize>sh.raw_size?sh.vsize:sh.raw_size;
            if(rva>=sh.rva && rva-sh.rva<span)
                return (sh.characteristics&0x80000000u)?2u:1u;
        }
        return rva<m.pe.oh.size_of_headers?1u:0u;
    }
    uint32_t v=g_vm.query(at);
    if(v==1u || v==2u)return v;
    /* Only the guest's private P1 stack is mutable here. */
    return at>=g_guest_area.stack_start && at<g_guest_area.stack_end?2u:0u;
}

/* An NT Native pointer is a caller-supplied address into writable guest
 * EXE/DLL PE data. Deliberately reject the EX-word OS stack, guest stack,
 * ROM, all P0 add-in windows, and RO executable/constant sections. */
static bool nt_validate_word(uint32_t at)
{
    if((at&3u) || !exword_rom32a::guest_owned(at,4u,
        g_guest_area.image_start,g_guest_area.image_end,
        0u,0u))return false;
    return guest_rights(at)==2u && guest_rights(at+3u)==2u;
}
static bool nt_read_word(void *,uint32_t at,uint32_t *out)
{
    if(!out || !nt_validate_word(at))return false;
    *out=*(volatile const uint32_t *)(uintptr_t)at;
    return true;
}
static bool nt_write_word(void *,uint32_t at,uint32_t value)
{
    if(!nt_validate_word(at))return false;
    *(volatile uint32_t *)(uintptr_t)at=value;
    return true;
}

int ntos_exword_run_pe(const char *path,const char *content_dir,uint32_t *exit_code)
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
    while(content_dir && content_dir[n] && n+1u<sizeof g_guest_root) {
        g_guest_root[n]=content_dir[n];++n;
    }
    g_guest_root[n]=0;
    g_guest_process=nullptr;
    g_alloc=nullptr;
    g_next_program[0]=0;
    using namespace ntos_exword_modules;
    /* Place the metadata at the end of the P1 image range. The aligned
     * gap beneath it is unavailable to both DLL images and guest heap. */
    const uint32_t module_bytes=sizeof(Module)*MAX_MODULES;
    const uint32_t guest_bytes=sizeof(GuestMetadata);
    if(area.image_end<=area.image_start+0x10000u ||
       module_bytes+guest_bytes>area.image_end-area.image_start-0x10000u) {
        set_error("P1 arena has no guest NT kernel space");return -2;
    }
    const uint32_t module_at=(area.image_end-module_bytes)&~7u;
    const uint32_t metadata=(module_at-guest_bytes)&~7u;
    limit=metadata&~4095u;
    if(limit<=area.image_start+4096u) {
        set_error("P1 metadata overlaps code");return -2;
    }
    modules=(Module *)(uintptr_t)module_at;
    g_guest_metadata=(GuestMetadata *)(uintptr_t)metadata;
    *g_guest_metadata={};
    g_guest_process=&g_guest_metadata->process;
    g_alloc=g_guest_metadata->blocks;
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
    if(!g_guest_process->begin()) {
        set_error("NTOS guest process init failed");
        ntos_exword_arena_release(area);
        return -5;
    }
    /* Keep Ob handles unique across serial child launches. */
    if(g_guest_cookie_seed>0x3fffff00u)g_guest_cookie_seed=1u;
    g_guest_process->handles.next_cookie=g_guest_cookie_seed;
    g_guest_process->pool.configure(nullptr,pool_allocate,pool_deallocate);
    g_vm.begin(vm_allocate,vm_deallocate,vm_zero,nullptr);
    g_native_vm.begin(&g_vm,nt_read_word,nt_write_word,nullptr);
    init_host_api();
    sync_exec(area.image_start,image_bytes);
    if(!init_one(exe,0u)) {
        unload();
        ntos_exword_dll_main(nullptr,0u,nullptr);
        guest_close_all();
        g_guest_process->finish();
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
    g_guest_cookie_seed=g_guest_process->handles.next_cookie;
    g_guest_process->finish();
    *exit_code=(uint32_t)rc;
    set_error("ok");
    /* Copy the queued child before reinitializing the guest's metadata. */
    char child[NTOS_EXWORD_PATH_MAX];
    unsigned at=0u;
    while(g_next_program[at] && at+1u<sizeof child) {
        child[at]=g_next_program[at];++at;
    }
    child[at]=0;
    g_next_program[0]=0;
    ntos_exword_arena_release(area);
    if(child[0]) {
        ++g_handoff_depth;
        int status=ntos_exword_run_pe(child,content_dir,exit_code);
        --g_handoff_depth;
        return status;
    }
    return 0;
}
