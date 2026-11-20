// Host regression: optional STARTHOST fallback must not cause false
// "PE file unavailable" after the PROCTEST serial launch finishes.
#include "../src/nt/launch_policy.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace ntos_launch_policy;
    unsigned probes=0u;
    auto missing=[&](const char *path) {
        assert(ascii_equal_ci(path,HOME));
        ++probes;return false;
    };
    auto exists=[&](const char *path) {
        assert(ascii_equal_ci(path,HOME));
        ++probes;return true;
    };
    // Kernel test chains must return to picker, NEVER launch Start.
    assert(!queue_optional_home("\\fls0\\PROCFINAL.EXE",missing));
    assert(!queue_optional_home("\\fls0\\PROCFINAL.EXE",exists));
    assert(!queue_optional_home("\\fls0\\PSCHILD.EXE",exists));
    assert(!queue_optional_home("\\FLS0\\pschild.exe",exists));
    assert(probes==0u);
    // Ordinary UI-managed application still gets optional fallback.
    assert(!queue_optional_home("\\fls0\\OTHER.EXE",missing));
    assert(probes==1u);
    assert(queue_optional_home("\\fls0\\OTHER.EXE",exists));
    assert(probes==2u);
    assert(!queue_optional_home("\\fls0\\STARTHOST.EXE",exists));
    assert(!queue_optional_home("\\FLS0\\starthost.exe",exists));
    assert(!queue_optional_home(nullptr,exists));
    assert(probes==2u); // no File_Stat needed when already at Start
    assert(!ascii_equal_ci("\\fls0\\STRESS.EXE",HOME));
    std::puts("PASS optional STARTHOST fallback: no missing-PE error or home loop after PROCTEST");
}
