#include "lsw/host/manager/display_facts.hpp"

#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <algorithm>

namespace lsw::host::manager {

DisplayFacts DisplayFacts::read(const services::StatusHub& status) {
    DisplayFacts facts;
    const auto& graphics = status.graphics();
    if (!status.vm().running()) {
        return facts;
    }
    QStringList offered;
    if (graphics.live_modes.has_value()) {
        offered = config::DisplayModes::normalized(
            graphics.live_modes->split(QLatin1Char(','), Qt::SkipEmptyParts));
    }
    facts.offered = !offered.isEmpty() ? static_cast<int>(offered.size())
                                       : std::max(graphics.log_facts.modes_taken, 0);
    const QString preferred =
        !offered.isEmpty() ? offered.first() : graphics.log_facts.preferred_mode;
    facts.mode = config::DisplayModes::parse(preferred);
    facts.host_modes = !facts.mode && graphics.live_modes.has_value() && offered.isEmpty();
    facts.refresh_millihz = graphics.live_refresh;
    facts.scale_percent = graphics.scale_percent;
    return facts;
}

QString DisplayFacts::mode_label() const {
    if (mode) {
        return mode->label();
    }
    return host_modes ? QStringLiteral("host monitors' modes") : QStringLiteral("-");
}

QString DisplayFacts::refresh_label() const {
    return config::DisplayRefresh::label(refresh_millihz);
}

QString DisplayFacts::scale_label() const {
    return scale_percent ? QStringLiteral("%1 %").arg(*scale_percent) : QStringLiteral("-");
}

QString DisplayFacts::summary() const {
    return QStringLiteral("%1 · %2 · %3").arg(mode_label(), refresh_label(), scale_label());
}

}
