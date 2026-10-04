#include "lsw/host/config/settings.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/vm_name.hpp"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace lsw::host::config {

namespace {

constexpr auto kAppDir = "lsw";

[[nodiscard]] QString join(const QString& root, const QString& sub) {
    return QDir(root).filePath(sub);
}

[[nodiscard]] bool mkpath(const QString& path) {
    if (QDir().mkpath(path)) {
        return true;
    }
    LSW_ERROR("failed to create dir {}", path.toStdString());
    return false;
}

}  // namespace

Settings::Settings(QObject* parent) : QObject(parent) {
    data_root_ = join(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), QString{});
    if (!data_root_.endsWith(kAppDir)) {
        data_root_ =
            join(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation), kAppDir);
    }
    config_root_ =
        join(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), kAppDir);
    cache_root_ =
        join(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation), kAppDir);
}

bool Settings::ensure_directories() const {
    return mkpath(data_root_) && mkpath(config_root_) && mkpath(cache_root_) &&
           mkpath(disks_dir()) && mkpath(isos_dir()) && mkpath(nvram_dir()) && mkpath(logs_dir());
}

QString Settings::data_dir() const {
    return data_root_;
}

QString Settings::config_dir() const {
    return config_root_;
}

QString Settings::cache_dir() const {
    return cache_root_;
}

QString Settings::disks_dir() const {
    return join(data_root_, "disks");
}

QString Settings::isos_dir() const {
    return join(data_root_, "isos");
}

QString Settings::nvram_dir() const {
    return join(data_root_, "nvram");
}

QString Settings::logs_dir() const {
    return join(data_root_, "logs");
}

QString Settings::vm_disk_path() const {
    return join(disks_dir(), QStringLiteral("%1.qcow2").arg(QLatin1String(kVmName)));
}

QString Settings::vm_nvram_path() const {
    return join(nvram_dir(), QStringLiteral("%1.fd").arg(QLatin1String(kVmName)));
}

QString Settings::vm_payload_iso_path() const {
    return join(isos_dir(), QStringLiteral("%1-payload.iso").arg(QLatin1String(kVmName)));
}

QString Settings::vm_avgpu_log_path() const {
    return join(logs_dir(), QStringLiteral("%1-avgpu.log").arg(QLatin1String(kVmName)));
}

bool Settings::is_first_run() const {
    QSettings s{join(config_root_, "manager.ini"), QSettings::IniFormat};
    return !s.value("setup/wizard_complete", false).toBool();
}

void Settings::mark_first_run_complete() {
    QSettings s{join(config_root_, "manager.ini"), QSettings::IniFormat};
    s.setValue("setup/wizard_complete", true);
}

}  // namespace lsw::host::config
