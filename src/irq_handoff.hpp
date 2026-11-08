// irq_handoff.hpp -- opt-in, transactional ownership protocol for future
// NTOS TLB-miss/interrupt world. Does not install any vector or UBC handler.
//
// Based on the structure (not copied code) of gint world switching and the
// user's UBC breakpoint stub. The latter uses DBR and RTE; it does NOT
// implement a TLB-miss handler, safe exception stack, dirty page writeback,
// or reentrant fault handling. Those are required before enabling this.
//
// Without an exception handler no true page fault can be served. The
// default is intentionally OFF and acquire() has no side effects.
#pragma once
#include <stdint.h>
#include "host_world.hpp"
#ifndef NTOS_ENABLE_IRQ_OWNERSHIP
#define NTOS_ENABLE_IRQ_OWNERSHIP 0
#endif

namespace ntos_irq {
enum class State : uint8_t {
    idle, prepared, owned, restored, failed
};
struct Hooks {
    void *context;
    bool (*prepare)(void *);      // must reserve a dedicated P1 IRQ stack
    bool (*install)(void *);      // installs VBR/UBC handlers under IRQ mask
    bool (*verify)(void *);       // checks vector paths and fault trampoline
    void (*uninstall)(void *);    // restores original VBR, DBR and UBC state
};
struct Owner {
    State state=State::idle;
    host_world::Snapshot saved{};
    Hooks hooks{};
    bool armed=false;

    bool prepare(const host_world::Snapshot &host,
                 const Hooks &callbacks) {
        if(state!=State::idle || (host.mmucr&1u))
            return false;
        saved=host;
        hooks=callbacks;
        state=State::prepared;
        return true;
    }
    bool acquire() {
#if NTOS_ENABLE_IRQ_OWNERSHIP
        if(state!=State::prepared || !hooks.prepare ||
           !hooks.install || !hooks.verify || !hooks.uninstall) {
            state=State::failed;return false;
        }
        if(!hooks.prepare(hooks.context)) {
            state=State::failed;return false;
        }
        // install MUST be atomic under saved interrupt mask and MUST NOT
        // dispatch host file/LCD code from the exception context.
        armed=true; // uninstall on every failed installation branch
        if(!hooks.install(hooks.context) || !hooks.verify(hooks.context)) {
            hooks.uninstall(hooks.context);
            armed=false;
            state=State::failed;
            return false;
        }
        state=State::owned;
        return true;
#else
        // Current HH3 policy: preserve host exception ownership.
        return false;
#endif
    }
    void release() {
        if(armed && hooks.uninstall) {
            hooks.uninstall(hooks.context);
            armed=false;
        }
        state=State::restored;
    }
};
} // namespace ntos_irq
