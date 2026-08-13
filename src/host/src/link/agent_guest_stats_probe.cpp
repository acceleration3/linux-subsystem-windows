#include "lsw/host/link/agent_guest_stats_probe.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::link {

bool AgentGuestStatsProbe::available() const { return rpc_.has_target(); }

vm::GuestStatsSample AgentGuestStatsProbe::sample() {
    vm::GuestStatsSample out;
    const auto reply = rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kSystemInfo}});
    if (reply.isEmpty()) {
        return out;
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(reply.constData(), reply.constData() + reply.size());
    } catch (const nlohmann::json::exception&) {
        return out;
    }
    if (!j.is_object()) {
        return out;
    }
    out.valid = true;
    out.cpu_percent = j.value("cpu_percent", -1);
    out.mem_total = j.value("mem_total", std::uint64_t{0});
    out.mem_avail = j.value("mem_avail", std::uint64_t{0});
    out.disk_total = j.value("disk_total", std::uint64_t{0});
    out.disk_avail = j.value("disk_avail", std::uint64_t{0});
    return out;
}

}  // namespace lsw::host::link
