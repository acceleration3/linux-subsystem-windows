#include "lsw/host/platform/byte_size.hpp"

namespace lsw::host::platform {

QString ByteSize::human(quint64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double value = static_cast<double>(bytes);
    if (bytes < 1024ULL * 1024ULL) {
        return QStringLiteral("%1 KiB").arg(value / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MiB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
}

}  // namespace lsw::host::platform
