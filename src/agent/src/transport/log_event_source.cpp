#include "lsw/agent/transport/log_event_source.hpp"

#include "lsw/agent/log.hpp"

#include "lsw/linkproto/commands.hpp"

#include <nlohmann/json.hpp>

namespace lsw::agent::transport {

std::vector<rpc::Event> LogEventSource::collect_pending() {
    std::uint64_t next_since = last_seq_;
    const auto records = log::collect_since(last_seq_, next_since);

    std::vector<rpc::Event> events;
    events.reserve(records.size());
    batch_seqs_.clear();
    batch_seqs_.reserve(records.size());
    for (const auto& r : records) {
        batch_seqs_.push_back(r.seq);
        events.emplace_back(linkproto::topic::kLog,
                            nlohmann::json{
                                {"seq", r.seq},
                                {"ts_ms", r.ts_ms},
                                {"level", log::level_name(r.level)},
                                {"msg", r.msg},
                            });
    }
    last_seq_ = next_since;
    return events;
}

void LogEventSource::undeliverable(std::size_t first_undelivered) {
    // Rewind to just before the first record that did not make it, so the
    // next connection picks up exactly there.
    if (first_undelivered < batch_seqs_.size()) {
        last_seq_ = (first_undelivered == 0) ? (batch_seqs_.front() - 1)
                                             : batch_seqs_[first_undelivered - 1];
    }
    batch_seqs_.clear();
}

}  // namespace lsw::agent::transport
