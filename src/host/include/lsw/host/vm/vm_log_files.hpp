#pragma once

#include "lsw/base/rolling_file.hpp"

#include <QString>

namespace lsw::host::vm {
class VmLogFiles {
public:
    explicit VmLogFiles(QString vm_logs_dir, QString libvirt_log_dir = default_libvirt_log_dir());

    [[nodiscard]] static QString default_libvirt_log_dir();

    [[nodiscard]] QString avgpu_path() const;

    void prepare_start();

    void refresh_links();

private:
    static void relink(const QString& link, const QString& target);

    QString dir_;
    QString libvirt_dir_;
    lsw::base::RollingFile avgpu_;
};

}
