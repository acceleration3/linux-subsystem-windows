#pragma once

#include <QString>

namespace lsw::host::platform {
class AgentExePath {
public:
    AgentExePath() = delete;

    [[nodiscard]] static QString locate();
};

}
