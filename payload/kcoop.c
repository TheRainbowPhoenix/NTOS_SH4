/* KCOOP.EXE: two genuine SH7305 cooperative execution stacks.
 * Not full kernel scheduling, does not create new KTHREAD objects.
 * Expected 0x4B434F31: 32 A/B alternations, correct counters and trace.
 */
#include <stdint.h>
#include <stddef.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
struct Fiber {
    u32 r8_to_r14[7];
    u32 sp,pr,gbr,mach,macl,sr;
};
_Static_assert(sizeof(struct Fiber)==52u,"assembly context layout");
_Static_assert(offsetof(struct Fiber,sp)==28u,"SP offset");
_Static_assert(offsetof(struct Fiber,sr)==48u,"SR offset");
extern void ntos_coop_switch(struct Fiber *from,const struct Fiber *to);
static struct Fiber host,a,b;
static uint8_t a_stack[4096] __attribute__((aligned(16)));
static uint8_t b_stack[4096] __attribute__((aligned(16)));
static volatile u32 order[64],order_index,done_a,done_b,count_a,count_b,error;
static void worker_a(void) {
    for(u32 i=0;i<32u;++i) {
        if(order_index!=2u*i)error=1u;
        order[order_index++]=0xA000u+i;
        ++count_a;
        ntos_coop_switch(&a,&b);
    }
    done_a=1u;
    ntos_coop_switch(&a,&host);
    for(;;) {}
}
static void worker_b(void) {
    for(u32 i=0;i<32u;++i) {
        if(order_index!=2u*i+1u)error=2u;
        order[order_index++]=0xB000u+i;
        ++count_b;
        ntos_coop_switch(&b,&a);
    }
    done_b=1u;
    ntos_coop_switch(&b,&host);
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,65u))return (int)0xBAD9F001u;
    u32 sr=0u,gbr=0u;
    __asm__ volatile("stc sr,%0\n\tstc gbr,%1"
                   : "=r"(sr),"=r"(gbr) :: "memory");
    if((sr&0x70000000u)!=0x40000000u)
        return (int)0xBAD9F002u;
    a.sp=(u32)(uintptr_t)(a_stack+sizeof(a_stack));
    b.sp=(u32)(uintptr_t)(b_stack+sizeof(b_stack));
    a.pr=(u32)(uintptr_t)&worker_a;
    b.pr=(u32)(uintptr_t)&worker_b;
    a.sr=b.sr=sr;a.gbr=b.gbr=gbr;
    ntos_coop_switch(&host,&a);
    if(!done_a || done_b || order_index!=64u)
        return (int)0xBAD9F003u;
    ntos_coop_switch(&host,&b);
    if(!done_b || count_a!=32u || count_b!=32u || error)
        return (int)0xBAD9F004u;
    for(u32 i=0u;i<32u;++i)
        if(order[2u*i]!=0xA000u+i ||
           order[2u*i+1u]!=0xB000u+i)
            return (int)0xBAD9F005u;
    return (int)0x4B434F31u;
}
