#include "../src/irq_handoff.hpp"
#include <cassert>
#include <cstdio>
struct Counters {int prepare=0,install=0,verify=0,uninstall=0;};
static bool prep(void *p) {
    ++static_cast<Counters *>(p)->prepare;return true;
}
static bool install(void *p) {
    ++static_cast<Counters *>(p)->install;return true;
}
static bool verify(void *p) {
    ++static_cast<Counters *>(p)->verify;return true;
}
static void uninstall(void *p) {
    ++static_cast<Counters *>(p)->uninstall;
}
int main() {
    static_assert(NTOS_ENABLE_IRQ_OWNERSHIP==0,
                  "CI must never acquire real SH4 IRQ ownership");
    host_world::Snapshot host{};
    host.mmucr=1u;
    Counters counters{};
    ntos_irq::Hooks hooks{&counters,prep,install,verify,uninstall};
    ntos_irq::Owner owner{};
    assert(!owner.prepare(host,hooks));
    assert(owner.state==ntos_irq::State::idle);
    host.mmucr=0u;
    assert(owner.prepare(host,hooks));
    assert(owner.state==ntos_irq::State::prepared);
    assert(!owner.acquire());
    assert(!owner.armed);
    assert(counters.prepare==0 && counters.install==0 &&
           counters.verify==0 && counters.uninstall==0);
    owner.release();
    assert(owner.state==ntos_irq::State::restored);
    assert(counters.uninstall==0);
    std::puts("PASS IRQ handoff: disabled and no UBC/VBR callbacks invoked");
}
