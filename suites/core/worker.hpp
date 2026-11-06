#pragma once

#include "core/check_result.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites {
class Worker {
public:
    explicit Worker(std::filesystem::path artifacts);

    [[nodiscard]] CheckResult run(const std::string& id, int timeout_seconds) const;

private:
    std::wstring exe_;
    std::filesystem::path artifacts_;
};
}  // namespace lsw::suites
