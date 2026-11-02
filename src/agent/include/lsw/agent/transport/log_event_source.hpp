#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <cstdint>

namespace lsw::agent::transport {
class LogEventSource : public rpc::EventSource {
public:
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;
    void undeliverable(std::size_t first_undelivered) override;

private:
    std::uint64_t last_seq_ = 0;
    std::vector<std::uint64_t> batch_seqs_;
};

}
