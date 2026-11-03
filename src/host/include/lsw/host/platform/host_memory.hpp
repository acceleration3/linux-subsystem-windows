#pragma once

#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class HostMemory {
public:
    HostMemory() = delete;

    [[nodiscard]] static std::optional<qint64> total_mib();
};

}
