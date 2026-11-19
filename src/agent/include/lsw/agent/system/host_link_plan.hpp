#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace lsw::agent::system {

struct HostLinkState {
    enum class Device : std::uint8_t { Absent, Working, Disabled, NoDriver, Failing };

    Device device = Device::Absent;
    bool ghosts = false;
    bool ipv4_bound = false;
    bool dhcp = true;
    bool has_address = false;
    bool metric_v4 = false;
    bool metric_v6 = true;
    bool host_route = false;
    bool named = false;
    bool firewall = true;
};

enum class HostLinkRepair : std::uint8_t {
    Rescan,
    Enable,
    InstallDriver,
    RemoveGhosts,
    BindIpv4,
    RestoreDhcp,
    Metric,
    HostRoute,
    Name,
    Firewall,
    Count,
};

class HostLinkPlan {
public:
    HostLinkPlan() = delete;

    [[nodiscard]] static std::vector<HostLinkRepair> repairs(const HostLinkState& state);

    [[nodiscard]] static bool needs_confirming(HostLinkRepair repair) noexcept;

    [[nodiscard]] static const char* name(HostLinkRepair repair) noexcept;
};

class RepairBackoff {
public:
    using Time = std::chrono::steady_clock::time_point;

    static constexpr std::chrono::seconds kConfirm{15};
    static constexpr std::array<std::chrono::seconds, 4> kRetry{
        std::chrono::seconds{10},
        std::chrono::seconds{60},
        std::chrono::seconds{300},
        std::chrono::seconds{900},
    };

    void observe(const std::vector<HostLinkRepair>& needed, Time now);

    [[nodiscard]] bool due(HostLinkRepair repair, Time now) const;

    void tried(HostLinkRepair repair, Time now);

    [[nodiscard]] int attempts(HostLinkRepair repair) const;

    [[nodiscard]] std::optional<Time> next_due() const;

private:
    struct Entry {
        bool needed = false;
        Time next;
        int attempts = 0;
    };

    std::array<Entry, static_cast<std::size_t>(HostLinkRepair::Count)> entries_{};
};

}  // namespace lsw::agent::system
