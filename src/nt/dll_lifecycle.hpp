// Windows CE-like DLL_PROCESS_ATTACH/DETACH with dependency-first ordering.
//
// Loader registers the complete PE import/forward graph before calling us.
// Topological selection runs dependencies before dependents; import cycles
// are broken in stable registry order and reported (not silently recursed).
// Every successful DllMain attach is remembered in a LIFO stack, so normal
// cleanup and an attach failure both detach the EXACT reverse attach order.
// Failed DllMain is NOT detached; EXE entry is forbidden on failure.
//
// Freestanding, allocation-free, no SDK, no output from guest-world calls.
#pragma once
#include <stdint.h>
namespace nt {
template<unsigned CAPACITY=16u>
struct DllLifecycle {
    static_assert(CAPACITY>=2u && CAPACITY<=32u);
    bool needs[CAPACITY][CAPACITY]{};
    bool has_entry[CAPACITY]{};
    bool used[CAPACITY]{};
    unsigned order[CAPACITY]{};
    unsigned stack[CAPACITY]{};
    unsigned total=0u,planned=0u,attached=0u,detached=0u,failed=0u;
    unsigned cycle_breaks=0u;
    bool valid=false;

    bool configure(unsigned module_count) {
        if(module_count==0u || module_count>CAPACITY)return false;
        *this=DllLifecycle{};
        total=module_count;
        return true;
    }
    bool depends(unsigned module,unsigned prerequisite) {
        if(module==0u || prerequisite==0u || module>=total ||
           prerequisite>=total || module==prerequisite)return false;
        needs[module][prerequisite]=true;
        return true;
    }
    bool entry(unsigned module,bool present) {
        if(module==0u || module>=total)return false;
        has_entry[module]=present;
        return true;
    }
    bool plan() {
        if(!total || valid)return false;
        // Kahn-like deterministic dependency ordering, without recursion.
        // Cycle break is explicit and counted so tests can detect it.
        for(unsigned count=1u;count<total;++count) {
            int chosen=-1;
            for(unsigned i=1u;i<total;++i) {
                if(used[i])continue;
                bool ready=true;
                for(unsigned j=1u;j<total;++j)
                    if(needs[i][j] && !used[j])ready=false;
                if(ready){chosen=static_cast<int>(i);break;}
            }
            if(chosen<0) {
                ++cycle_breaks;
                for(unsigned i=1u;i<total;++i)
                    if(!used[i]){chosen=static_cast<int>(i);break;}
            }
            if(chosen<0)return false;
            const unsigned id=static_cast<unsigned>(chosen);
            used[id]=true;
            order[planned++]=id;
        }
        valid=true;return true;
    }
    using Entry=bool (*)(void *,unsigned,bool);
    bool attach(Entry call,void *context) {
        if(!valid || !call || attached || failed)return false;
        for(unsigned i=0u;i<planned;++i) {
            const unsigned id=order[i];
            if(!has_entry[id])continue;
            if(!call(context,id,true)) {
                ++failed;
                detach(call,context);
                return false;
            }
            stack[attached++]=id;
        }
        return true;
    }
    void detach(Entry call,void *context) {
        if(!call)return;
        while(attached) {
            const unsigned id=stack[--attached];
            (void)call(context,id,false); // detach return value ignored
            ++detached;
        }
    }
};
} // namespace nt
