#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestVirtiofsPath {
public:
    GuestVirtiofsPath() = delete;

    [[nodiscard]] static QString locate();
};

}
