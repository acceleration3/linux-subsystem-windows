#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class ResizableBar {
public:
    struct Sizes {
        quint64 current_bytes = 0;
        quint64 largest_bytes = 0;
    };

    ResizableBar() = delete;

    [[nodiscard]] static std::optional<Sizes> read(const QString& pci_address);

    [[nodiscard]] static bool is_off(const Sizes& sizes, quint64 vram_bytes);
};

}
