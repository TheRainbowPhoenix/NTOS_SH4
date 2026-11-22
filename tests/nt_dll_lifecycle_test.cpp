#include "../src/nt/dll_lifecycle.hpp"
#include <cassert>
#include <cstdio>
struct Calls {
    unsigned entries[64]{};
    unsigned count=0u;
    int fail=0;
    static bool call(void *opaque,unsigned id,bool attach) {
        auto &c=*static_cast<Calls *>(opaque);
        c.entries[c.count++]=(attach?0x100u:0x200u)|id;
        return !attach || static_cast<int>(id)!=c.fail;
    }
};
int main() {
    nt::DllLifecycle<16> g{};
    assert(g.configure(6u)); // 0=EXE, modules 1..5
    // 1 depends on 2 and 3, 2 depends on 4, 3 on 5.
    assert(g.depends(1u,2u) && g.depends(1u,3u));
    assert(g.depends(2u,4u) && g.depends(3u,5u));
    for(unsigned i=1u;i<6u;++i)assert(g.entry(i,true));
    assert(g.plan() && g.cycle_breaks==0u && g.planned==5u);
    unsigned at[6]{};
    for(unsigned i=0u;i<g.planned;++i)at[g.order[i]]=i;
    assert(at[4]<at[2] && at[2]<at[1]);
    assert(at[5]<at[3] && at[3]<at[1]);
    Calls ok{};
    assert(g.attach(&Calls::call,&ok));
    assert(ok.count==5u);
    g.detach(&Calls::call,&ok);
    assert(ok.count==10u && g.detached==5u);
    for(unsigned i=0u;i<5u;++i)
        assert(ok.entries[i]==(0x100u|g.order[i]));
    for(unsigned i=0u;i<5u;++i)
        assert(ok.entries[5u+i]==(0x200u|g.order[4u-i]));
    assert(!g.depends(1u,1u) && !g.depends(1u,9u));

    assert(g.configure(4u));
    assert(g.depends(1u,2u) && g.depends(2u,3u));
    for(unsigned i=1u;i<4u;++i)assert(g.entry(i,true));
    assert(g.plan());
    Calls fail{};fail.fail=2;
    assert(!g.attach(&Calls::call,&fail));
    // GOOD 3 attached, FAIL 2 rejects, rollback GOOD 3 only.
    assert(fail.count==3u);
    assert(fail.entries[0]==0x103u &&
           fail.entries[1]==0x102u &&
           fail.entries[2]==0x203u);
    assert(g.failed==1u && g.attached==0u && g.detached==1u);
    g.detach(&Calls::call,&fail);
    assert(fail.count==3u); // exactly-once detach

    nt::DllLifecycle<16> cycle{};
    assert(cycle.configure(4u));
    assert(cycle.depends(1u,2u));
    assert(cycle.depends(2u,3u));
    assert(cycle.depends(3u,1u));
    for(unsigned i=1u;i<4u;++i)assert(cycle.entry(i,true));
    assert(cycle.plan() && cycle.cycle_breaks==1u);
    Calls cyclic{};
    assert(cycle.attach(&Calls::call,&cyclic));
    cycle.detach(&Calls::call,&cyclic);
    assert(cyclic.count==6u);
    std::puts("PASS DLL lifecycle: transitive dep-first attach, exact reverse detach, failed attach rollback, deterministic cycles");
}
