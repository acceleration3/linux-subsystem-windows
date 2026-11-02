#pragma once

#include <QString>

namespace lsw::host::services {
class StartupHeader {
public:
    StartupHeader() = delete;

    [[nodiscard]] static QString version();

    static void write();

    [[nodiscard]] static QString libvirt_version();
};

}
