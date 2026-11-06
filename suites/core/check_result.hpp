#pragma once

#include "core/json.hpp"
#include "core/outcome.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct CheckResult {
    std::string id;
    std::string suite;
    std::vector<std::string> tags;
    Status status = Status::Crash;
    std::string reason;
    long long ms = 0;
    std::uint32_t pid = 0;
    std::vector<std::string> artifacts;
    std::vector<std::string> notes;
    Json measures = Json::object();
    std::string output;
    std::string started;
};
}  // namespace lsw::suites
