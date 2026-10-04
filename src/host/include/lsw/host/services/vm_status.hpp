#pragma once

#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/vm_stats.hpp"

#include <QUuid>
#include <optional>

namespace lsw::host::services {

// The machine as StatusHub last saw it: which VM, whether its QEMU runs,
// whether its agent answers, the guest's own figures, and the machine's
// shape as libvirt holds it.
struct VmStatus {
    QUuid uuid;  // null: no VM to watch
    vm::VmStats stats;

    // The running domain's live definition: what this boot actually has,
    // whatever the settings say now. Unset while the VM is off.
    std::optional<vm::DomainDescription> running_device;
    // The stored definition, while the VM is off: what the next start
    // boots. Unset while it runs (running_device is the one that matters
    // then) and when there is no such domain.
    std::optional<vm::DomainDescription> next_boot;
    // The system disk image's virtual size, as its header says.
    std::optional<quint64> disk_bytes;
    // The agent transport's shared region while the VM runs: its size,
    // and whether this manager has its doorbells (it started the VM) or
    // sweeps the ring on a timer (it attached to one already running).
    quint64 region_bytes = 0;
    bool doorbells = false;

    [[nodiscard]] bool running() const noexcept { return stats.running; }
    [[nodiscard]] bool agent_online() const noexcept { return stats.agent_reachable; }

    // The definition that describes the machine now: the live one while
    // it runs, the stored one while it is off. Null when neither is known.
    [[nodiscard]] const vm::DomainDescription* definition() const noexcept {
        if (running_device) {
            return &*running_device;
        }
        return next_boot ? &*next_boot : nullptr;
    }
};

}  // namespace lsw::host::services
