/* MMRACE.EXE: two real Ke-owned cooperative KTHREADs, one process.
 * 500 alternating decommit/recommit rounds EACH; same-VA root.
 * GATE: test only after MMREUSE==4D444331 & K2THREAD==4B325431.
 * Expected 0x4D524335. Never run on unvalidated bootloader.
 */
#include <stdint.h>
#include "ntapi_caps.h"
typedef uint32_t u32;
typedef u32 (*query_fn)(void);
typedef u32 (*create_fn)(u32,u32);
typedef u32 (*alloc_fn)(u32,u32 *,u32,u32 *,u32,u32);
typedef u32 (*free_fn)(u32,u32 *,u32 *,u32);
typedef int32_t (*hint_fn)(u32);
static volatile u32 base[2] __attribute__((aligned(4096)));
static volatile u32 count[2],error,finished,cycles[2],tid[2];
static uint8_t worker_stack[4096] __attribute__((aligned(4096)));
static query_fn get_tid,get_pid,do_yield,do_exit,switch_count;
static alloc_fn allocate;
static free_fn free_;
static hint_fn hint;
enum { CURRENT=0xFFFFFFFFu,COMMIT=0x1000u,RESERVE=0x2000u,
       DECOMMIT=0x4000u,RW=4u };
static u32 cycle(u32 which,u32 round) {
    volatile uint8_t *const p=(volatile uint8_t *)(uintptr_t)base[which];
    volatile uint8_t *const neighbor=(volatile uint8_t *)(uintptr_t)base[which^1u];
    if(get_tid()!=tid[which] || !get_pid())return 0xBADAD001u+which;
    if(round) {
        const u32 other_round=which==0u ? round-1u : round;
        const uint8_t expect=(uint8_t)((which==0u?0x80u:0x20u)+other_round);
        if(neighbor[0]!=expect)return 0xBADAD003u+which;
    }
    const uint8_t value=(uint8_t)((round*29u+which*51u+7u)&255u);
    p[0]=value;p[4095]=(uint8_t)~value;
    count[which]=4096u;
    if(free_(CURRENT,(u32 *)(uintptr_t)&base[which],
             (u32 *)(uintptr_t)&count[which],DECOMMIT)!=0u)
        return 0xBADAD010u+which;
    count[which]=4096u;
    if(allocate(CURRENT,(u32 *)(uintptr_t)&base[which],0u,
                (u32 *)(uintptr_t)&count[which],COMMIT,RW)!=0u ||
       hint(base[which])!=0)
        return 0xBADAD020u+which;
    if(p[0]!=0u)return 0xBADAD030u+which;
    if(p[4095]!=0u)return 0xBADAD040u+which;
    p[0]=(uint8_t)((which==0u?0x20u:0x80u)+round);
    ++cycles[which];
    return 0u;
}
static void worker(void) {
    for(u32 i=0u;i<500u && !error;++i) {
        error=cycle(1u,i);
        if(do_yield()!=0u)error=0xBADAD051u;
    }
    finished=1u;
    (void)do_exit();
    for(;;) {}
}
int main(const void *const *api,char **argv,char **envp) {
    (void)argv;(void)envp;
    const volatile u32 *const w=(const volatile u32 *)0x08100000u;
    if(!api || !ntos_api_has_slot(w,70u))return (int)0xBADAD060u;
    get_pid=(query_fn)api[55];get_tid=(query_fn)api[56];
    allocate=(alloc_fn)api[51];free_=(free_fn)api[53];
    hint=(hint_fn)api[23];
    create_fn create=(create_fn)api[67];
    do_yield=(query_fn)api[68];do_exit=(query_fn)api[69];
    switch_count=(query_fn)api[70];
    if(!get_pid||!get_tid||!allocate||!free_||!hint||
       !create||!do_yield||!do_exit||!switch_count)
        return (int)0xBADAD061u;
    if(hint((u32)(uintptr_t)&base[0])!=0 ||
       hint((u32)(uintptr_t)&base[1])!=0 ||
       hint((u32)(uintptr_t)&count[0])!=0 ||
       hint((u32)(uintptr_t)&count[1])!=0)
        return (int)0xBADAD062u;
    tid[0]=get_tid();
    if(!tid[0])return (int)0xBADAD063u;
    for(u32 i=0u;i<2u;++i) {
        base[i]=0u;count[i]=4096u;
        if(allocate(CURRENT,(u32 *)(uintptr_t)&base[i],0u,
                    (u32 *)(uintptr_t)&count[i],RESERVE|COMMIT,RW)!=0u ||
           !base[i] || hint(base[i])!=0)
            return (int)(0xBADAD064u+i);
    }
    if(base[0]==base[1] || hint((u32)(uintptr_t)&worker_stack[4095])!=0 ||
       hint((u32)(uintptr_t)&worker)!=0)
        return (int)0xBADAD066u;
    tid[1]=create((u32)(uintptr_t)&worker,
                   (u32)(uintptr_t)(worker_stack+sizeof(worker_stack)));
    if(tid[1]!=tid[0]+1u)return (int)0xBADAD067u;
    for(u32 i=0u;i<500u && !error;++i) {
        error=cycle(0u,i);
        if(do_yield()!=0u)error=0xBADAD068u;
    }
    if(do_yield()!=0u)return (int)0xBADAD069u;
    if(error)return (int)error;
    if(!finished || cycles[0]!=500u || cycles[1]!=500u ||
       get_tid()!=tid[0] || switch_count()!=1002u)
        return (int)0xBADAD06Au;
    return (int)0x4D524335u;
}
