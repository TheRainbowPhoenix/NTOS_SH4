// HOST ONLY! Fault injection is categorically forbidden on real CP400 flash.
// Mock SDK filesystem short reads, ambiguous closes and deferred requests.
#include "../src/fs_guard.hpp"
#include "../src/nt/process.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
struct Sim {
    ntos_fs::Gate gate{};
    nt::Process process{};
    unsigned opens=0u,reads=0u,closes=0u,finds=0u;
    bool short_read=false,ambiguous_close=false;
    bool pending=false;
    static Sim *self;
    static int close_file(int fd) {
        assert(self && fd==17);
        ++self->closes;
        return self->ambiguous_close ? -9 : 0;
    }
    static int close_finder(int fd) {
        assert(self && fd==29);
        ++self->closes;
        return self->ambiguous_close ? -9 : 0;
    }
    void start() {self=this;assert(process.begin());}
    int32_t open() {
        if(gate.busy() || gate.poisoned)return -16;
        ++opens;
        assert(gate.acquire_file(17));
        uint32_t cookie=0u;
        assert(process.attach_file(17,&cookie)==nt::SUCCESS);
        return static_cast<int32_t>(cookie);
    }
    int32_t read(uint32_t cookie,uint32_t requested) {
        const int raw=process.file_fd(cookie);
        if(!gate.file_is(raw))return -2;
        ++reads;
        if(short_read){short_read=false;return requested>5u ? 5 : static_cast<int32_t>(requested);}
        return static_cast<int32_t>(requested);
    }
    int close(uint32_t cookie) {
        const int raw=process.file_fd(cookie);
        if(!gate.file_is(raw))return -2;
        if(!gate.close(close_file,close_finder))return -1;
        return process.drop_file(cookie)==nt::SUCCESS ? 0 : -2;
    }
    bool end() {
        const bool ok=gate.cleanup(close_file,close_finder);
        process.finish();
        return ok;
    }
    bool defer_open_if_busy() {
        if(gate.busy()) {pending=true;return true;}
        return false;
    }
    int32_t flush_deferred() {
        if(!pending)return -1;
        if(gate.busy() || gate.poisoned)return -16;
        pending=false;
        return open();
    }
};
Sim *Sim::self=nullptr;
int main() {
    Sim f{};
    f.start();
    int32_t first=f.open();
    assert(first>0);
    assert(f.open()==-16 && f.opens==1u);
    assert(!f.gate.acquire_finder(29) && f.finds==0u);
    assert(f.defer_open_if_busy());
    assert(f.flush_deferred()==-16 && f.pending && f.opens==1u);
    f.short_read=true;
    assert(f.read(static_cast<uint32_t>(first),32u)==5 && f.reads==1u);
    assert(f.read(static_cast<uint32_t>(first),8u)==8 && f.reads==2u);
    assert(f.close(static_cast<uint32_t>(first))==0 && f.closes==1u);
    assert(f.read(static_cast<uint32_t>(first),8u)==-2 && f.reads==2u);
    assert(f.close(static_cast<uint32_t>(first))==-2 && f.closes==1u);
    int32_t second=f.flush_deferred();
    assert(second>0 && second!=first && f.opens==2u && !f.pending);
    assert(f.read(static_cast<uint32_t>(first),8u)==-2);
    assert(f.end() && f.closes==2u); // leaked second closed once
    assert(f.gate.busy()==false);
    f.start();
    int32_t third=f.open();
    assert(third>0 && third!=second);
    f.ambiguous_close=true;
    assert(f.close(static_cast<uint32_t>(third))==-1);
    assert(f.gate.poisoned && f.closes==3u);
    assert(f.read(static_cast<uint32_t>(third),1u)==-2);
    assert(f.close(static_cast<uint32_t>(third))==-2);
    assert(f.open()==-16 && f.opens==3u);
    assert(f.defer_open_if_busy() && f.flush_deferred()==-16);
    assert(!f.end() && f.closes==3u);
    assert(f.gate.poisoned);
    std::puts("PASS host IOTEST injection: short read, busy/deferred open, stale fd, ambiguous close fail-stop, zero retries");
}
