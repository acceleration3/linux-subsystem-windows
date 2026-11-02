#pragma once

#include "lsw/host/vm/guest_stats_probe.hpp"

namespace lsw::host::link {

class AgentRpcClient;

class AgentGuestStatsProbe final : public vm::IGuestStatsProbe {
public:
    explicit AgentGuestStatsProbe(AgentRpcClient& rpc) noexcept : rpc_(rpc) {}

    [[nodiscard]] bool available() const override;
    [[nodiscard]] vm::GuestStatsSample sample() override;

private:
    AgentRpcClient& rpc_;
};

}
