#include "sanity/err/spin_device.hpp"

#include <algorithm>
#include <chrono>

namespace lsw::suites::sanity::err {

std::unique_ptr<SpinDevice> SpinDevice::make(const std::string& api) {
    if (api == "d3d11") {
        return make_spin_d3d11();
    }
    if (api == "d3d12") {
        return make_spin_d3d12();
    }
    if (api == "vulkan") {
        return make_spin_vulkan();
    }
    if (api == "opencl") {
        return make_spin_opencl();
    }
    return nullptr;
}

bool SpinDevice::calibrate(SpinDevice& device, double target_ms, std::uint32_t& outer, double& took_ms, std::string& error) {
    outer = 1;
    double measured = 0;
    for (;;) {
        std::string detail;
        if (!device.submit(outer, error)) {
            return false;
        }
        const auto begin = std::chrono::steady_clock::now();
        const SpinState state = device.poll(30000, detail);
        measured = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        if (state != SpinState::Done) {
            error = "the calibration run with outer count " + std::to_string(outer) + " did not complete: " + detail;
            return false;
        }
        if (measured >= 100.0 || outer >= (1u << 24)) {
            break;
        }
        outer *= 4;
    }
    const double scaled = static_cast<double>(outer) * target_ms / std::max(measured, 1.0);
    outer = std::max<std::uint32_t>(static_cast<std::uint32_t>(std::min(scaled, 4.0e9)), 1);
    took_ms = measured;
    return true;
}

std::uint32_t SpinDevice::expected(std::uint32_t thread, std::uint64_t iterations) {
    std::uint32_t multiplier = 1664525u;
    std::uint32_t increment = 1013904223u;
    std::uint32_t total_multiplier = 1u;
    std::uint32_t total_increment = 0u;
    std::uint64_t remaining = iterations;
    while (remaining != 0) {
        if ((remaining & 1u) != 0) {
            total_increment = total_increment * multiplier + increment;
            total_multiplier *= multiplier;
        }
        increment = increment * (multiplier + 1u);
        multiplier *= multiplier;
        remaining >>= 1;
    }
    const std::uint32_t start = thread * 2654435761u + 12345u;
    return start * total_multiplier + total_increment;
}

}  // namespace lsw::suites::sanity::err
