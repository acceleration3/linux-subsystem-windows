#pragma once

#include <string>

namespace lsw::agent::install {
struct ProcessOutcome {
    bool completed = false;
    int exit_code = 0;
    std::string error;
};

}
