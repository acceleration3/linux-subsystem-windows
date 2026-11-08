#pragma once

#include "support/vulkan_rig.hpp"

namespace lsw::suites {
class VulkanHostGate {
public:
    [[nodiscard]] VkResult open(VulkanRig& rig);

    [[nodiscard]] VkResult submit(VulkanRig& rig, VulkanQueue& queue, VkCommandBuffer commands, VkFence fence, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT);

    [[nodiscard]] VkResult release(VulkanRig& rig);

private:
    VulkanOwned<VkSemaphore> semaphore_;
};
}  // namespace lsw::suites
