#pragma once

#include <string>

namespace lsw::agent::install {

// Result of running a child process to completion.
//
// ``completed`` false means the process never ran or was killed on
// timeout — distinct from "ran and returned non-zero", which the jobs
// report to the user very differently.
struct ProcessOutcome {
    bool completed = false;
    int exit_code = 0;
    std::string error;
};

}  // namespace lsw::agent::install
