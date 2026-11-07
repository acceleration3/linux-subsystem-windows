#include "lsw/host/config/vram_reserve.hpp"

#include "lsw/host/platform/host_gpus.hpp"

#include <algorithm>

namespace lsw::host::config {

VramReserve::Split VramReserve::resolve(int setting_mib, quint64 total_mib) {
    constexpr quint64 kMib = 1024 * 1024;
    const quint64 total = total_mib * kMib;
    quint64 reserve = 0;
    if (setting_mib <= 0) {
        reserve = std::max(total / 8, kAutoFloorMib * kMib);
    } else {
        reserve = std::max(static_cast<quint64>(setting_mib) * kMib, kLeastReserveMib * kMib);
        const quint64 most = total > kLeastGuestMib * kMib ? total - kLeastGuestMib * kMib : 0;
        reserve = std::min(reserve, most);
    }
    Split out;
    const quint64 cap = total > reserve && total - reserve >= total / 2 ? total - reserve : total / 2;
    out.reserve_mib = reserve / kMib;
    out.guest_cap_mib = cap / kMib;
    return out;
}

QString VramReserve::label(int setting_mib) {
    return setting_mib <= 0 ? QStringLiteral("Auto")
                            : platform::HostGpus::format_mib(static_cast<quint64>(setting_mib));
}

}
