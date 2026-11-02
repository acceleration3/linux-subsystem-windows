#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::config {
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    [[nodiscard]] bool ensure_directories() const;

    [[nodiscard]] QString data_dir() const;
    [[nodiscard]] QString config_dir() const;
    [[nodiscard]] QString cache_dir() const;

    [[nodiscard]] QString disks_dir() const;
    [[nodiscard]] QString isos_dir() const;
    [[nodiscard]] QString nvram_dir() const;
    [[nodiscard]] QString logs_dir() const;
    [[nodiscard]] QString manager_log_path() const;
    [[nodiscard]] QString mirror_logs_dir() const;
    [[nodiscard]] QString vm_logs_dir() const;

    [[nodiscard]] QString vm_disk_path() const;
    [[nodiscard]] QString vm_nvram_path() const;
    [[nodiscard]] QString vm_payload_iso_path() const;
    [[nodiscard]] QString vm_install_media_path() const;
    [[nodiscard]] QString vm_avgpu_log_path() const;
    [[nodiscard]] QString driver_versions_path() const;

    [[nodiscard]] bool is_first_run() const;
    void mark_first_run_complete();

private:
    QString data_root_;
    QString config_root_;
    QString cache_root_;
};

}
