/* Exercises actual portable NTOS executive files used by the EX-word D01. */
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "../exword/storage.h"
#include "../src/nt/process.hpp"
#include "../src/nt/io_manager.hpp"

static void *alloc(void *,size_t size){return malloc(size);}
static void dealloc(void *,void *p){free(p);}
int main()
{
    assert(std::strcmp(ntos_exword_pattern(0),"\\\\drv0\\*")==0);
    assert(std::strcmp(ntos_exword_pattern(1),"\\\\crd0\\*")==0);
    assert(ntos_exword_pattern(2)==nullptr);
    assert(sizeof(ntos_exword_listing)<4096u);
    assert(NTOS_EXWORD_LIST_MAX==48u);
    assert(NTOS_EXWORD_PATH_MAX>=128u);
    nt::Process p{};
    assert(p.begin());
    assert(p.ke.begin(0x70000080u,0x20000u)==nt::SUCCESS);
    assert(p.ke.process.active && p.ke.process.threads==1u);
    assert(p.ke.thread.state==nt::KTHREAD_STATE::ready);
    uint32_t h=0;
    assert(p.create_event(false,false,&h)==nt::SUCCESS);
    assert(p.event_poll(h,0)==nt::TIMEOUT);
    assert(p.event_set(h)==nt::SUCCESS);
    assert(p.event_poll(h,0)==nt::SUCCESS);
    assert(p.event_poll(h,0)==nt::TIMEOUT);
    assert(p.handles.close(h,nt::Type::event)==nt::SUCCESS);
    p.pool.configure(nullptr,alloc,dealloc);
    void *buffer=p.pool.alloc(64,nt::tag('E','X','W','D'));
    assert(buffer);
    assert(p.pool.free(buffer,nt::tag('E','X','W','D'))==nt::SUCCESS);
    assert(p.pool.current==0);
    /* This D01 deliberately never creates a P0 guest VM mapping. */
    assert(p.vm.backing==nullptr && p.vm.committed_pages==0u);
    p.finish();
    assert(!p.active && !p.ke.process.active);
    return 0;
}
