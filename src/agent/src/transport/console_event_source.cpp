#include "lsw/agent/transport/console_event_source.hpp"

#include "lsw/agent/console/console.hpp"
#include "lsw/agent/util/base64.hpp"

#include "lsw/linkproto/commands.hpp"

#include <nlohmann/json.hpp>

namespace lsw::agent::transport {

std::vector<rpc::Event> ConsoleEventSource::collect_pending() {
    const auto notes = console_->collect_pending();
    std::vector<rpc::Event> events;
    events.reserve(notes.size());
    for (const auto& n : notes) {
        const bool is_output = (n.kind == console::Console::Kind::Output);
        nlohmann::json payload;
        payload["session_id"] = n.session_id;
        if (is_output) {
            // Base64 so the bytes survive JSON. Console output is a few
            // KB/s at most — not worth a dedicated binary envelope type.
            payload["data_b64"] = util::Base64::encode(n.data);
        } else {
            payload["exit_code"] = n.exit_code;
        }
        events.emplace_back(is_output ? linkproto::topic::kConsoleOutput
                                      : linkproto::topic::kConsoleClosed,
                            std::move(payload));
    }
    return events;
}

}  // namespace lsw::agent::transport
