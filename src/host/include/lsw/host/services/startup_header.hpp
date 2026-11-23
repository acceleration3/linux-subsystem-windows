#pragma once

#include <QString>

namespace lsw::host::services {
class StartupHeader {
public:
    StartupHeader() = delete;

    [[nodiscard]] static QString version();

    [[nodiscard]] static QString number();

    static void write();

    [[nodiscard]] static QString libvirt_version();
};

}
