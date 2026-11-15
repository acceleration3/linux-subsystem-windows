#include "sanity/sync/wait_checks.hpp"

#include "sanity/err/spin_device.hpp"
#include "sanity/sync/api_children.hpp"
#include "sanity/sync/sync_device.hpp"

#include <windows.h>

#include <chrono>

namespace lsw::suites::sanity::sync {

namespace {

using err::SpinDevice;
using err::SpinState;

constexpr double kSlackMs = 500.0;
const std::vector<std::string> kApis = {"d3d11", "d3d12", "vulkan", "opencl"};

double since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}  // namespace

Outcome WaitChecks::timeout_honoured(Context& context) {
    return ApiChildren::run(context, kApis, [](Context& inner, const std::string& api) {
        std::unique_ptr<SpinDevice> device = SpinDevice::make(api);
        std::string error;
        if (device == nullptr || !device->open(true, error)) {
            return Context::skip(api + " is unavailable: " + error);
        }
        SpinDevice* leaked = device.release();
        if (!leaked->submit(1, error)) {
            return Outcome::fail("submit: " + error);
        }
        for (int timeout : {30, 120, 400}) {
            std::string detail;
            const auto begin = std::chrono::steady_clock::now();
            const SpinState state = leaked->wait_native(timeout, detail);
            const double took = since(begin);
            inner.measure("wait_" + std::to_string(timeout) + "_ms", took);
            if (state != SpinState::Pending) {
                return Outcome::fail("a wait of " + std::to_string(timeout) + " ms on work that never completes returned state " + std::to_string(static_cast<int>(state)) + " after " +
                                     std::to_string(took) + " ms: " + detail);
            }
            if (took > timeout + kSlackMs) {
                return Outcome::fail("a wait of " + std::to_string(timeout) + " ms took " + std::to_string(took) + " ms");
            }
            if (took < timeout - 30.0) {
                return Outcome::fail("a wait of " + std::to_string(timeout) + " ms returned its timeout result after only " + std::to_string(took) + " ms");
            }
        }
        return Outcome::ok();
    }, 120);
}

Outcome WaitChecks::zero_timeout_poll(Context& context) {
    return ApiChildren::run(context, kApis, [](Context& inner, const std::string& api) {
        std::unique_ptr<SpinDevice> device = SpinDevice::make(api);
        std::string error;
        if (device == nullptr || !device->open(false, error)) {
            return Context::skip(api + " is unavailable: " + error);
        }
        std::uint32_t outer = 0;
        double calibrated = 0;
        if (!SpinDevice::calibrate(*device, 600.0, outer, calibrated, error)) {
            return Outcome::fail(error);
        }
        inner.measure("outer", outer);
        if (!device->submit(outer, error)) {
            return Outcome::fail("submit: " + error);
        }
        std::string detail;
        auto begin = std::chrono::steady_clock::now();
        SpinState state = device->wait_native(0, detail);
        double took = since(begin);
        if (state != SpinState::Pending) {
            return Outcome::fail("a zero-timeout wait right after submitting about 600 ms of work did not report not-ready (state " + std::to_string(static_cast<int>(state)) + "): " + detail);
        }
        if (took > 100.0) {
            return Outcome::fail("a zero-timeout wait took " + std::to_string(took) + " ms");
        }
        if (device->poll(30000, detail) != SpinState::Done) {
            return Outcome::fail("the work did not complete: " + detail);
        }
        begin = std::chrono::steady_clock::now();
        state = device->wait_native(0, detail);
        took = since(begin);
        if (state != SpinState::Done) {
            return Outcome::fail("a zero-timeout wait after the work ended did not report ready (state " + std::to_string(static_cast<int>(state)) + "): " + detail);
        }
        if (took > 100.0) {
            return Outcome::fail("a zero-timeout wait after completion took " + std::to_string(took) + " ms");
        }
        std::vector<std::uint32_t> words;
        if (!device->read(words, error)) {
            return Outcome::fail(error);
        }
        for (std::uint32_t t = 0; t < SpinDevice::kThreads && t < words.size(); ++t) {
            if (words[t] != SpinDevice::expected(t, static_cast<std::uint64_t>(outer) * SpinDevice::kInner)) {
                return Outcome::fail("thread " + std::to_string(t) + " result is wrong after the polls");
            }
        }
        return Outcome::ok();
    }, 180);
}

Outcome WaitChecks::signal_before_wait(Context& context) {
    return ApiChildren::run(context, kApis, [](Context& inner, const std::string& api) {
        std::unique_ptr<SyncDevice> device = SyncDevice::make(api);
        std::string error;
        if (device == nullptr || !device->open(error)) {
            return Context::skip(api + " is unavailable: " + error);
        }
        std::string detail;
        for (int round = 0; round < 20; ++round) {
            if (!device->set(0, static_cast<std::uint32_t>(round), error) || !device->signal(error)) {
                return Outcome::fail(error);
            }
            if (device->wait(20000, detail) != SyncState::Done) {
                return Outcome::fail("the first wait did not complete: " + detail);
            }
            for (int timeout : {0, 5000}) {
                const auto begin = std::chrono::steady_clock::now();
                const SyncState state = device->wait(timeout, detail);
                const double took = since(begin);
                inner.measure("again_" + std::to_string(timeout) + "_ms", took);
                if (state != SyncState::Done) {
                    return Outcome::fail("waiting on an already signalled fence with timeout " + std::to_string(timeout) + " ms did not return ready: " + detail);
                }
                if (took > 100.0) {
                    return Outcome::fail("waiting on an already signalled fence took " + std::to_string(took) + " ms");
                }
            }
        }
        return Outcome::ok();
    }, 120);
}

}  // namespace lsw::suites::sanity::sync
