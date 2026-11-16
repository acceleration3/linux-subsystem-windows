#include "lsw/host/services/balloon_policy.hpp"

#include <algorithm>

namespace lsw::host::services {

qint64 BalloonPolicy::low_kib(qint64 host_total_kib) noexcept {
    return std::max(kLeastLowKib, host_total_kib / kLowDivisor);
}

qint64 BalloonPolicy::floor_kib(const Reading& reading) noexcept {
    qint64 floor = std::max(kLeastGuestKib, reading.guest_max_kib / 4);
    if (reading.guest_usable_kib) {
        floor = std::max(floor, reading.guest_actual_kib - *reading.guest_usable_kib + kGuestHeadroomKib);
    }
    return std::min(floor, reading.guest_max_kib);
}

std::optional<qint64> BalloonPolicy::target_kib(const Reading& reading) noexcept {
    const qint64 low = low_kib(reading.host_total_kib);
    const qint64 actual = reading.guest_actual_kib;
    const qint64 available = reading.host_available_kib;
    const auto shrink_to = [actual](qint64 target) {
        return actual - target >= kStepKib ? std::optional<qint64>{target} : std::nullopt;
    };
    const auto grow_by = [&reading, actual](qint64 room) {
        const qint64 target = std::min(reading.guest_max_kib, actual + room);
        return target - actual >= kStepKib ? std::optional<qint64>{target} : std::nullopt;
    };
    if (available < low) {
        return shrink_to(std::max(floor_kib(reading), actual - (2 * low - available)));
    }
    if (!reading.guest_usable_kib) {
        return available > 2 * low ? grow_by(available - 2 * low) : std::nullopt;
    }
    const qint64 usable = *reading.guest_usable_kib;
    if (usable < kGuestStarvedKib) {
        return grow_by(std::min(available - low, kGuestGrowKib));
    }
    if (usable > kGuestSpareKib + kGuestSlackKib) {
        return shrink_to(std::max(floor_kib(reading), actual - (usable - kGuestSpareKib)));
    }
    return std::nullopt;
}

}
