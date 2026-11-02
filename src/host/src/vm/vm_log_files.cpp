#include "lsw/host/vm/vm_log_files.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/vm_name.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace lsw::host::vm {

VmLogFiles::VmLogFiles(QString vm_logs_dir, QString libvirt_log_dir)
    : dir_(std::move(vm_logs_dir)), libvirt_dir_(std::move(libvirt_log_dir)),
      avgpu_(QDir(dir_).filePath(QStringLiteral("avgpu.log")).toStdString(),
             lsw::base::RollingFile::kDefaultLimits) {}

QString VmLogFiles::default_libvirt_log_dir() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
        .filePath(QStringLiteral("libvirt/qemu/log"));
}

QString VmLogFiles::avgpu_path() const {
    return QDir(dir_).filePath(QStringLiteral("avgpu.log"));
}

void VmLogFiles::prepare_start() {
    QDir().mkpath(dir_);
    avgpu_.roll_now();
    refresh_links();
}

void VmLogFiles::relink(const QString& link, const QString& target) {
    const QFileInfo info(link);
    if (info.isSymLink()) {
        if (info.symLinkTarget() == target) {
            return;
        }
        QFile::remove(link);
    } else if (info.exists()) {
        return;
    }
    if (!QFile::link(target, link)) {
        LSW_WARN("could not link {} to {}", link.toStdString(), target.toStdString());
    }
}

void VmLogFiles::refresh_links() {
    QDir().mkpath(dir_);
    const QString domain = QLatin1String(config::kVmName);
    relink(QDir(dir_).filePath(QStringLiteral("qemu.log")),
           QDir(libvirt_dir_).filePath(domain + QStringLiteral(".log")));

    const QFileInfoList found = QDir(libvirt_dir_)
                                    .entryInfoList({domain + QStringLiteral("-*-virtiofsd.log")},
                                                   QDir::Files, QDir::Time);
    const QString target = found.isEmpty()
                               ? QDir(libvirt_dir_).filePath(domain + QStringLiteral("-fs0-virtiofsd.log"))
                               : found.first().absoluteFilePath();
    relink(QDir(dir_).filePath(QStringLiteral("virtiofsd.log")), target);
}

}
