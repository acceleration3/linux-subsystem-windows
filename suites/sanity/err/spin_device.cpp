#include "sanity/err/spin_device.hpp"

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
