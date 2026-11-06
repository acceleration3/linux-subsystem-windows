#pragma once

#include "core/outcome.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lsw::suites {
class Context;

struct Check {
    std::string id;
    std::vector<std::string> tags;
    bool exclusive = false;
    int timeout_seconds = 0;
    std::function<Outcome(Context&)> run;
};
}  // namespace lsw::suites
