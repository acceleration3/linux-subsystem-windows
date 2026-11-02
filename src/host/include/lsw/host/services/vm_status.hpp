#pragma once

#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/vm_stats.hpp"

#include <QUuid>
#include <optional>

namespace lsw::host::services {
struct VmStatus {
    QUuid uuid;
    vm::VmStats stats;
    int domain_id = -1;

    std::optional<vm::DomainDescription> running_device;
    std::optional<vm::DomainDescription> next_boot;
    std::optional<quint64> disk_bytes;

    [[nodiscard]] bool running() const noexcept { return stats.running; }
    [[nodiscard]] bool agent_online() const noexcept { return stats.agent_reachable; }

    [[nodiscard]] const vm::DomainDescription* definition() const noexcept {
        if (running_device) {
            return &*running_device;
        }
        return next_boot ? &*next_boot : nullptr;
    }
};

}
