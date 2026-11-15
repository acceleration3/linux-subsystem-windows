#pragma once

#include "support/cl_rig.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d12_rig.hpp"
#include "support/gl_api.hpp"
#include "support/vulkan_rig.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::sanity::shader {
class Workload {
public:
    using Words = std::vector<std::uint32_t>;
    using Bytes = std::vector<std::uint8_t>;

    static constexpr unsigned kCount = 1024;

    [[nodiscard]] static const char* hlsl();

    [[nodiscard]] static const char* glsl();

    [[nodiscard]] static const char* opencl();

    [[nodiscard]] static bool d3d11(D3d11Device& device, Words& out, Bytes* code, std::string& error);

    [[nodiscard]] static bool d3d12(D3d12Rig& rig, Words& out, std::string& error);

    [[nodiscard]] static bool gl_supported(const GlApi& api);

    [[nodiscard]] static bool gl(const GlApi& api, Words& out, std::string& error);

    [[nodiscard]] static VkResult vulkan(VulkanRig& rig, Words& out, VkPipelineCache cache = VK_NULL_HANDLE);

    [[nodiscard]] static bool opencl_run(ClRig& rig, Words& out, std::string& error);

    [[nodiscard]] static std::uint64_t hash(const Words& words);
};
}  // namespace lsw::suites::sanity::shader
