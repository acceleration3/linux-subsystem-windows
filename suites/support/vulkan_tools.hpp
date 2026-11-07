#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites {
class VulkanTools {
public:
    [[nodiscard]] static std::string result_name(VkResult result);

    [[nodiscard]] static bool code_in(VkResult got, std::initializer_list<VkResult> accepted);

    [[nodiscard]] static std::string accepted_text(std::initializer_list<VkResult> accepted);

    [[nodiscard]] static std::string bytes_text(std::uint64_t bytes);

    [[nodiscard]] static std::string memory_flags_text(VkMemoryPropertyFlags flags);

    [[nodiscard]] static std::string version_text(std::uint32_t version);

    [[nodiscard]] static std::string format_name(VkFormat format);

    [[nodiscard]] static std::uint32_t texel_size(VkFormat format);

    [[nodiscard]] static bool is_depth_stencil(VkFormat format);

    [[nodiscard]] static bool has_extension(const char* const* names, std::uint32_t count, const char* wanted);

    [[nodiscard]] static std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment);
};
}  // namespace lsw::suites
