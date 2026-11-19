/* POOLTEST.EXE: tagged ExPool allocation, randomized release and hard limits.
 * No SDK filesystem calls. Expected 0x50010200 with a 512 KiB peak.
 * Low 16 bits report peak tracked KiB, not physical heap usage. */
#include <stdint.h>
typedef uint32_t u32;
typedef void *(*alloc_fn)(u32,u32);
typedef u32 (*free_fn)(void *,u32);
typedef u32 (*count_fn)(void);
#define TAG 0x4C4F4F50u
#define OK 0u
#define BAD 0xC000000Du
int main(const void *const *api,char **argv,char **envp)
{
    (void)argv;(void)envp;
    if(!api)return 0xBAD20001u;
    alloc_fn alloc=(alloc_fn)api[33];
    free_fn release=(free_fn)api[34];
    count_fn peak=(count_fn)api[35],live=(count_fn)api[36],count=(count_fn)api[37];
    if(!alloc||!release||!peak||!live||!count)return 0xBAD20002u;
    if(live()!=0u || count()!=0u)return 0xBAD20003u;
    void *block[128]={0};
    u32 length[128]={0},order[128]={0};
    u32 rng=0x3278FDD1u;
    for(u32 round=0u;round<4u;++round) {
        u32 expected=0u;
        for(u32 i=0u;i<64u;++i) {
            u32 n=1u+(i*131u+round*1091u)%8192u;
            block[i]=alloc(n,TAG);
            if(!block[i])return 0xBAD20004u;
            length[i]=n;order[i]=i;expected+=n;
            volatile uint8_t *p=(volatile uint8_t *)block[i];
            p[0]=(uint8_t)(0x31u^i^round);
            p[n-1u]=(uint8_t)(0xB7u^i^round);
        }
        if(live()!=expected)return 0xBAD20005u;
        for(u32 i=63u;i>0u;--i) {
            rng=rng*1664525u+1013904223u;
            u32 j=rng%(i+1u),tmp=order[i];
            order[i]=order[j];order[j]=tmp;
        }
        for(u32 i=0u;i<64u;++i) {
            u32 j=order[i],n=length[j];
            volatile uint8_t *p=(volatile uint8_t *)block[j];
            if(n>1u && p[0]!=(uint8_t)(0x31u^j^round))
                return 0xBAD20006u;
            if(p[n-1u]!=(uint8_t)(0xB7u^j^round))
                return 0xBAD20007u;
            if(release(block[j],TAG^1u)!=BAD)return 0xBAD20008u;
            if(release(block[j],TAG)!=OK)return 0xBAD20009u;
            expected-=n;
            if(live()!=expected)return 0xBAD2000Au;
            block[j]=0;
        }
    }
    if(alloc(0u,TAG) || alloc(0xFFFFFFFFu,TAG))
        return 0xBAD2000Bu;
    for(u32 i=0u;i<64u;++i) {
        block[i]=alloc(8192u,TAG);
        if(!block[i])return 0xBAD2000Cu;
        volatile uint8_t *p=(volatile uint8_t *)block[i];
        p[0]=(uint8_t)i;p[8191]=(uint8_t)(i^0xA5u);
    }
    if(live()!=524288u || peak()<524288u)return 0xBAD2000Du;
    for(u32 i=64u;i>0u;--i) {
        u32 j=i-1u;
        volatile uint8_t *p=(volatile uint8_t *)block[j];
        if(p[0]!=(uint8_t)j || p[8191]!=(uint8_t)(j^0xA5u))
            return 0xBAD2000Eu;
        if(release(block[j],TAG)!=OK)return 0xBAD2000Fu;
    }
    if(live()!=0u)return 0xBAD20010u;
    // All 128 pool tracking slots, without exhausting physical RAM.
    for(u32 i=0u;i<128u;++i)
        if(!(block[i]=alloc(i+1u,TAG)))return 0xBAD20011u;
    if(alloc(1u,TAG)!=0)return 0xBAD20012u;
    for(u32 i=0u;i<128u;++i)
        if(release(block[i],TAG)!=OK)return 0xBAD20013u;
    if(release(block[127],TAG)!=BAD || live()!=0u ||
       count()!=0u)return 0xBAD20014u;
    return 0x50010000u|((peak()>>10u)&0xFFFFu);
}
