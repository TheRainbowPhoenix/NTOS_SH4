/* Dedicated EX-word/native-world filesystem bridge. The D01 currently
 * only enumerates; no create/delete/rename/write is performed.
 */
#include "storage.h"
#include <gint/exword.h>
#include <gint/gint.h>
#include <stddef.h>

struct ntos_scan {
    const char *pattern;
    struct ntos_exword_listing *listing;
};
static int ntos_scan_native(struct ntos_scan *scan)
{
    char found[512] = {0};
    int handle = -1;
    unsigned long kind = 0;
    int rc = exword_sys_findfirst(scan->pattern, &handle, found, &kind);
    if(rc < 0)return rc;
    struct ntos_exword_listing *out=scan->listing;
    for(;;) {
        if(out->count >= NTOS_EXWORD_LIST_MAX)break;
        char *dst=out->names[out->count];
        unsigned n=0u;
        for(;n+1u<NTOS_EXWORD_NAME_MAX && found[n];++n) {
            const unsigned char ch=(unsigned char)found[n];
            dst[n]=(ch>=32u && ch<127u)?(char)ch:'?';
        }
        dst[n]=0;
        out->kinds[out->count]=(uint32_t)kind;
        ++out->count;
        found[0]=0;
        kind=0;
        rc=exword_sys_findnext(handle,found,&kind);
        if(rc<0)break; /* enumeration terminates with OS error code */
    }
    int closed=exword_sys_findclose(handle);
    return closed<0?closed:0;
}
int ntos_exword_scan(unsigned volume, struct ntos_exword_listing *out)
{
    const char *pattern=ntos_exword_pattern(volume);
    if(!out || !pattern)return -1;
    out->count=0u;out->result=0;
    struct ntos_scan query={pattern,out};
    /* EX-word firmware owns the file handles and its own MMU/VBR world. */
    out->result=gint_world_switch(GINT_CALL(ntos_scan_native,(void *)&query));
    return out->result;
}
