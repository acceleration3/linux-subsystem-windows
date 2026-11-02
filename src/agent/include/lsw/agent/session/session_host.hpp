#pragma once

#include <string>

namespace lsw::agent::session {
class SessionHost {
public:
    SessionHost() = delete;

    [[nodiscard]] static int run(const std::wstring& pipe_name);
};

}
