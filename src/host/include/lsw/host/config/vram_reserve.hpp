#pragma once

#include <QString>
#include <QtGlobal>

namespace lsw::host::config {
class VramReserve {
public:
    static constexpr quint64 kAutoFloorMib = 1024;
    static constexpr quint64 kLeastReserveMib = 256;
    static constexpr quint64 kLeastGuestMib = 512;

    struct Split {
        quint64 reserve_mib = 0;
        quint64 guest_cap_mib = 0;
    };

    VramReserve() = delete;

    [[nodiscard]] static Split resolve(int setting_mib, quint64 total_mib);

    [[nodiscard]] static QString label(int setting_mib);
};

}
