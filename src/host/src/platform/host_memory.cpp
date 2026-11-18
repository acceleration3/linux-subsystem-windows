#include "lsw/host/platform/host_memory.hpp"

#include <QFile>
#include <QString>

namespace lsw::host::platform {

namespace {
constexpr qint64 kKibPerMib = 1024;

std::optional<qint64> meminfo_kib(QByteArrayView field) {
    QFile meminfo{QStringLiteral("/proc/meminfo")};
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    for (const QByteArray& line : meminfo.readAll().split('\n')) {
        if (line.startsWith(field) && line.size() > field.size() && line.at(field.size()) == ':') {
            const qint64 kib =
                line.mid(field.size() + 1).trimmed().split(' ').value(0).toLongLong();
            return kib > 0 ? std::optional<qint64>{kib} : std::nullopt;
        }
    }
    return std::nullopt;
}
}  // namespace

std::optional<qint64> HostMemory::total_mib() {
    const auto kib = meminfo_kib("MemTotal");
    return kib ? std::optional<qint64>{*kib / kKibPerMib} : std::nullopt;
}

std::optional<qint64> HostMemory::available_mib() {
    const auto kib = meminfo_kib("MemAvailable");
    return kib ? std::optional<qint64>{*kib / kKibPerMib} : std::nullopt;
}
}
