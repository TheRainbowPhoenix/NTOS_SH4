/* Native EX-word FS calls require gint_world_switch(), including close.
 * Borrow the syscall ABI/semantics used by the Edelweiss EX-word host. */
#include "storage.h"
#include <gint/exword.h>
#include <gint/gint.h>
#include <stdint.h>
#include <stddef.h>

static int ntos_path_ok(const char *path)
{
    if(!path || path[0]!='\\' || path[1]!='\\')return 0;
    const char *root=path+2;
    if(!((root[0]=='d'&&root[1]=='r'&&root[2]=='v'&&root[3]=='0') ||
         (root[0]=='c'&&root[1]=='r'&&root[2]=='d'&&root[3]=='0')))
        return 0;
    if(root[4]!='\\' && root[4]!=0)return 0;
    unsigned n=0;
    while(path[n]) {
        if(++n>=NTOS_EXWORD_PATH_MAX)return 0;
        if(path[n-1]=='/' || (path[n-1]=='.' && path[n]=='.'))
            return 0;
    }
    return 1;
}
struct ntos_scan {
    const char *pattern;
    struct ntos_exword_listing *listing;
};
static int ntos_scan_native(struct ntos_scan *scan)
{
    char found[512]={0};
    int handle=-1;
    unsigned long kind=0;
    int rc=exword_sys_findfirst(scan->pattern,&handle,found,&kind);
    if(rc<0)return rc;
    struct ntos_exword_listing *out=scan->listing;
    for(;;) {
        if(out->count>=NTOS_EXWORD_LIST_MAX)break;
        char *dst=out->names[out->count];
        unsigned n=0;
        for(;n+1u<NTOS_EXWORD_NAME_MAX && found[n];++n) {
            unsigned char ch=(unsigned char)found[n];
            dst[n]=(ch>=32u && ch<127u)?(char)ch:'?';
        }
        dst[n]=0;
        out->kinds[out->count]=(uint32_t)kind;
        ++out->count;
        found[0]=0;
        kind=0;
        rc=exword_sys_findnext(handle,found,&kind);
        if(rc<0)break;
    }
    int close=exword_sys_findclose(handle);
    return close<0?close:0;
}
int ntos_exword_scan_path(const char *pattern, struct ntos_exword_listing *out)
{
    if(!out || !ntos_path_ok(pattern))return -1;
    out->count=0u;
    out->result=0;
    struct ntos_scan q={pattern,out};
    out->result=gint_world_switch(GINT_CALL(ntos_scan_native,(void *)&q));
    return out->result;
}
int ntos_exword_scan(unsigned volume, struct ntos_exword_listing *out)
{
    return ntos_exword_scan_path(ntos_exword_pattern(volume),out);
}
struct ntos_file {
    const char *path;
    uint32_t offset;
    uint32_t size;
    uint8_t *buffer;
};
static int ntos_size_native(struct ntos_file *q)
{
    int fd=exword_sys_open(q->path,1);
    if(fd<0)return fd;
    int size=exword_sys_get_filesize(fd);
    int close=exword_sys_close(fd);
    if(size<0)return size;
    if(close<0)return close;
    q->size=(uint32_t)size;
    return 0;
}
int ntos_exword_file_size(const char *path, uint32_t *size)
{
    if(!size || !ntos_path_ok(path))return -1;
    struct ntos_file q={path,0u,0u,NULL};
    int rc=gint_world_switch(GINT_CALL(ntos_size_native,(void *)&q));
    if(rc==0)*size=q.size;
    return rc;
}
static int ntos_read_native(struct ntos_file *q)
{
    int fd=exword_sys_open(q->path,1);
    if(fd<0)return fd;
    int fsize=exword_sys_get_filesize(fd);
    int rc=-1;
    if(fsize>=0 && q->offset<=(uint32_t)fsize &&
       q->size<=(uint32_t)fsize-q->offset &&
       q->offset<=0x7fffffffu && q->size<=0x7fffffffu &&
       exword_sys_seek(fd,(int)q->offset,0)>=0) {
        uint32_t done=0;
        rc=0;
        while(done<q->size) {
            uint32_t part=q->size-done;
            if(part>2048u)part=2048u;
            int n=exword_sys_read(fd,q->buffer+done,(int)part);
            if(n<=0 || (uint32_t)n>part) {rc=-1;break;}
            done+=(uint32_t)n;
        }
    }
    int close=exword_sys_close(fd);
    return rc<0?rc:close<0?close:0;
}
int ntos_exword_read_at(const char *path, uint32_t offset, void *out, uint32_t count)
{
    if(!ntos_path_ok(path) || (!out && count) || count>0x200000u)return -1;
    if(!count)return 0;
    struct ntos_file q={path,offset,count,(uint8_t *)out};
    return gint_world_switch(GINT_CALL(ntos_read_native,(void *)&q));
}

/* Keep native descriptors within the firmware world. Calling any of these
 * EX-word syscall wrappers without gint_world_switch breaks the MMU/VBR
 * ownership contract when NTOS's PE image is resident in P1. */
struct ntos_stream_req {
    const char *path;
    int fd,offset,method;
    void *buffer;
    uint32_t count;
};
static int ntos_stream_open(struct ntos_stream_req *q)
{ return exword_sys_open(q->path,1); }
static int ntos_stream_read(struct ntos_stream_req *q)
{ return exword_sys_read(q->fd,q->buffer,(int)q->count); }
static int ntos_stream_seek(struct ntos_stream_req *q)
{ return exword_sys_seek(q->fd,q->offset,q->method); }
static int ntos_stream_size(struct ntos_stream_req *q)
{ return exword_sys_get_filesize(q->fd); }
static int ntos_stream_close(struct ntos_stream_req *q)
{ return exword_sys_close(q->fd); }
int ntos_exword_open_read(const char *path)
{
    if(!ntos_path_ok(path))return -1;
    struct ntos_stream_req q={0};
    q.path=path;
    return gint_world_switch(GINT_CALL(ntos_stream_open,(void *)&q));
}
int ntos_exword_read_fd(int fd,void *out,uint32_t count)
{
    if(fd<0 || (!out && count) || count>4096u)return -1;
    struct ntos_stream_req q={0};
    q.fd=fd;q.buffer=out;q.count=count;
    return gint_world_switch(GINT_CALL(ntos_stream_read,(void *)&q));
}
int ntos_exword_seek_fd(int fd,int off,uint32_t method)
{
    if(fd<0 || method>2u)return -1;
    struct ntos_stream_req q={0};
    q.fd=fd;q.offset=off;q.method=(int)method;
    return gint_world_switch(GINT_CALL(ntos_stream_seek,(void *)&q));
}
int ntos_exword_size_fd(int fd)
{
    if(fd<0)return -1;
    struct ntos_stream_req q={0};q.fd=fd;
    return gint_world_switch(GINT_CALL(ntos_stream_size,(void *)&q));
}
int ntos_exword_close_fd(int fd)
{
    if(fd<0)return -1;
    struct ntos_stream_req q={0};q.fd=fd;
    return gint_world_switch(GINT_CALL(ntos_stream_close,(void *)&q));
}
