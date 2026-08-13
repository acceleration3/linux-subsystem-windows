#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <cstdint>

namespace lsw::agent::transport {

// Publishes the agent's log ring as ``log`` events.
//
// Per-connection, because the cursor is: each manager that connects
// starts from wherever the ring is now and follows along. Undelivered
// records are not requeued — the cursor simply is not advanced past
// them, so a reconnecting manager re-reads them from the ring if they
// are still retained.
class LogEventSource : public rpc::EventSource {
public:
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;
    void undeliverable(std::size_t first_undelivered) override;

private:
    std::uint64_t last_seq_ = 0;
    // Sequence numbers of the batch last handed out, so a partial
    // delivery can rewind the cursor to just before the first record
    // that was lost.
    std::vector<std::uint64_t> batch_seqs_;
};

}  // namespace lsw::agent::transport
