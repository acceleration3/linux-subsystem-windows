#include "lsw/host/platform/host_memory.hpp"

#include <QFile>
#include <QString>

namespace lsw::host::platform {

std::optional<qint64> HostMemory::total_mib() {
    QFile meminfo{QStringLiteral("/proc/meminfo")};
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    while (!meminfo.atEnd()) {
        const QByteArray line = meminfo.readLine();
        if (line.startsWith("MemTotal:")) {
            const qint64 kib = line.mid(9).trimmed().split(' ').value(0).toLongLong();
            return kib > 0 ? std::optional<qint64>{kib / 1024} : std::nullopt;
        }
    }
    return std::nullopt;
}

}
