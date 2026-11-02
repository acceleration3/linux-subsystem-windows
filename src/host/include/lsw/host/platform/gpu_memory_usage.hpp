#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class GpuMemoryUsage {
public:
    struct Reading {
        quint64 used_bytes = 0;
        quint64 total_bytes = 0;
    };

    GpuMemoryUsage() = delete;

    [[nodiscard]] static std::optional<Reading> read(const QString& pci_address);
};

}
