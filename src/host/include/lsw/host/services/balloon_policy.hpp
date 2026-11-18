#pragma once

#include <QtGlobal>
#include <optional>

namespace lsw::host::services {
class BalloonPolicy {
public:
    BalloonPolicy() = delete;

    struct Reading {
        qint64 host_available_kib = 0;
        qint64 host_total_kib = 0;
        qint64 guest_max_kib = 0;
        qint64 guest_actual_kib = 0;
        std::optional<qint64> guest_usable_kib;
    };

    static constexpr qint64 kGib = 1024 * 1024;
    static constexpr qint64 kStepKib = 256 * 1024;
    static constexpr qint64 kLeastLowKib = 3 * kGib;
    static constexpr qint64 kLowDivisor = 8;
    static constexpr qint64 kLeastGuestKib = 2 * kGib;
    static constexpr qint64 kGuestHeadroomKib = kGib;
    static constexpr qint64 kGuestSpareKib = 2 * kGib;
    static constexpr qint64 kGuestSlackKib = kGib;
    static constexpr qint64 kGuestStarvedKib = kGib;
    static constexpr qint64 kGuestGrowKib = 2 * kGib;

    [[nodiscard]] static qint64 low_kib(qint64 host_total_kib) noexcept;
    [[nodiscard]] static qint64 floor_kib(const Reading& reading) noexcept;
    [[nodiscard]] static std::optional<qint64> target_kib(const Reading& reading) noexcept;
};

}  // namespace lsw::host::services
