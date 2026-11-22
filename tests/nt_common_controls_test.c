/* Host-only bounded WinCE common-control model and handle regression. */
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef struct {int left,top,right,bottom;} NT_RECT;
#include "../payload/wince/nt_common_controls.h"

static const u16 button[]={'B','U','T','T','O','N',0};
static const u16 label[]={'S','T','A','T','I','C',0};
static const u16 progress[]={'m','s','c','t','l','s','_','p','r','o','g','r','e','s','s','3','2',0};
static const u16 listbox[]={'L','i','s','t','B','o','x',0};
static const u16 content[]={'H','e','l','l','o',0};

int main(void) {
    assert(nt_ctl_kind(button,0)==NT_CTL_BUTTON);
    assert(nt_ctl_kind(button,3u)==NT_CTL_CHECKBOX);
    assert(nt_ctl_kind(label,0)==NT_CTL_STATIC);
    assert(nt_ctl_kind(progress,0)==NT_CTL_PROGRESS);
    assert(nt_ctl_kind(listbox,0)==NT_CTL_LISTBOX);
    int parent=5;
    NT_COMMON_CTL *b=nt_ctl_create(NT_CTL_BUTTON,&parent,27,0,4,8,100,36,content);
    NT_COMMON_CTL *check=nt_ctl_create(NT_CTL_CHECKBOX,&parent,28,3,4,52,100,36,content);
    assert(b && check && b!=check);
    void *old=(void *)(uintptr_t)b->handle;
    assert(nt_ctl_find(old)==b);
    assert(nt_ctl_count(b)==-1);
    assert(nt_ctl_add_string(b,content)==-1);
    assert(!nt_ctl_hit(&parent,12,12));
    b->visible=1;check->visible=1;
    assert(nt_ctl_hit(&parent,12,12)==b);
    assert(nt_ctl_hit(&parent,12,55)==check);
    assert(!nt_ctl_hit(&parent,-1,12));
    assert(nt_ctl_command(b));
    assert(!b->checked);
    assert(nt_ctl_command(check) && check->checked);
    assert(nt_ctl_command(check) && !check->checked);
    nt_ctl_focus(check);
    assert(check->focus && !b->focus);
    nt_ctl_focus(b);
    assert(b->focus && !check->focus);
    assert(nt_ctl_destroy(old));
    assert(!nt_ctl_find(old));
    NT_COMMON_CTL *again=nt_ctl_create(NT_CTL_BUTTON,&parent,30,0,0,0,50,30,0);
    assert(again && (void *)(uintptr_t)again->handle!=old);
    assert(!nt_ctl_destroy(old));
    NT_COMMON_CTL *lb=nt_ctl_create(NT_CTL_LISTBOX,&parent,40,0,10,100,170,100,0);
    assert(lb);
    for(int i=0;i<8;++i)assert(nt_ctl_add_string(lb,content)==i);
    assert(nt_ctl_add_string(lb,content)==-1);
    assert(nt_ctl_select(lb,7)==7);
    assert(lb->selected==7u);
    assert(nt_ctl_select(lb,8)==-1);
    assert(nt_ctl_select(lb,-1)==-1 && lb->selected==0xffffffffu);
    for(int i=0;i<5;++i)assert(nt_ctl_create(NT_CTL_STATIC,&parent,100+i,0,0,0,10,10,content));
    assert(!nt_ctl_create(NT_CTL_STATIC,&parent,200,0,0,0,10,10,content));
    nt_ctl_destroy_parent(&parent);
    for(unsigned i=0;i<NT_CTL_MAX;++i)assert(!g_common_controls[i].handle);
    assert(!nt_ctl_find(old));
    return 0;
}
