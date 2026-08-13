#include "lsw/host/config/settings.hpp"

#include "lsw/base/log.hpp"

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
           mkpath(disks_dir()) && mkpath(isos_dir()) && mkpath(nvram_dir());
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

QString Settings::last_iso_path() const {
    QSettings s{join(config_root_, "manager.ini"), QSettings::IniFormat};
    return s.value("paths/last_iso").toString();
}

void Settings::set_last_iso_path(const QString& path) {
    QSettings s{join(config_root_, "manager.ini"), QSettings::IniFormat};
    s.setValue("paths/last_iso", path);
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
