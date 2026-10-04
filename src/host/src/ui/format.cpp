#include "lsw/host/ui/format.hpp"

#include "lsw/host/platform/byte_size.hpp"

namespace lsw::host::ui {

namespace {

constexpr double kMib = 1024.0 * 1024.0;
constexpr double kGib = 1024.0 * 1024.0 * 1024.0;

}  // namespace

double Format::to_gib(qint64 bytes) {
    return static_cast<double>(bytes) / kGib;
}

QString Format::gib(qint64 bytes, int decimals) {
    return QStringLiteral("%1 GiB").arg(to_gib(bytes), 0, 'f', decimals);
}

QString Format::bytes(quint64 bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return gib(static_cast<qint64>(bytes));
    }
    return platform::ByteSize::human(bytes);
}

QString Format::kib(quint64 kib) {
    if (kib >= 1024 && (kib % 1024) == 0) {
        return QStringLiteral("%1 MiB").arg(kib / 1024);
    }
    return QStringLiteral("%1 KiB").arg(kib);
}

QString Format::mib_per_second(double bytes_per_second) {
    return QStringLiteral("%1 MiB/s").arg(bytes_per_second / kMib, 0, 'f', 1);
}

QString Format::clock(qint64 seconds, int minute_digits) {
    if (seconds < 0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, minute_digits, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace lsw::host::ui
