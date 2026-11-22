#include "../exword/native_vm.hpp"
#include <cassert>
#include <cstdio>
struct Mock {
    uint32_t cells[4]{};
    uint32_t next=0x8c710000u,zeroes=0u,releases=0u;
};
static uint32_t alloc(uint32_t bytes,void *ctx) {
    Mock &m=*(Mock *)ctx;
    const uint32_t result=m.next;
    m.next+=bytes+4096u;
    return result;
}
static void release(uint32_t,void *ctx){((Mock *)ctx)->releases++;}
static void clear(uint32_t,uint32_t n,void *ctx){
    assert(n==4096u);
    ((Mock *)ctx)->zeroes++;
}
static bool read(void *ctx,uint32_t at,uint32_t *out){
    if(at<0x10000000u || at>=0x10000010u || (at&3u))return false;
    *out=((Mock *)ctx)->cells[(at-0x10000000u)/4u];
    return true;
}
static bool write(void *ctx,uint32_t at,uint32_t value){
    if(at<0x10000000u || at>=0x10000010u || (at&3u))return false;
    ((Mock *)ctx)->cells[(at-0x10000000u)/4u]=value;
    return true;
}
int main() {
    Mock m{};
    NtosP1Vm vm{};
    vm.begin(alloc,release,clear,&m);
    NtosExwordNativeVm api{};
    api.begin(&vm,read,write,&m);
    using N=NtosExwordNativeVm;
    constexpr uint32_t base=0x10000000u,size=base+4u,old=base+8u;
    m.cells[0]=0;m.cells[1]=8192u;
    assert(api.allocate(N::CURRENT,base,0u,size,N::RESERVE|N::COMMIT,N::PAGE_RW)==0u);
    const uint32_t region=m.cells[0];
    assert(region && m.cells[1]==8192u && vm.committed()==2u);
    assert(vm.query(region)==2u && m.zeroes==2u);
    m.cells[1]=4096u;
    assert(api.protect(N::CURRENT,base,size,N::PAGE_RO,old)==0u);
    assert(m.cells[2]==N::PAGE_RW && vm.query(region)==1u);
    assert(api.protect(N::CURRENT,base,size,N::PAGE_RW,old)==0u);
    assert(m.cells[2]==N::PAGE_RO && vm.query(region)==2u);
    m.cells[1]=0u;
    assert(api.free(N::CURRENT,base,size,N::RELEASE)==N::DEVICE_BUSY);
    assert(m.cells[0]==region);
    m.cells[1]=4096u;
    assert(api.free(N::CURRENT,base,size,N::DECOMMIT)==0u);
    assert(vm.committed()==1u && vm.query(region)==0u);
    assert(api.allocate(N::CURRENT,base,0u,size,N::COMMIT,N::PAGE_RW)==0u);
    assert(vm.query(region)==2u && m.zeroes==3u);
    m.cells[0]=0;m.cells[1]=4096u;
    assert(api.allocate(N::CURRENT,base,0u,size,N::RESERVE,N::PAGE_RO)==0u);
    assert(m.cells[0] && m.cells[0]!=region && vm.query(m.cells[0])==0u);
    m.cells[1]=0u;
    assert(api.free(N::CURRENT,base,size,N::RELEASE)==0u && m.cells[0]==0u);
    assert(m.releases==1u);
    m.cells[0]=0;m.cells[1]=4096u;
    assert(api.allocate(N::CURRENT,0x20000000u,0u,size,
                        N::RESERVE,N::PAGE_RW)==N::INVALID);
    assert(api.allocate(4u,base,0u,size,N::RESERVE,N::PAGE_RW)==N::INVALID);
    puts("PASS Exword NtAllocate/NtProtect/NtFree P1 Native ABI + pointer validation");
}
