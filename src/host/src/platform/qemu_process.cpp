#include "lsw/host/platform/qemu_process.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

namespace {
constexpr qint64 kMaxCmdline = 256 * 1024;
}

bool QemuProcess::runs_domain(const QByteArray& cmdline, const QByteArray& uuid) {
    const QList<QByteArray> args = cmdline.split('\0');
    for (qsizetype i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == "-uuid" && args.at(i + 1).compare(uuid, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

qint64 QemuProcess::pid_of(const QUuid& uuid) {
    if (uuid.isNull()) {
        return -1;
    }
    const QByteArray wanted = uuid.toByteArray(QUuid::WithoutBraces);
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0) {
            continue;
        }
        QFile file(proc.filePath(entry + QStringLiteral("/cmdline")));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (runs_domain(file.read(kMaxCmdline), wanted)) {
            return pid;
        }
    }
    return -1;
}

}
