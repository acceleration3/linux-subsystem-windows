#include "support/vulkan_host_gate.hpp"

namespace lsw::suites {

VkResult VulkanHostGate::open(VulkanRig& rig) {
    if (!rig.timeline || rig.fn.vkSignalSemaphore == nullptr) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return rig.make_semaphore(semaphore_, true, 0);
}

VkResult VulkanHostGate::submit(VulkanRig& rig, VulkanQueue& queue, VkCommandBuffer commands, VkFence fence, VkPipelineStageFlags stage) {
    const std::uint64_t value = 1;
    VkTimelineSemaphoreSubmitInfo timeline{};
    timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline.waitSemaphoreValueCount = 1;
    timeline.pWaitSemaphoreValues = &value;
    const VkSemaphore handle = semaphore_.get();
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.pNext = &timeline;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &handle;
    info.pWaitDstStageMask = &stage;
    info.commandBufferCount = commands != nullptr ? 1 : 0;
    info.pCommandBuffers = commands != nullptr ? &commands : nullptr;
    return rig.submit(queue, 1, &info, fence);
}

VkResult VulkanHostGate::release(VulkanRig& rig) {
    VkSemaphoreSignalInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    info.semaphore = semaphore_.get();
    info.value = 1;
    return rig.fn.vkSignalSemaphore(rig.device, &info);
}

}  // namespace lsw::suites
