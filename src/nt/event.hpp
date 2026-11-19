// ex/ke: event object semantics matching NT NotificationEvent (manual reset)
// and SynchronizationEvent (auto reset). Only nonblocking waits so far.
// A real wait scheduler must precede blocking NtWaitForSingleObject.
#pragma once
#include "object.hpp"
namespace nt {
struct Event {
    Object header{};
    bool in_use=false;
    bool manual=false;
    bool signaled=false;
};
template<unsigned COUNT=16u>
struct Events {
    Event entries[COUNT]{};
    static void destroy(Object *header) {
        Event *e=reinterpret_cast<Event *>(header);
        e->in_use=false;e->signaled=false;e->manual=false;
    }
    Event *create(bool manual,bool initial) {
        for(unsigned i=0u;i<COUNT;++i) {
            Event &e=entries[i];
            if(!e.in_use) {
                e.in_use=true;e.manual=manual;e.signaled=initial;
                init(e.header,Type::event,&destroy);
                return &e;
            }
        }
        return nullptr;
    }
};
inline Status set_event(Event &e) {
    if(!e.in_use)return INVALID_HANDLE;
    e.signaled=true;return SUCCESS;
}
inline Status reset_event(Event &e) {
    if(!e.in_use)return INVALID_HANDLE;
    e.signaled=false;return SUCCESS;
}
inline Status poll_event(Event &e,uint32_t timeout_ms) {
    if(!e.in_use)return INVALID_HANDLE;
    if(e.signaled) {
        if(!e.manual)e.signaled=false;
        return SUCCESS;
    }
    return timeout_ms==0u ? TIMEOUT : NOT_SUPPORTED;
}
} // namespace nt
