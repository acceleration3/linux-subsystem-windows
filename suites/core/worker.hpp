#pragma once

#include "core/check_result.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites {
class Worker {
public:
    explicit Worker(std::filesystem::path artifacts);

    [[nodiscard]] CheckResult run(const std::string& id, int timeout_seconds) const;

    void set_exe(const std::filesystem::path& exe) { exe_ = exe.wstring(); }

    void set_cwd(const std::filesystem::path& cwd) { cwd_ = cwd.wstring(); }

    void set_child_mode() { child_mode_ = true; }

    void set_restricted_token() { restricted_ = true; }

private:
    std::wstring exe_;
    std::wstring cwd_;
    bool child_mode_ = false;
    bool restricted_ = false;
    std::filesystem::path artifacts_;
};
}  // namespace lsw::suites
