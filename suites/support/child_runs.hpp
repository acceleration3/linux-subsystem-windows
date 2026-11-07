#pragma once

#include "core/check_result.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites {
struct ChildRunOptions {
    std::filesystem::path cwd;
    bool restricted = false;
    int timeout_seconds = 60;
};

class ChildRuns {
public:
    [[nodiscard]] static std::filesystem::path own_exe();

    [[nodiscard]] static std::filesystem::path sibling_exe();

    [[nodiscard]] static CheckResult run_self(const std::string& id, const ChildRunOptions& options);

    [[nodiscard]] static bool run_sibling(const std::string& id, const ChildRunOptions& options,
                                          CheckResult& result, std::string& error);

    [[nodiscard]] static std::string text(const CheckResult& result, const std::string& key);

    [[nodiscard]] static long long number(const CheckResult& result, const std::string& key);

    [[nodiscard]] static bool has(const CheckResult& result, const std::string& key);
};
}  // namespace lsw::suites
