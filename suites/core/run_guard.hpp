#pragma once

#include <string>

namespace lsw::suites {
class RunGuard {
public:
    RunGuard() = default;
    RunGuard(const RunGuard&) = delete;
    RunGuard& operator=(const RunGuard&) = delete;
    ~RunGuard();

    [[nodiscard]] bool acquire(bool exclusive, std::string& why);

    [[nodiscard]] static std::wstring marker_path();

private:
    void* handle_ = nullptr;
};
}  // namespace lsw::suites
