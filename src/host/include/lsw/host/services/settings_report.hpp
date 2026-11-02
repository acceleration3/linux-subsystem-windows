#pragma once

#include <QString>

namespace lsw::host::config {
class ManagerSettings;
class Settings;
}

namespace lsw::host::services {

class SettingsReport {
public:
    SettingsReport() = delete;

    [[nodiscard]] static bool secret(const QString& key);

    [[nodiscard]] static QString render(const config::ManagerSettings& manager,
                                        const config::Settings& settings);
};

}
