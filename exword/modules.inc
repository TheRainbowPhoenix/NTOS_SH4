/* Bounded recursive PE DLL graph (native P1 EX-word).
 * Materialize every image and apply relocations BEFORE snapping any IAT.
 * Graph cycles are okay for mapping; DLL initialization uses DFS ordering.
 * No CP400 UTLB remapping and no executable OS scratch allocations.
 */
namespace ntos_exword_modules {
constexpr unsigned MAX_MODULES=8u;
struct Module {
    char name[48];
    char path[NTOS_EXWORD_PATH_MAX];
    pe::Image pe;
    uint8_t *image;
    uint32_t base,size;
    bool dll;
    unsigned init_state;
};
/* Keep large PE image descriptors in the already-verified guest P1 arena:
 * the EX-word gint resident 32 KiB is reserved for stack/driver/OS switching.
 * modules is set before any PE load; its entire region is kept outside
 * both the module image pages and the guest allocation heap. */
static Module *modules;
static unsigned count;
static uint32_t cursor,limit;
static unsigned attach_order[MAX_MODULES],attach_count;

static unsigned str_len(const char *p) {
    unsigned n=0u;
    while(p && p[n])++n;
    return n;
}
static bool str_copy(char *out,unsigned capacity,const char *src) {
    unsigned i=0u;
    if(!src || !capacity)return false;
    for(;src[i] && i+1u<capacity;++i)out[i]=src[i];
    out[i]=0;
    return !src[i];
}
static bool joined(const char *dir,const char *leaf,char out[NTOS_EXWORD_PATH_MAX]) {
    if(!str_copy(out,NTOS_EXWORD_PATH_MAX,dir))return false;
    unsigned n=str_len(out),j=0u;
    if(n+1u>=NTOS_EXWORD_PATH_MAX)return false;
    out[n++]='\\';
    while(leaf[j] && n+1u<NTOS_EXWORD_PATH_MAX)out[n++]=leaf[j++];
    out[n]=0;
    return !leaf[j];
}
static bool dirname(const char *full,char dest[NTOS_EXWORD_PATH_MAX]) {
    if(!str_copy(dest,NTOS_EXWORD_PATH_MAX,full))return false;
    unsigned n=str_len(dest);
    while(n && dest[n-1u]!='\\')--n;
    if(n<7u)return false;
    dest[n-1u]=0;
    return true;
}
static bool valid_dll(const char *name) {
    unsigned n=str_len(name);
    if(n<5u || n>=48u || !same_case(name+n-4u,".DLL"))return false;
    for(unsigned i=0u;i<n;++i)
        if(name[i]=='\\' || name[i]=='/' || name[i]==':' ||
           (name[i]=='.' && name[i+1u]=='.') ||
           (unsigned char)name[i]<32u)return false;
    return true;
}
static bool in_dir(const char *dir,const char *filename,
                   char out[NTOS_EXWORD_PATH_MAX]) {
    if(!dir || !dir[0])return false;
    if(!joined(dir,filename,out))return false;
    uint32_t size=0u;
    if(ntos_exword_file_size(out,&size)==0)return true;
    char short_file[13];
    if(!ntos_exword_shortname(filename,short_file))return false;
    if(joined(dir,short_file,out) &&
       ntos_exword_file_size(out,&size)==0)return true;
    char pattern[NTOS_EXWORD_PATH_MAX];
    if(!joined(dir,"*",pattern))return false;
    ntos_exword_listing *directory=ntos_exword_scratch();
    unsigned skip=0u,more=0u;
    do {
        if(ntos_exword_scan_page(pattern,skip,directory,&more)<0)return false;
        for(unsigned i=0u;i<directory->count;++i)
            if(directory->kinds[i]!=5u &&
               same_case(directory->names[i],filename))
                return joined(dir,directory->names[i],out);
        if(!directory->count)break;
        skip+=directory->count;
    } while(more && skip<=4096u);
    return false;
}
/* EX-word _CONTENT is a single flat 8.3 namespace. Recursive DLLs
 * resolve in the same directory as the process EXE. */
static bool resolve_dll(const char *filename,const char *requester,
                        char out[NTOS_EXWORD_PATH_MAX]) {
    (void)requester;
    char dir[NTOS_EXWORD_PATH_MAX];
    if(count && dirname(modules[0].path,dir))
        return in_dir(dir,filename,out);
    return false;
}
static int find_module(const char *name) {
    for(unsigned i=0u;i<count;++i)
        if(same_case(modules[i].name,name))return (int)i;
    return -1;
}
static bool check_entry(const Module &m) {
    uint32_t rva=m.pe.oh.entry_rva;
    if(rva>=m.size || ((m.base+rva)&1u))return false;
    for(unsigned i=0u;i<m.pe.section_count;++i) {
        const pe::SectionHeader &s=m.pe.sections[i];
        uint32_t span=s.vsize>s.raw_size?s.vsize:s.raw_size;
        if((s.characteristics&pe::SCN_MEM_EXECUTE) &&
           rva>=s.rva && rva-s.rva<span)return true;
    }
    return false;
}
static bool load(const char *path,const char *name,bool is_dll,
                 unsigned depth,unsigned *index) {
    if(depth>=MAX_MODULES || !path || !index) {
        set_error("DLL dependency graph too deep");return false;
    }
    int existing=find_module(name);
    if(existing>=0) {*index=(unsigned)existing;return true;}
    if(count>=MAX_MODULES) {
        set_error("too many dependency DLLs");return false;
    }
    uint32_t file_size=0u;
    if(ntos_exword_file_size(path,&file_size)<0 ||
       file_size<64u || file_size>8u*1024u*1024u) {
        set_error("DLL/EXE file missing or unreadable");return false;
    }
    Source source={path};
    pe::Image header{};
    if(pe_stream::parse(file_read,&source,file_size,&header)!=0 ||
       !!(header.fh.characteristics&pe::F_DLL)!=is_dll ||
       !(header.fh.characteristics&pe::F_EXECUTABLE_IMAGE)) {
        set_error("invalid SH4BE DLL/EXE header");return false;
    }
    const uint32_t bytes=header.oh.size_of_image;
    if(!bytes || bytes>0x100000u || (bytes&0x80000000u)) {
        set_error("DLL image too large");return false;
    }
    uint32_t start=(cursor+4095u)&~4095u;
    uint32_t rounded=(bytes+4095u)&~4095u;
    if(start<cursor || start>limit || rounded>limit-start) {
        set_error("dependency images exceed P1 RAM");return false;
    }
    cursor=start+rounded;
    Module &m=modules[count];
    m={};
    if(!str_copy(m.name,sizeof m.name,name) ||
       !str_copy(m.path,sizeof m.path,path)) {
        set_error("DLL path too long");return false;
    }
    m.pe=header;
    m.image=(uint8_t *)(uintptr_t)start;
    m.base=start;
    m.size=bytes;
    m.dll=is_dll;
    unsigned id=count++;
    *index=id;
    clear_area(m.image,bytes);
    if(!copy_sections(source,header,m.image,bytes)) {
        set_error("DLL section read failed");return false;
    }
    /* Old CP400 NT probes dereference a hardcoded P0 NtosApi window at
     * 0x08100000. That address has NO EX-word mapping: refuse the guest
     * before executing rather than causing a firmware TLB-miss crash.
     * The Exword VMTEST build uses the passed host NtosApi pointer. */
    for(unsigned si=0u;si<header.section_count && !is_dll;++si) {
        const pe::SectionHeader &s=header.sections[si];
        if(!(s.characteristics&pe::SCN_MEM_EXECUTE) || s.rva>bytes)continue;
        uint32_t span=s.vsize>s.raw_size?s.vsize:s.raw_size;
        if(span>bytes-s.rva)span=bytes-s.rva;
        for(uint32_t at=0u;at+4u<=span;++at) {
            const uint8_t *p=m.image+s.rva+at;
            if(p[0]==0x08u && p[1]==0x10u &&
               p[2]==0u && p[3]==0u) {
                set_error("CP400 P0 API window unsupported on Exword");
                return false;
            }
        }
    }
    const char *fix=nullptr;
    if(!ntos_rebase::apply(m.pe,m.image,m.size,m.base,&fix)) {
        set_error(fix?fix:"DLL relocation failure");return false;
    }
    if(m.pe.oh.entry_rva && !check_entry(m)) {
        set_error("DLL/EXE entry not executable");return false;
    }
    uint32_t ndesc=m.pe.oh.import_size/20u;
    if(m.pe.oh.import_rva && (ndesc==0u || ndesc>128u)) {
        set_error("invalid DLL import directory");return false;
    }
    for(uint32_t n=0u;n<ndesc;++n) {
        pe::ImportDesc imp{};
        if(!pe::read_import_desc(m.pe,m.image,m.size,n,&imp))break;
        char dep[48];
        if(!pe::rva_string(m.image,m.size,imp.name_rva,dep,sizeof dep)) {
            set_error("invalid DLL import name");return false;
        }
        if(same_case(dep,"COREDLL.DLL"))continue;
        if(!valid_dll(dep)) {
            set_error("unsupported DLL dependency name");return false;
        }
        char dependency_path[NTOS_EXWORD_PATH_MAX];
        if(!resolve_dll(dep,m.path,dependency_path)) {
            set_error("dependency DLL missing from SYSROOT");return false;
        }
        unsigned child=0u;
        if(!load(dependency_path,dep,true,depth+1u,&child))return false;
    }
    return true;
}
static bool bind_imports(Module &m) {
    const uint32_t ndesc=m.pe.oh.import_size/20u;
    for(uint32_t n=0u;n<ndesc;++n) {
        pe::ImportDesc d{};
        if(!pe::read_import_desc(m.pe,m.image,m.size,n,&d))break;
        char dep[48];
        if(!pe::rva_string(m.image,m.size,d.name_rva,dep,sizeof dep) ||
           !d.ft_rva) {
            set_error("invalid PE import descriptor");return false;
        }
        bool native=same_case(dep,"COREDLL.DLL");
        int target=native?-1:find_module(dep);
        if(!native && target<0) {
            set_error("DLL not resident");return false;
        }
        uint32_t names=d.oft_rva?d.oft_rva:d.ft_rva;
        for(uint32_t i=0u;i<16384u;++i) {
            if(names>m.size || d.ft_rva>m.size ||
               i>m.size/4u || 4u>m.size-names || 4u>m.size-d.ft_rva ||
               4u*i>m.size-names-4u ||
               4u*i>m.size-d.ft_rva-4u) {
                set_error("invalid IAT array");return false;
            }
            const uint32_t thunk=pe::rd32le(m.image+names+4u*i);
            if(!thunk)break;
            const bool ordinal=!!(thunk&0x80000000u);
            const uint32_t ord=thunk&0xffffu;
            char symbol[100]={};
            if(!ordinal &&
               (thunk>m.size || m.size-thunk<3u ||
                !pe::rva_string(m.image,m.size,thunk+2u,
                                symbol,sizeof symbol))) {
                set_error("bad imported symbol");return false;
            }
            uint32_t resolved=0u;
            if(native) {
                if(ordinal) {
                    set_error("COREDLL ordinal not supported");return false;
                }
                resolved=ntos_exword_lookup_coredll(symbol);
            } else {
                const Module &provider=modules[target];
                uint32_t exported=0u;
                if(!pe::export_address(provider.pe,provider.image,
                    provider.size,ordinal?nullptr:symbol,ord,ordinal,
                    &exported)) {
                    set_error("DLL export missing");return false;
                }
                if(exported>=provider.pe.oh.export_rva &&
                   exported-provider.pe.oh.export_rva<
                     provider.pe.oh.export_size) {
                    set_error("DLL forwarded export not yet supported");
                    return false;
                }
                resolved=provider.base+exported;
            }
            if(!resolved) {
                set_error("missing native COREDLL function");return false;
            }
            pe::wr32be(m.image+d.ft_rva+4u*i,resolved);
        }
    }
    return true;
}
static bool init_one(unsigned id,unsigned depth) {
    if(depth>=MAX_MODULES)return false;
    Module &m=modules[id];
    if(m.init_state==2u)return true;
    if(m.init_state==1u) {
        set_error("cyclic DLL initialization");return false;
    }
    m.init_state=1u;
    uint32_t nd=m.pe.oh.import_size/20u;
    for(uint32_t n=0u;n<nd;++n) {
        pe::ImportDesc d{};
        if(!pe::read_import_desc(m.pe,m.image,m.size,n,&d))break;
        char name[48];
        if(!pe::rva_string(m.image,m.size,d.name_rva,name,sizeof name))
            return false;
        if(same_case(name,"COREDLL.DLL"))continue;
        int child=find_module(name);
        if(child<0 || !init_one((unsigned)child,depth+1u))return false;
    }
    if(m.dll && m.pe.oh.entry_rva) {
        int status=ntos_exword_invoke_dll(
            m.base+m.pe.oh.entry_rva,m.base,1u,g_guest_area.stack_end-16u);
        if(!status) {
            set_error("DLL_PROCESS_ATTACH failed");return false;
        }
        attach_order[attach_count++]=id;
    }
    m.init_state=2u;
    return true;
}
static void unload()
{
    while(attach_count) {
        const Module &m=modules[attach_order[--attach_count]];
        (void)ntos_exword_invoke_dll(m.base+m.pe.oh.entry_rva,
                                    m.base,0u,g_guest_area.stack_end-16u);
    }
}
} // namespace ntos_exword_modules
