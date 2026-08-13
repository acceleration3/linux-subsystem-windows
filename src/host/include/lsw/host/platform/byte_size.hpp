#pragma once

#include <QString>
#include <QtGlobal>

namespace lsw::host::platform {

// Renders byte counts for humans: "512 B", "1.4 KiB", "750.2 MiB".
class ByteSize {
public:
    ByteSize() = delete;

    [[nodiscard]] static QString human(quint64 bytes);
};

}  // namespace lsw::host::platform
