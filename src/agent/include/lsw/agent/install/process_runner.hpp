#pragma once

#include "lsw/agent/install/process_outcome.hpp"

#include <string>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::install {
class ProcessRunner {
public:
    ProcessRunner() = delete;

    static constexpr unsigned long kNoTimeout = 0xFFFFFFFFUL;

    [[nodiscard]] static ProcessOutcome run(const std::wstring& command_line,
                                            const win::RunAs& as,
                                            unsigned long timeout_ms = kNoTimeout);

    [[nodiscard]] static ProcessOutcome run(const std::wstring& command_line,
                                            unsigned long timeout_ms = kNoTimeout);
};

}
