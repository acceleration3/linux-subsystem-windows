#include "lsw/agent/transport/event_pump.hpp"

namespace lsw::agent::transport {

bool EventPump::drain(const Sender& send) {
    for (auto* source : sources_) {
        auto events = source->collect_pending();
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (!send(events[i])) {
                source->undeliverable(i);
                return false;
            }
        }
    }
    return true;
}

}  // namespace lsw::agent::transport
