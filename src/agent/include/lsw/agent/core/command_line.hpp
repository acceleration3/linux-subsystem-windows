#pragma once

#include <string>
#include <vector>

namespace lsw::agent::core {
struct CommandLine {
    enum class Mode { Foreground, Service, SessionHelper };

    Mode mode = Mode::Foreground;
    std::wstring pipe;
    std::wstring error;

    [[nodiscard]] static CommandLine parse(const std::vector<std::wstring>& args);
};

}
