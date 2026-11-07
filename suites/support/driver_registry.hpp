#pragma once

#include "support/display_devices.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
struct DriverFiles {
    std::vector<std::wstring> umd_own;
    std::vector<std::wstring> umd_other;
    std::vector<std::wstring> vulkan_own;
    std::vector<std::wstring> vulkan_other;
    std::vector<std::wstring> opencl_own;
    std::vector<std::wstring> opencl_other;
    std::wstring driver_version;
};

class DriverRegistry {
public:
    [[nodiscard]] static DriverFiles read(const DisplayDevice& device);

    [[nodiscard]] static std::wstring resolve(const std::wstring& name, bool own_bitness);

    [[nodiscard]] static bool exists(const std::wstring& path);
};
}  // namespace lsw::suites
