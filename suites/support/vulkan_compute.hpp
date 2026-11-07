#pragma once

#include "support/vulkan_rig.hpp"

namespace lsw::suites {
struct VulkanBinding {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    std::uint32_t count = 1;
    const VkSampler* immutable = nullptr;
};

class VulkanCompute {
public:
    [[nodiscard]] VkResult create(VulkanRig& rig, const std::vector<std::uint32_t>& spirv,
                                  const std::vector<VulkanBinding>& bindings, std::uint32_t push_bytes,
                                  const VkSpecializationInfo* specialization = nullptr, bool push_descriptors = false,
                                  VkPipelineCache cache = VK_NULL_HANDLE);

    void write_buffer(std::uint32_t binding, VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE,
                      std::uint32_t element = 0);

    void write_texel(std::uint32_t binding, VkBufferView view);

    void write_image(std::uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout image_layout);

    void bind(VkCommandBuffer commands) const;

    void push(VkCommandBuffer commands, const void* data, std::uint32_t size) const;

    void dispatch(VkCommandBuffer commands, std::uint32_t x, std::uint32_t y = 1, std::uint32_t z = 1) const;

    [[nodiscard]] VkResult run(const void* push_data, std::uint32_t push_size, std::uint32_t x, std::uint32_t y = 1,
                               std::uint32_t z = 1, VulkanQueue* queue = nullptr);

    VulkanOwned<VkShaderModule> module;
    VulkanOwned<VkDescriptorSetLayout> set_layout;
    VulkanOwned<VkPipelineLayout> layout;
    VulkanOwned<VkPipeline> pipeline;
    VulkanOwned<VkDescriptorPool> pool;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::vector<std::uint32_t> dynamic_offsets;

private:
    VulkanRig* rig_ = nullptr;
    std::vector<VulkanBinding> bindings_;
};
}  // namespace lsw::suites
