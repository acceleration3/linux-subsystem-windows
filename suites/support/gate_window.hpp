#pragma once

#include "core/check_result.hpp"
#include "core/context.hpp"
#include "core/outcome.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace lsw::suites {
class GateWindow {
public:
    static constexpr int kHoldMs = 3000;
    static constexpr long long kProbeLimitUs = 1500000;

    explicit GateWindow(std::string check_id);
    GateWindow(const GateWindow&) = delete;
    GateWindow& operator=(const GateWindow&) = delete;
    ~GateWindow();

    void open(Problems& problems);

    void begin();

    [[nodiscard]] long long elapsed_ms() const;

    void sleep_until(int milliseconds) const;

    void sleep_until_release() const { sleep_until(kHoldMs); }

    void close(Context& context, Problems& problems, const std::string& label, long long limit_us = kProbeLimitUs);

    [[nodiscard]] static Outcome peer(Context& context);

private:
    void stop();

    std::string id_;
    HANDLE ready_ = nullptr;
    HANDLE stop_ = nullptr;
    std::thread thread_;
    CheckResult result_;
    bool started_ = false;
    std::chrono::steady_clock::time_point begin_;
};
}  // namespace lsw::suites
