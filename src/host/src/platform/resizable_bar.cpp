#include "lsw/host/platform/resizable_bar.hpp"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <algorithm>
#include <bit>

namespace lsw::host::platform {

namespace {
constexpr int kBarCount = 6;
constexpr quint64 kMiB = 1ULL << 20;

std::optional<quint64> read_hex(const QString& text) {
    bool ok = false;
    const quint64 value = text.trimmed().toULongLong(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

std::optional<quint64> largest_supported(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const auto mask = read_hex(QString::fromLatin1(file.readAll()));
    if (!mask || *mask == 0) {
        return std::nullopt;
    }
    return kMiB << (std::bit_width(*mask) - 1);
}

std::optional<quint64> current_size(const QString& path, int bar) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));
    if (bar >= lines.size()) {
        return std::nullopt;
    }
    const QStringList fields = lines.at(bar).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() < 2) {
        return std::nullopt;
    }
    const auto start = read_hex(fields.at(0));
    const auto end = read_hex(fields.at(1));
    if (!start || !end) {
        return std::nullopt;
    }
    if (*start == 0 && *end == 0) {
        return 0;
    }
    if (*end < *start) {
        return std::nullopt;
    }
    return *end - *start + 1;
}

}

std::optional<ResizableBar::Sizes> ResizableBar::read(const QString& pci_address) {
    if (pci_address.isEmpty()) {
        return std::nullopt;
    }
    const QString device = QStringLiteral("/sys/bus/pci/devices/") + pci_address.toLower();
    std::optional<Sizes> best;
    for (int bar = 0; bar < kBarCount; ++bar) {
        const auto largest =
            largest_supported(device + QStringLiteral("/resource%1_resize").arg(bar));
        if (!largest) {
            continue;
        }
        const auto current = current_size(device + QStringLiteral("/resource"), bar);
        if (!current) {
            continue;
        }
        if (!best || *largest > best->largest_bytes) {
            best = Sizes{*current, *largest};
        }
    }
    return best;
}

bool ResizableBar::is_off(const Sizes& sizes, quint64 vram_bytes) {
    quint64 target = sizes.largest_bytes;
    if (vram_bytes > 0) {
        target = std::min(target, std::bit_ceil(vram_bytes));
    }
    return sizes.current_bytes < target;
}

}
