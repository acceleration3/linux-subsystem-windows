#pragma once

#include <QString>

namespace lsw::host::platform {
class MirrorExePaths {
public:
    MirrorExePaths() = delete;

    [[nodiscard]] static QString host();

    [[nodiscard]] static QString guest();
};

}
