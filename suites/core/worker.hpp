#pragma once

#include "core/check_result.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
class Worker {
public:
    explicit Worker(std::filesystem::path artifacts);

    [[nodiscard]] CheckResult run(const std::string& id, int timeout_seconds) const;

    void set_exe(const std::filesystem::path& exe) { exe_ = exe.wstring(); }

    void set_cwd(const std::filesystem::path& cwd) { cwd_ = cwd.wstring(); }

    void set_child_mode() { child_mode_ = true; }

    void set_restricted_token() { restricted_ = true; }

    void set_timeout_millis(int milliseconds) { timeout_ms_ = milliseconds; }

    void add_env(const std::wstring& name, const std::wstring& value) { env_.emplace_back(name, value); }

private:
    std::wstring exe_;
    std::wstring cwd_;
    bool child_mode_ = false;
    bool restricted_ = false;
    int timeout_ms_ = 0;
    std::vector<std::pair<std::wstring, std::wstring>> env_;
    std::filesystem::path artifacts_;
};
}  // namespace lsw::suites
