#pragma once

#include "lsw/host/config/log_verbosity.hpp"

#include <QObject>
#include <QTimer>

namespace lsw::host::apps { class LogStore; }
namespace lsw::host::config { class ManagerSettings; class Settings; }

namespace lsw::host::services {
class HostLogging : public QObject {
    Q_OBJECT
public:
    HostLogging(config::Settings& settings, config::ManagerSettings& manager_settings,
                apps::LogStore& store, QObject* parent = nullptr);
    ~HostLogging() override;

    void start();

    [[nodiscard]] config::LogVerbosity& verbosity() noexcept { return verbosity_; }

private:
    void apply_mirror_level();

    config::Settings& settings_;
    config::ManagerSettings& manager_settings_;
    apps::LogStore& store_;
    config::LogVerbosity verbosity_;
    QTimer repeats_;
    bool started_ = false;
    bool mirror_level_from_environment_ = false;
};

}
