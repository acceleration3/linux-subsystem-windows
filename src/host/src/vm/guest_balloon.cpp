#include "lsw/host/vm/guest_balloon.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <array>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

namespace lsw::host::vm {

std::optional<GuestBalloon::Reading> GuestBalloon::read(const QUuid& uuid) const {
    const DomainHandle dom = DomainHandle::lookup(conn_.raw(), uuid);
    if (!dom.valid()) {
        return std::nullopt;
    }
    std::array<virDomainMemoryStatStruct, VIR_DOMAIN_MEMORY_STAT_NR> stats{};
    const int count = ::virDomainMemoryStats(dom.raw(), stats.data(), stats.size(), 0);
    if (count < 0) {
        return std::nullopt;
    }
    Reading reading;
    bool has_actual = false;
    for (int i = 0; i < count; ++i) {
        const auto value = static_cast<qint64>(stats[static_cast<std::size_t>(i)].val);
        switch (stats[static_cast<std::size_t>(i)].tag) {
        case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
            reading.actual_kib = value;
            has_actual = true;
            break;
        case VIR_DOMAIN_MEMORY_STAT_USABLE:
            reading.usable_kib = value;
            break;
        default:
            break;
        }
    }
    reading.max_kib = static_cast<qint64>(::virDomainGetMaxMemory(dom.raw()));
    return has_actual && reading.max_kib > 0 ? std::optional<Reading>{reading} : std::nullopt;
}

bool GuestBalloon::set_target(const QUuid& uuid, qint64 kib) const {
    const DomainHandle dom = DomainHandle::lookup(conn_.raw(), uuid);
    if (!dom.valid()) {
        return false;
    }
    if (::virDomainSetMemoryFlags(dom.raw(), static_cast<unsigned long>(kib),
                                  VIR_DOMAIN_AFFECT_LIVE) != 0) {
        const virErrorPtr error = ::virGetLastError();
        LSW_WARN("balloon: the guest's memory was not set to {} MiB: {}", kib / 1024,
                 error != nullptr && error->message != nullptr ? error->message
                                                               : "libvirt gave no reason");
        return false;
    }
    return true;
}

}  // namespace lsw::host::vm
