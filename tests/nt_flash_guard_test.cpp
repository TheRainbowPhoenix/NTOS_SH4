// In-memory SDK simulator for the *combined* nt::Process handle table and
// exclusive filesystem Gate. It deliberately reuses SDK fd 17 on every open.
// No physical flash and no HH3 SDK calls in this test.
#include "../src/nt/process.hpp"
#include "../src/fs_guard.hpp"
#include <cassert>
#include <cstdio>
struct MockFs {
    nt::Process process{};
    ntos_fs::Gate gate{};
    unsigned opens=0u,reads=0u,closes=0u,finds=0u;
    bool fail_close=false;
    static MockFs *active;
    static int close_sdk(int fd) {
        assert(active && fd==17);
        ++active->closes;
        return active->fail_close ? -1 : 0;
    }
    static int close_find(int fd) {
        assert(active && fd==29);
        return active->fail_close ? -1 : 0;
    }
    bool begin() {
        active=this;
        return process.begin();
    }
    int32_t open_file() {
        if(gate.busy() || gate.poisoned)return -16;
        ++opens; // simulator's File_Open would return descriptor 17
        if(!gate.acquire_file(17))return -16;
        uint32_t cookie=0u;
        if(process.attach_file(17,&cookie)!=nt::SUCCESS) {
            // A successful SDK open must be closed once before returning.
            return gate.close(close_sdk,close_find) ? -12 : -1;
        }
        return static_cast<int32_t>(cookie);
    }
    int32_t read_file(uint32_t cookie) {
        const int32_t sdk_fd=process.file_fd(cookie);
        if(!gate.file_is(sdk_fd))return -2;
        ++reads;
        return 1;
    }
    int32_t close_file(uint32_t cookie) {
        const int32_t sdk_fd=process.file_fd(cookie);
        if(!gate.file_is(sdk_fd))return -2;
        if(!gate.close(close_sdk,close_find))return -1;
        return process.drop_file(cookie)==nt::SUCCESS ? 0 : -2;
    }
    bool close_leaked_and_end() {
        const bool ok=gate.cleanup(close_sdk,close_find);
        process.finish(); // no SDK calls here; not allowed to retry
        return ok;
    }
};
MockFs *MockFs::active=nullptr;
int main() {
    MockFs d{};
    assert(d.begin());
    const int32_t a=d.open_file();
    assert(a>0 && d.opens==1u);
    assert(d.open_file()==-16 && d.opens==1u); // no second File_Open
    assert(!d.gate.acquire_finder(29) && d.finds==0u);
    assert(d.read_file(static_cast<uint32_t>(a))==1 && d.reads==1u);
    assert(d.close_file(static_cast<uint32_t>(a))==0 && d.closes==1u);
    assert(d.read_file(static_cast<uint32_t>(a))==-2 && d.reads==1u);
    assert(d.close_file(static_cast<uint32_t>(a))==-2 && d.closes==1u);
    const int32_t b=d.open_file();
    assert(b>0 && b!=a && d.opens==2u); // SDK fd is recycled but token isn't
    assert(d.read_file(static_cast<uint32_t>(a))==-2 && d.reads==1u);
    assert(d.read_file(static_cast<uint32_t>(b))==1 && d.reads==2u);
    assert(d.close_leaked_and_end());
    assert(d.closes==2u); // leaked b closed once, on normal teardown
    assert(d.begin());
    assert(d.read_file(static_cast<uint32_t>(b))==-2);
    const int32_t c=d.open_file();
    assert(c>0 && c!=a && c!=b && d.opens==3u);
    assert(d.read_file(static_cast<uint32_t>(b))==-2);
    d.fail_close=true;
    assert(d.close_file(static_cast<uint32_t>(c))==-1);
    assert(d.closes==3u && d.gate.poisoned);
    assert(d.read_file(static_cast<uint32_t>(c))==-2);
    assert(d.open_file()==-16 && d.opens==3u);
    assert(d.close_file(static_cast<uint32_t>(c))==-2);
    assert(!d.close_leaked_and_end()); // poisoned cleanup MUST NOT retry
    assert(d.closes==3u);
    assert(!d.gate.acquire_file(17));
    std::puts("PASS 1-owner SDK gate, stale fd reuse, leak close once and permanently poisoned failure");
    return 0;
}
