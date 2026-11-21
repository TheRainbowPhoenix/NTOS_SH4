/* NTOSH: native stage-0 port of the NTOS executive to EX-word SH7305.
 *
 * Uses the *same* src/nt/Process, Ke, Ob, Ex event/pool and Io definitions
 * as the CP400 build. EX-word gint owns the host MMU transition and must
 * restore the firmware world on exit. Do NOT apply the CP400 UTLB layout
 * or disable translation while gint/OS code needs 0x7000/0x7400 windows.
 *
 * This first D01 is a boot/FS readiness monitor, not a PE guest executor.
 * Display is EX-word landscape 528x320, hardware-native RGB565.
 */
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdint.h>
#include <stdlib.h>
#include "storage.h"
#include "../src/nt/process.hpp"

static nt::Process g_kernel;
static ntos_exword_listing g_listing;
static unsigned g_volume;
static bool g_ready;

static void *pool_alloc(void *,size_t n) {return malloc(n);}
static void pool_free(void *,void *p) {free(p);}

static bool init_kernel()
{
    if(!g_kernel.begin())return false;
    /* The native D01 has a loader image, not a user P0 process. We only
     * initialize Ke's bookkeeping; never call enter_guest/dispatch here. */
    if(g_kernel.ke.begin(0x70000080u,0x20000u)!=nt::SUCCESS)return false;
    uint32_t handle=0u;
    if(g_kernel.create_event(true,false,&handle)!=nt::SUCCESS)return false;
    const bool event_ok=
        g_kernel.event_set(handle)==nt::SUCCESS &&
        g_kernel.event_poll(handle,0u)==nt::SUCCESS &&
        g_kernel.handles.close(handle,nt::Type::event)==nt::SUCCESS;
    if(!event_ok)return false;
    g_kernel.pool.configure(nullptr,&pool_alloc,&pool_free);
    const uint32_t tag=nt::tag('E','X','W','D');
    void *p=g_kernel.pool.alloc(32u,tag);
    if(!p)return false;
    return g_kernel.pool.free(p,tag)==nt::SUCCESS;
}
static void append(char *out,unsigned capacity,const char *text)
{
    unsigned i=0u;
    while(i+1u<capacity && out[i])++i;
    for(unsigned j=0u;i+1u<capacity && text[j];++i,++j)out[i]=text[j];
    out[i]=0;
}
static void hex8(char out[9],uint32_t v)
{
    static const char digits[]="0123456789ABCDEF";
    for(unsigned i=0u;i<8u;++i)
        out[i]=digits[(v>>(28u-4u*i))&15u];
    out[8]=0;
}
static void draw()
{
    dclear(C_WHITE);
    drect(0,0,DWIDTH-1,28,0x2945);
    dtext(9,8,C_WHITE,"NTOSH  |  NTOS for EX-word SH4");
    dtext(10,40,C_BLACK,"Native D01 stage 0  /  528 x 320 RGB565");
    dtext(10,58,C_BLACK,g_ready?
        "Ke/Ps + Ob/Ex: initialized (no guest yet)" :
        "Kernel model init FAILED; press EXIT");
    dtext(10,76,C_BLACK,"MMU: EX-word gint/firmware-managed");
    dtext(10,94,C_BLACK,"Read-only filesystem inspection:");
    drect(6,113,DWIDTH-7,137,0xd6da);
    dtext(12,120,C_BLACK,g_volume==0u?
        "Volume: \\\\drv0   [LEFT/RIGHT to change]" :
        "Volume: \\\\crd0   [LEFT/RIGHT to change]");
    if(g_listing.result<0) {
        char code[9];hex8(code,(uint32_t)g_listing.result);
        char err[58]="Cannot enumerate volume: 0x";
        append(err,sizeof err,code);
        dtext(12,151,C_BLACK,err);
    } else if(!g_listing.count) {
        dtext(12,151,C_BLACK,"No entries found");
    } else {
        for(unsigned i=0u;i<g_listing.count;++i) {
            char line[78]={0};
            append(line,sizeof line,g_listing.kinds[i]==5u?"[DIR]  ":"[FILE] ");
            append(line,sizeof line,g_listing.names[i]);
            dtext(12,148+(int)i*17,C_BLACK,line);
        }
    }
    drect(0,297,DWIDTH-1,DHEIGHT-1,0x2945);
    dtext(9,304,C_WHITE,"EXE refresh  |  LEFT/RIGHT volume  |  EXIT return");
    dupdate();
}
int main(void)
{
    /* Gint's _exword_entry / mmu_exword_prepare first transition from the
     * OS-owned mappings to the P1 runtime. No manual MMUCR writes here. */
    g_ready=init_kernel();
    if(g_ready)ntos_exword_scan(g_volume,&g_listing);
    for(;;) {
        draw();
        key_event_t e=getkey();
        if(e.type!=KEYEV_DOWN && e.type!=KEYEV_HOLD)continue;
        if(e.key==KEY_EXIT || e.key==KEY_ACON)break;
        if(!g_ready)continue;
        if(e.key==KEY_LEFT || e.key==KEY_RIGHT)
            g_volume^=1u;
        else if(e.key!=KEY_EXE)continue;
        ntos_exword_scan(g_volume,&g_listing);
    }
    /* No guest mappings were installed. gint restores saved OS state
     * after main returns, unlike CP400's private UTLB handoff. */
    g_kernel.finish();
    return 0;
}
