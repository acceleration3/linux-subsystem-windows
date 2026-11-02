#pragma once

#include <QtGlobal>

namespace lsw::host::platform {
class KWinActivation {
public:
    KWinActivation() = delete;

    [[nodiscard]] static bool activate(qint64 pid);
};

}
