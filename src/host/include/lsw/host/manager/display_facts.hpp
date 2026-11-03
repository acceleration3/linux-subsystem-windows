#pragma once

#include "lsw/host/config/display_modes.hpp"

#include <QString>
#include <optional>

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class DisplayFacts {
public:
    [[nodiscard]] static DisplayFacts read(const services::StatusHub& status);

    [[nodiscard]] QString mode_label() const;
    [[nodiscard]] QString refresh_label() const;
    [[nodiscard]] QString scale_label() const;
    [[nodiscard]] QString summary() const;

    std::optional<config::DisplayMode> mode;
    int offered = 0;
    bool host_modes = false;
    std::optional<quint32> refresh_millihz;
    std::optional<int> scale_percent;
};

}
