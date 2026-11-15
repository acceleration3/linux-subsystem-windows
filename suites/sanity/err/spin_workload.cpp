#include "sanity/err/spin_workload.hpp"

#include "sanity/err/spin_device.hpp"
#include "support/robust_child.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>

namespace lsw::suites::sanity::err {

namespace {

constexpr double kTargetMs = 10000.0;
constexpr int kLongWaitMs = 60000;
constexpr int kForeverWaitMs = 20000;

double elapsed_ms(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}

std::string state_text(SpinState state) {
    switch (state) {
    case SpinState::Done:
        return "completed";
    case SpinState::Lost:
        return "lost";
    case SpinState::Pending:
        return "pending";
    case SpinState::Error:
        return "error";
    }
    return "error";
}

}  // namespace

Outcome SpinWorkload::run(Context& context, const std::string& api, bool forever) {
    SpinDevice* device = SpinDevice::make(api).release();
    if (device == nullptr) {
        return Outcome::fail("no spin workload for " + api);
    }
    std::string error;
    if (!device->open(forever, error)) {
        context.measure("spin.detail", error);
        return Context::skip(api + " is unavailable: " + error);
    }
    std::string detail;
    std::uint32_t outer = 1;
    if (!forever) {
        double calibrated = 0;
        for (;;) {
            if (!device->submit(outer, error)) {
                return Outcome::fail("calibration submit: " + error);
            }
            const auto begin = std::chrono::steady_clock::now();
            const SpinState state = device->poll(30000, detail);
            calibrated = elapsed_ms(begin);
            if (state != SpinState::Done) {
                return Outcome::fail("the calibration run with outer count " + std::to_string(outer) + " ended " + state_text(state) + ": " + detail);
            }
            if (calibrated >= 100.0 || outer >= (1u << 24)) {
                break;
            }
            outer *= 4;
        }
        const double scaled = static_cast<double>(outer) * kTargetMs / std::max(calibrated, 1.0);
        outer = static_cast<std::uint32_t>(std::min(scaled, 4.0e9));
        outer = std::max<std::uint32_t>(outer, 1);
        context.measure("spin.calibrated_ms", calibrated);
        context.measure("spin.outer", outer);
    }
    if (!device->submit(outer, error)) {
        return Outcome::fail("submit: " + error);
    }
    RobustChild::ready();
    const auto begin = std::chrono::steady_clock::now();
    const SpinState state = device->poll(forever ? kForeverWaitMs : kLongWaitMs, detail);
    const double took = elapsed_ms(begin);
    context.measure("spin.result", state_text(state));
    context.measure("spin.ms", took);
    context.measure("spin.detail", detail);
    if (state == SpinState::Error) {
        return Outcome::fail("the guest saw an undocumented failure after " + std::to_string(static_cast<long long>(took)) + " ms: " + detail);
    }
    if (state == SpinState::Lost) {
        return Outcome::ok();
    }
    if (forever) {
        if (state == SpinState::Done) {
            return Outcome::fail("a shader that waits for a value nobody writes completed after " + std::to_string(static_cast<long long>(took)) + " ms");
        }
        return Outcome::ok();
    }
    if (state == SpinState::Pending) {
        return Outcome::fail("a bounded shader was still running after " + std::to_string(kLongWaitMs) + " ms and no loss was reported");
    }
    std::vector<std::uint32_t> words;
    if (!device->read(words, error)) {
        return Outcome::fail("reading the results failed: " + error);
    }
    const std::uint64_t iterations = static_cast<std::uint64_t>(SpinDevice::kInner) * outer;
    for (std::uint32_t thread = 0; thread < SpinDevice::kThreads && thread < words.size(); ++thread) {
        if (words[thread] != SpinDevice::expected(thread, iterations)) {
            return Outcome::fail("thread " + std::to_string(thread) + " computed " + std::to_string(words[thread]) + " instead of " +
                                 std::to_string(SpinDevice::expected(thread, iterations)));
        }
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::err
