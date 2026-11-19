// Host-only Ob/Ex and process-lifetime tests. No HH3 SDK required.
// Run: g++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2
//      tests/nt_executive_test.cpp -o /tmp/nt-exec && /tmp/nt-exec
#include "../src/nt/process.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
static unsigned frees=0,allocations=0;
static bool fail_next=false;
static void *alloc(void *,size_t bytes) {
    if(fail_next){fail_next=false;return nullptr;}
    ++allocations;return std::malloc(bytes);
}
static void release(void *,void *ptr) {
    ++frees;std::free(ptr);
}
int main() {
    nt::Process process{};
    process.pool.configure(nullptr,alloc,release);
    assert(process.begin());
    uint32_t first=0u;
    assert(process.attach_file(17,&first)==nt::SUCCESS);
    assert(first>=0x40000000u && first<=0x7fffffffu);
    assert(process.file_fd(first)==17);
    assert(process.attach_file(18,&first)==nt::DEVICE_BUSY);
    assert(process.handles.used==1u);
    assert(process.handles.get(first,nt::Type::event,
        reinterpret_cast<nt::Object **>(nullptr))==nt::INVALID_PARAMETER);
    assert(process.drop_file(first)==nt::SUCCESS);
    assert(process.file_fd(first)<0);
    assert(process.drop_file(first)==nt::INVALID_HANDLE);
    uint32_t second=0u;
    assert(process.attach_file(17,&second)==nt::SUCCESS);
    assert(second!=first); // recycled SDK fd never revives a stale token
    assert(process.file_fd(first)<0 && process.file_fd(second)==17);
    assert(process.drop_file(second)==nt::SUCCESS);
    uint32_t finder=0u;
    assert(process.attach_finder(29,&finder)==nt::SUCCESS);
    assert(process.finder_fd(finder)==29);
    assert(process.file_fd(finder)<0); // cannot confuse a finder with a file
    assert(process.drop_finder(finder)==nt::SUCCESS);
    assert(process.finder_fd(finder)<0);

    uint32_t manual=0u;
    assert(process.create_event(true,false,&manual)==nt::SUCCESS);
    assert(process.event_poll(manual,0u)==nt::TIMEOUT);
    assert(process.event_set(manual)==nt::SUCCESS);
    assert(process.event_poll(manual,0u)==nt::SUCCESS);
    assert(process.event_poll(manual,0u)==nt::SUCCESS);
    assert(process.event_reset(manual)==nt::SUCCESS);
    assert(process.event_poll(manual,1u)==nt::NOT_SUPPORTED);
    assert(process.handles.close(manual)==nt::SUCCESS);
    assert(process.event_set(manual)==nt::INVALID_HANDLE);

    uint32_t auto_reset=0u;
    assert(process.create_event(false,true,&auto_reset)==nt::SUCCESS);
    assert(process.event_poll(auto_reset,0u)==nt::SUCCESS);
    assert(process.event_poll(auto_reset,0u)==nt::TIMEOUT);
    assert(process.handles.close(auto_reset)==nt::SUCCESS);
    for(unsigned i=0u;i<1000u;++i) {
        uint32_t t=0u;
        assert(process.create_event(true,true,&t)==nt::SUCCESS);
        assert(t!=auto_reset && t!=manual);
        assert(process.handles.close(t)==nt::SUCCESS);
    }
    assert(process.handles.used==0u);

    constexpr uint32_t TAG=nt::tag('T','E','S','T');
    void *p=process.pool.alloc(4096u,TAG);
    assert(p && process.pool.current==4096u && process.pool.peak==4096u);
    assert(process.pool.free(p,0u)==nt::INVALID_PARAMETER);
    assert(process.pool.current==4096u);
    assert(process.pool.free(p,TAG)==nt::SUCCESS);
    assert(process.pool.free(p,TAG)==nt::INVALID_PARAMETER);
    fail_next=true;
    assert(process.pool.alloc(1u,TAG)==nullptr);
    assert(process.pool.failures==1u && process.pool.invalid_frees==2u);
    void *leaked=process.pool.alloc(32u,TAG);
    assert(leaked);
    uint32_t unfinished=0u;
    assert(process.create_event(true,true,&unfinished)==nt::SUCCESS);
    assert(process.handles.used==1u);
    process.finish(); // cleanup with NO SDK File_Close
    assert(process.handles.used==0u && process.pool.current==0u);
    assert(process.events.entries[0].in_use==false);
    assert(frees==allocations);
    assert(process.begin()); // cookies persist across separate launches
    uint32_t newest=0u;
    assert(process.create_event(true,true,&newest)==nt::SUCCESS);
    assert(newest!=unfinished);
    assert(process.event_poll(unfinished,0u)==nt::INVALID_HANDLE);
    process.finish();
    std::puts("PASS NT Ob typed handles, stale-cookie rejection, event semantics, Ex pool and process cleanup");
}
