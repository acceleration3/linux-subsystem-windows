#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::win {

class RunAs;

class MirrorProcess {
public:
    MirrorProcess() = delete;

    [[nodiscard]] static unsigned long start(const std::string& exe, std::uint16_t port,
                                             const std::string& log_path,
                                             const std::string& log_level, const RunAs& as,
                                             std::string* error_out);
};

}
