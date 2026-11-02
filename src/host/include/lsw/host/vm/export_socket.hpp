#pragma once

#include <QString>

namespace lsw::host::vm {
class ExportSocket {
public:
    ExportSocket() = delete;

    [[nodiscard]] static QString path();
};

}
