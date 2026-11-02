#include "lsw/host/platform/gpu_memory_usage.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lsw::host::platform {

namespace {
std::optional<quint64> read_number(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 value = file.readAll().trimmed().toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

}

std::optional<GpuMemoryUsage::Reading> GpuMemoryUsage::read(const QString& pci_address) {
    if (pci_address.isEmpty()) {
        return std::nullopt;
    }
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards =
        drm.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString& card : cards) {
        if (card.contains(QLatin1Char('-'))) {
            continue;
        }
        const QString device = drm.filePath(card + QStringLiteral("/device"));
        if (QFileInfo(QFileInfo(device).canonicalFilePath()).fileName()
                .compare(pci_address, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto used = read_number(device + QStringLiteral("/mem_info_vram_used"));
        const auto total = read_number(device + QStringLiteral("/mem_info_vram_total"));
        if (!used || !total || *total == 0) {
            return std::nullopt;
        }
        return Reading{*used, *total};
    }
    return std::nullopt;
}

}
