#pragma once

#include "lsw/host/vm/guest_stats_probe.hpp"

namespace lsw::host::link {

class AgentRpcClient;

// IGuestStatsProbe over the agent channel: one ``system_info`` RPC per
// sample. The adapter that lets Monitor read in-guest numbers without
// lsw_host_vm ever learning that an agent, a socket, or JSON exists.
class AgentGuestStatsProbe final : public vm::IGuestStatsProbe {
public:
    explicit AgentGuestStatsProbe(AgentRpcClient& rpc) noexcept : rpc_(rpc) {}

    [[nodiscard]] bool available() const override;
    [[nodiscard]] vm::GuestStatsSample sample() override;

private:
    AgentRpcClient& rpc_;
};

}  // namespace lsw::host::link
