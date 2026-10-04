#include "lsw/host/platform/gpu_memory_usage.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lsw::host::platform {

namespace {

// One decimal number from a sysfs attribute, or nullopt.
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

}  // namespace

std::optional<GpuMemoryUsage::Reading> GpuMemoryUsage::read(const QString& pci_address) {
    if (pci_address.isEmpty()) {
        return std::nullopt;
    }
    // Each card's ``device`` is a link to its PCI function's directory,
    // whose name is the address. cardN rather than renderDN, because the
    // counters hang off the device either way and every GPU has a card.
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards =
        drm.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString& card : cards) {
        if (card.contains(QLatin1Char('-'))) {
            continue;  // a connector, card0-DP-1
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

}  // namespace lsw::host::platform
