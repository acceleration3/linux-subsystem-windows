#include "lsw/host/services/guest_update_steps.hpp"

namespace lsw::host::services {

const std::array<GuestUpdateSteps::Entry, 8>& GuestUpdateSteps::all() {
    static const std::array<Entry, 8> steps{{
        {"verify", "Checking the files", "checked"},
        {"stop", "Stopping the agent", "done"},
        {"agent and mirror", "Agent and window mirror", "updated"},
        {"driver", "GPU driver", "updated"},
        {"loaders", "Graphics loaders", "updated"},
        {"shared folders", "Shared folders", "updated"},
        {"service and firewall", "Service and firewall", "updated"},
        {"start", "Starting the agent", "done"},
    }};
    return steps;
}

QString GuestUpdateSteps::title_of(const QString& key) {
    for (const auto& entry : all()) {
        if (key == QLatin1StringView(entry.key)) {
            return QString::fromLatin1(entry.title);
        }
    }
    QString title = key;
    if (!title.isEmpty()) {
        title[0] = title[0].toUpper();
    }
    return title;
}

QString GuestUpdateSteps::done_of(const QString& key) {
    for (const auto& entry : all()) {
        if (key == QLatin1StringView(entry.key)) {
            return QString::fromLatin1(entry.done);
        }
    }
    return QStringLiteral("done");
}

}
