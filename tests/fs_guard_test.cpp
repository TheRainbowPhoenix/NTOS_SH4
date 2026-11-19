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
    assert(gate.busy() && gate.poisoned && gate.close_failures==1);
    assert(!gate.file_is(17)); // poisoned descriptor is NOT usable
    assert(!gate.acquire_file(18)); // close failure locks all new access
    fail_close=0;
    assert(!gate.cleanup(close_file,close_find)); // no retry of ambiguous close
    assert(!gate.close(close_file,close_find));
    assert(file_closes==1); // critical: only one SDK close attempted

    ntos_fs::Gate healthy;
    assert(healthy.acquire_finder(29));
    assert(healthy.finder_is(29));
    assert(!healthy.acquire_file(17));
    assert(healthy.cleanup(close_file,close_find));
    assert(!healthy.busy() && healthy.rescued==1 && find_closes==1);
    assert(healthy.acquire_file(17));
    assert(healthy.close(close_file,close_find));
    assert(!healthy.busy());
    assert(healthy.cleanup(close_file,close_find));
    std::puts("PASS exclusive ownership, stale SDK handle fail-stop, no close retry");
}
