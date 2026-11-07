#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "core/text.hpp"
#include "support/artifact_writer.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_image.hpp"
#include "support/vulkan_rig.hpp"
#include "support/vulkan_scene.hpp"
#include "support/vulkan_shaders.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace lsw::suites::sanity::vk {
class Kit {
public:
    using DeviceBody = std::function<std::string(VulkanRig&, Problems&)>;

    [[nodiscard]] static Outcome each_device(Context& context, const VulkanRigOptions& options, const DeviceBody& body);

    [[nodiscard]] static Outcome each_device(Context& context, const DeviceBody& body) {
        return each_device(context, VulkanRigOptions{}, body);
    }

    static bool expect(Problems& problems, const std::string& what, VkResult got, std::initializer_list<VkResult> accepted);

    static bool succeeds(Problems& problems, const std::string& what, VkResult got) {
        return expect(problems, what, got, {VK_SUCCESS});
    }

    [[nodiscard]] static std::string name(VkResult result) { return VulkanTools::result_name(result); }

    [[nodiscard]] static std::string hex(std::uint64_t value, int digits = 8) { return Text::hex(value, digits); }

    [[nodiscard]] static VkResult host_buffer(VulkanRig& rig, VulkanBuffer& out, VkDeviceSize size,
                                              VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    [[nodiscard]] static VkResult device_buffer(VulkanRig& rig, VulkanBuffer& out, VkDeviceSize size,
                                                VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    [[nodiscard]] static VkResult copy(VulkanRig& rig, VkBuffer source, VkBuffer destination, VkDeviceSize size,
                                       VkDeviceSize source_offset = 0, VkDeviceSize destination_offset = 0);

    [[nodiscard]] static std::vector<std::uint32_t> words(std::uint64_t seed, std::size_t count);

    static bool same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected,
                           const void* actual, std::size_t size);

    static bool same_words(Context& context, Problems& problems, const std::string& what,
                           const std::vector<std::uint32_t>& expected, const std::vector<std::uint32_t>& actual);

    [[nodiscard]] static std::string sanitize(std::string text);

    [[nodiscard]] static VkFormatProperties format_properties(VulkanRig& rig, VkFormat format);

    [[nodiscard]] static bool image_supported(VulkanRig& rig, VkFormat format, VkImageType type, VkImageTiling tiling,
                                              VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties& out,
                                              VkResult* code = nullptr);

    [[nodiscard]] static std::uint64_t device_local_budget(VulkanRig& rig);

    [[nodiscard]] static std::uint32_t buffer_type_bits(VulkanRig& rig, VkDeviceSize size, VkBufferUsageFlags usage);

    [[nodiscard]] static std::uint32_t image_type_bits(VulkanRig& rig, VkFormat format, std::uint32_t width, std::uint32_t height,
                                                      VkImageUsageFlags usage);

    [[nodiscard]] static Outcome finish(const std::vector<std::string>& skips, const Problems& problems);
};
}  // namespace lsw::suites::sanity::vk
