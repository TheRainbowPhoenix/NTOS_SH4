#include "../exword/p1_vm.hpp"
#include <cassert>
#include <cstdio>
struct Stub {
    uint32_t cursor=0x8c780033u;
    unsigned zeros=0u,freed=0u;
};
static uint32_t alloc(uint32_t n,void *ctx) {
    Stub &s=*(Stub *)ctx;
    uint32_t addr=s.cursor;
    s.cursor+=n+4096u;
    return addr;
}
static void release(uint32_t,void *ctx){((Stub *)ctx)->freed++;}
static void zero(uint32_t, uint32_t n,void *ctx){
    assert(n==4096u);
    ((Stub *)ctx)->zeros++;
}
int main() {
    Stub state{};
    NtosP1Vm vm{};
    vm.begin(alloc,release,zero,&state);
    assert(vm.reserve(0u,0u)==0u);
    assert(vm.reserve(0x1234u,12u)==0u);
    assert(vm.reserve(0u,65u)==0u);
    uint32_t base=vm.reserve(0u,12u);
    assert(base!=0u && !(base&4095u));
    assert(vm.query(base)==0u && vm.committed()==0u);
    assert(vm.change(base,8u,2u,true)==0u && vm.committed()==8u);
    assert(state.zeros==8u);
    for(uint32_t i=0u;i<8u;++i) {
        assert(vm.hint(base+4096u*i)==0);
        assert(vm.query(base+4096u*i)==2u);
    }
    assert(vm.change(base,4u,1u,false)==0u);
    assert(vm.query(base)==1u && vm.query(base+16384u)==2u);
    assert(vm.change(base,4u,2u,false)==0u);
    assert(vm.change(base+8u*4096u,1u,1u,false)!=0u);
    assert(vm.release(base)!=0u && vm.committed()==8u);
    uint32_t other=vm.reserve(0u,6u);
    assert(other!=0u && other!=base);
    assert(vm.release(other)==0u && state.freed==1u);
    assert(vm.query(0x08100000u)==(uint32_t)-1);
    std::puts("PASS Exword P1-backed VMTEST reserve, commit, query, protect, hint");
}
