#include "../src/fs_guard.hpp"
#include <cassert>
#include <cstdio>

static int file_closes=0, find_closes=0, fail_close=0;
static int close_file(int fd) {
    assert(fd==17);
    ++file_closes;
    return fail_close ? -1 : 0;
}
static int close_find(int fd) {
    assert(fd==29);
    ++find_closes;
    return fail_close ? -1 : 0;
}
int main() {
    ntos_fs::Gate gate;
    assert(!gate.busy());
    assert(gate.acquire_file(17));
    assert(gate.file_is(17));
    assert(!gate.acquire_file(17)); // same-file double open rejected
    assert(!gate.acquire_file(18)); // even different file rejected
    assert(!gate.acquire_finder(29)); // no overlapping enumerator
    assert(gate.rejected==3);
    fail_close=1;
    assert(!gate.close(close_file,close_find));
    assert(gate.busy() && gate.close_failures==1);
    assert(!gate.acquire_file(18)); // close failure locks all new access
    fail_close=0;
    assert(gate.cleanup(close_file,close_find));
    assert(!gate.busy() && gate.rescued==1 && file_closes==2);
    assert(gate.acquire_finder(29));
    assert(gate.finder_is(29));
    assert(!gate.acquire_file(17));
    assert(gate.cleanup(close_file,close_find));
    assert(!gate.busy() && gate.rescued==2 && find_closes==1);
    assert(gate.acquire_file(17));
    assert(gate.close(close_file,close_find));
    assert(!gate.busy());
    assert(gate.cleanup(close_file,close_find));
    std::puts("PASS exclusive file/finder ownership, double-open denial, failed-close interlock, exit cleanup");
}
