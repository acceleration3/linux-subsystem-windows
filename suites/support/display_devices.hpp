#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct DisplayDevice {
    std::string name;
    std::wstring instance_id;
    std::wstring hardware_ids;
    std::wstring driver_key;
    std::string driver_version;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::uint32_t subsystem = 0;
    std::uint32_t bus = 0;
    std::uint32_t address = 0;
    std::uint32_t problem_code = 0;
    bool started = false;
    bool status_known = false;
    bool accel = false;

    [[nodiscard]] std::wstring class_key() const;
};

class DisplayDevices {
public:
    [[nodiscard]] static std::vector<DisplayDevice> enumerate(bool present_only);
};
}  // namespace lsw::suites
