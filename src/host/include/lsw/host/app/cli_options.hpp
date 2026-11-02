#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::app {
struct CliOptions {
    bool accept_license = false;
    QString iso_path;
    QString edition_label;
    QString language_label;
    QString user_name;
    QString user_password;
    QString computer_name;
    int cpu_cores = -1;
    int memory_mib = -1;
    int disk_gib = -1;
    QString launch_app_id;
    QString activation_token;

    QStringList files;

    bool reinstall = false;

    QString screenshot_dir;

    [[nodiscard]] bool unattended() const noexcept {
        return accept_license || !iso_path.isEmpty() || !edition_label.isEmpty()
            || !user_name.isEmpty() || !user_password.isEmpty()
            || !computer_name.isEmpty() || cpu_cores > 0
            || memory_mib > 0 || disk_gib > 0;
    }
};

}
