#pragma once

#include "core/check_result.hpp"

#include <filesystem>
#include <mutex>

namespace lsw::suites {
class ResultLog {
public:
    ResultLog() = default;
    ResultLog(const ResultLog&) = delete;
    ResultLog& operator=(const ResultLog&) = delete;
    ~ResultLog();

    [[nodiscard]] bool open(const std::filesystem::path& path);

    void append(const CheckResult& result);

private:
    std::mutex mutex_;
    void* handle_ = nullptr;
};
}  // namespace lsw::suites
