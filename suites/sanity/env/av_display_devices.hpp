#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::sanity::env {
struct AvDisplayDevice {
    std::string name;
    std::string hardware_id;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::uint32_t problem_code = 0;
    bool started = false;
    bool status_known = false;
};

class AvDisplayDevices {
public:
    [[nodiscard]] static std::vector<AvDisplayDevice> enumerate();

    static constexpr std::uint32_t kSubsystem = 0x52561AF4;
};
}  // namespace lsw::suites::sanity::env
