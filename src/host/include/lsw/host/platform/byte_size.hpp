#pragma once

#include <QString>
#include <QtGlobal>

namespace lsw::host::platform {
class ByteSize {
public:
    ByteSize() = delete;

    [[nodiscard]] static QString human(quint64 bytes);
};

}
