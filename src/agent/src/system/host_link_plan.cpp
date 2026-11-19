#include "lsw/agent/system/host_link_plan.hpp"

#include <algorithm>

namespace lsw::agent::system {

namespace {
std::size_t slot(HostLinkRepair repair) {
    return static_cast<std::size_t>(repair);
}

}  // namespace

std::vector<HostLinkRepair> HostLinkPlan::repairs(const HostLinkState& state) {
    std::vector<HostLinkRepair> out;
    if (!state.firewall) {
        out.push_back(HostLinkRepair::Firewall);
    }
    switch (state.device) {
    case HostLinkState::Device::Absent:
    case HostLinkState::Device::Failing:
        out.push_back(HostLinkRepair::Rescan);
        return out;
    case HostLinkState::Device::Disabled:
        out.push_back(HostLinkRepair::Enable);
        return out;
    case HostLinkState::Device::NoDriver:
        out.push_back(HostLinkRepair::InstallDriver);
        return out;
    case HostLinkState::Device::Working:
        break;
    }
    if (state.ghosts) {
        out.push_back(HostLinkRepair::RemoveGhosts);
    }
    if (!state.ipv4_bound) {
        out.push_back(HostLinkRepair::BindIpv4);
        return out;
    }
    if (!state.dhcp && !state.has_address) {
        out.push_back(HostLinkRepair::RestoreDhcp);
    }
    if (!state.metric_v4 || !state.metric_v6) {
        out.push_back(HostLinkRepair::Metric);
    }
    if (state.has_address && !state.host_route) {
        out.push_back(HostLinkRepair::HostRoute);
    }
    if (!state.named) {
        out.push_back(HostLinkRepair::Name);
    }
    return out;
}

bool HostLinkPlan::needs_confirming(HostLinkRepair repair) noexcept {
    switch (repair) {
    case HostLinkRepair::Metric:
    case HostLinkRepair::HostRoute:
    case HostLinkRepair::Name:
    case HostLinkRepair::Firewall:
        return false;
    default:
        return true;
    }
}

const char* HostLinkPlan::name(HostLinkRepair repair) noexcept {
    switch (repair) {
    case HostLinkRepair::Rescan:
        return "rescan devices";
    case HostLinkRepair::Enable:
        return "enable the adapter";
    case HostLinkRepair::InstallDriver:
        return "install its driver";
    case HostLinkRepair::RemoveGhosts:
        return "remove adapters left at its slot";
    case HostLinkRepair::BindIpv4:
        return "bind IPv4";
    case HostLinkRepair::RestoreDhcp:
        return "restore DHCP";
    case HostLinkRepair::Metric:
        return "set its metric";
    case HostLinkRepair::HostRoute:
        return "add the route to the host";
    case HostLinkRepair::Name:
        return "name it";
    case HostLinkRepair::Firewall:
        return "restore the firewall rules";
    case HostLinkRepair::Count:
        break;
    }
    return "?";
}

void RepairBackoff::observe(const std::vector<HostLinkRepair>& needed, Time now) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto repair = static_cast<HostLinkRepair>(i);
        const bool wanted = std::find(needed.begin(), needed.end(), repair) != needed.end();
        Entry& entry = entries_[i];
        if (!wanted) {
            entry = Entry{};
            continue;
        }
        if (!entry.needed) {
            entry.needed = true;
            entry.attempts = 0;
            entry.next = HostLinkPlan::needs_confirming(repair) ? now + kConfirm : now;
        }
    }
}

bool RepairBackoff::due(HostLinkRepair repair, Time now) const {
    const Entry& entry = entries_[slot(repair)];
    return entry.needed && now >= entry.next;
}

void RepairBackoff::tried(HostLinkRepair repair, Time now) {
    Entry& entry = entries_[slot(repair)];
    const auto step =
        std::min<std::size_t>(static_cast<std::size_t>(entry.attempts), kRetry.size() - 1);
    ++entry.attempts;
    entry.next = now + kRetry[step];
}

int RepairBackoff::attempts(HostLinkRepair repair) const {
    return entries_[slot(repair)].attempts;
}

std::optional<RepairBackoff::Time> RepairBackoff::next_due() const {
    std::optional<Time> soonest;
    for (const Entry& entry : entries_) {
        if (entry.needed && (!soonest.has_value() || entry.next < *soonest)) {
            soonest = entry.next;
        }
    }
    return soonest;
}

}  // namespace lsw::agent::system
