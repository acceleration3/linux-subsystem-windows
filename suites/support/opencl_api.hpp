#pragma once

#include "support/dyn_library.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct OpenClDevice {
    std::string name;
    std::string vendor;
    std::string version;
    std::string extensions;
    std::string driver_version;
    std::uint32_t vendor_id = 0;
    std::uint64_t global_memory = 0;
    std::uint64_t max_allocation = 0;
    bool luid_extension = false;
    bool luid_valid = false;
    LUID luid{};
    int luid_query_error = 0;
    int luid_valid_query_error = 0;
    std::string query_failures;
};

struct OpenClPlatform {
    std::string name;
    std::string vendor;
    std::string version;
    std::vector<OpenClDevice> devices;
};

class OpenClApi {
public:
    static constexpr int kPlatformNotFound = -1001;

    [[nodiscard]] bool load(std::string& error);

    [[nodiscard]] bool platforms(std::vector<OpenClPlatform>& out, std::string& error) const;

private:
    DynLibrary library_;
    int(__stdcall* get_platform_ids_)(unsigned, void**, unsigned*) = nullptr;
    int(__stdcall* get_platform_info_)(void*, unsigned, std::size_t, void*, std::size_t*) = nullptr;
    int(__stdcall* get_device_ids_)(void*, unsigned long long, unsigned, void**, unsigned*) = nullptr;
    int(__stdcall* get_device_info_)(void*, unsigned, std::size_t, void*, std::size_t*) = nullptr;
};
}  // namespace lsw::suites
