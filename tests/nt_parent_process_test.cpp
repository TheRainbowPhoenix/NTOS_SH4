#include "../src/nt/ke.hpp"
#include <cassert>
#include <cstdio>
int main() {
    nt::KernelContext k{};
    assert(k.begin(0x00400000u,0x20000u)==nt::SUCCESS);
    const uint32_t p=k.process.unique_process_id;
    assert(p==257u && k.process.inherited_from_process_id==0u);
    assert(k.enter_guest(0x00400100u)==nt::SUCCESS);k.end();
    assert(k.begin(0x00400000u,0x20000u,p)==nt::SUCCESS);
    assert(k.process.unique_process_id==p+1u &&
           k.process.inherited_from_process_id==p);
    assert(k.enter_guest(0x00400100u)==nt::SUCCESS);
    const uint32_t child=k.process.unique_process_id;k.end();
    assert(k.begin(0x00400000u,0x20000u,child)==nt::SUCCESS);
    assert(k.process.unique_process_id==child+1u &&
           k.process.inherited_from_process_id==child);
    k.end();
    std::puts("PASS Ps serial parent PID: child distinct, parent link, generations");
}
