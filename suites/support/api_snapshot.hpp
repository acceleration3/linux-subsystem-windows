#pragma once

#include "support/av_adapters.hpp"
#include "support/opencl_api.hpp"
#include "support/vulkan_api.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
struct ApiSnapshot {
    std::string dxgi;
    std::string kmt;
    std::string vulkan;
    std::string opencl;

    [[nodiscard]] std::string all() const { return dxgi + kmt + vulkan + opencl; }

    [[nodiscard]] static bool take(ApiSnapshot& out, std::string& error);

    [[nodiscard]] static std::string describe_dxgi(const std::vector<AvAdapter>& adapters,
                                                   bool identity_only);

    [[nodiscard]] static std::string describe_vulkan(const std::vector<VulkanDevice>& devices);

    [[nodiscard]] static std::string describe_opencl(const std::vector<OpenClPlatform>& platforms);
};
}  // namespace lsw::suites
