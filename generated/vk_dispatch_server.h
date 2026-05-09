#pragma once

#include "lsw/protocol/buffer.hpp"

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace lsw::vkproto {

// User implements this; the dispatch table calls into it for every command.
class ServerDispatch {
public:
    virtual ~ServerDispatch() = default;

    virtual void on_vkDestroyInstance(VkInstance instance,
                                      const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                                     uint32_t queueIndex, VkQueue* pQueue) = 0;
    virtual VkResult on_vkEnumeratePhysicalDevices(VkInstance instance,
                                                   uint32_t* pPhysicalDeviceCount,
                                                   VkPhysicalDevice* pPhysicalDevices) = 0;
    virtual void on_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                                   VkPhysicalDeviceProperties2* pProperties) = 0;
    virtual VkResult on_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkInstance* pInstance) = 0;
    virtual VkResult on_vkCreateDevice(VkPhysicalDevice physicalDevice,
                                       const VkDeviceCreateInfo* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkDevice* pDevice) = 0;
    virtual void on_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                  VkPhysicalDeviceProperties* pProperties) = 0;
    virtual void on_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                VkPhysicalDeviceFeatures* pFeatures) = 0;
    virtual void on_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                                        VkFormat format,
                                                        VkFormatProperties* pFormatProperties) = 0;
    virtual VkResult on_vkGetPhysicalDeviceImageFormatProperties(
        VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling,
        VkImageUsageFlags usage, VkImageCreateFlags flags,
        VkImageFormatProperties* pImageFormatProperties) = 0;
    virtual void
    on_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                           VkPhysicalDeviceMemoryProperties* pMemoryProperties) = 0;
    virtual void on_vkGetPhysicalDeviceQueueFamilyProperties(
        VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
        VkQueueFamilyProperties* pQueueFamilyProperties) = 0;
    virtual void on_vkGetPhysicalDeviceSparseImageFormatProperties(
        VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
        VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
        uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) = 0;
    virtual VkResult
    on_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                            uint32_t* pPropertyCount,
                                            VkExtensionProperties* pProperties) = 0;
    virtual VkResult on_vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                         uint32_t* pPropertyCount,
                                                         VkLayerProperties* pProperties) = 0;
    virtual void on_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                 VkPhysicalDeviceFeatures2* pFeatures) = 0;
    virtual void
    on_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                            VkFormatProperties2* pFormatProperties) = 0;
    virtual VkResult on_vkGetPhysicalDeviceImageFormatProperties2(
        VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
        VkImageFormatProperties2* pImageFormatProperties) = 0;
    virtual void on_vkGetPhysicalDeviceMemoryProperties2(
        VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) = 0;
    virtual void on_vkGetPhysicalDeviceQueueFamilyProperties2(
        VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
        VkQueueFamilyProperties2* pQueueFamilyProperties) = 0;
    virtual void on_vkGetPhysicalDeviceSparseImageFormatProperties2(
        VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
        uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) = 0;
    virtual VkResult on_vkDeviceWaitIdle(VkDevice device) = 0;
    virtual VkResult on_vkQueueWaitIdle(VkQueue queue) = 0;
    virtual VkResult on_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkDeviceMemory* pMemory) = 0;
    virtual void on_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                 const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                           VkDeviceSize memoryOffset) = 0;
    virtual VkResult on_vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                                          VkDeviceSize memoryOffset) = 0;
    virtual void on_vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                                                  VkMemoryRequirements* pMemoryRequirements) = 0;
    virtual void on_vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                                                 VkMemoryRequirements* pMemoryRequirements) = 0;
    virtual VkResult on_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkBuffer* pBuffer) = 0;
    virtual void on_vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                    const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkImage* pImage) = 0;
    virtual void on_vkDestroyImage(VkDevice device, VkImage image,
                                   const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkImageView* pView) = 0;
    virtual void on_vkDestroyImageView(VkDevice device, VkImageView imageView,
                                       const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkSampler* pSampler) = 0;
    virtual void on_vkDestroySampler(VkDevice device, VkSampler sampler,
                                     const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkSemaphore* pSemaphore) = 0;
    virtual void on_vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                       const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkFence* pFence) = 0;
    virtual void on_vkDestroyFence(VkDevice device, VkFence fence,
                                   const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkResetFences(VkDevice device, uint32_t fenceCount,
                                      const VkFence* pFences) = 0;
    virtual VkResult on_vkWaitForFences(VkDevice device, uint32_t fenceCount,
                                        const VkFence* pFences, VkBool32 waitAll,
                                        uint64_t timeout) = 0;
    virtual VkResult on_vkGetFenceStatus(VkDevice device, VkFence fence) = 0;
    virtual VkResult on_vkCreateCommandPool(VkDevice device,
                                            const VkCommandPoolCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkCommandPool* pCommandPool) = 0;
    virtual void on_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                         const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                           VkCommandPoolResetFlags flags) = 0;
    virtual VkResult on_vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                             const VkCommandBufferBeginInfo* pBeginInfo) = 0;
    virtual VkResult on_vkEndCommandBuffer(VkCommandBuffer commandBuffer) = 0;
    virtual VkResult on_vkResetCommandBuffer(VkCommandBuffer commandBuffer,
                                             VkCommandBufferResetFlags flags) = 0;
    virtual void on_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                                          const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateShaderModule(VkDevice device,
                                             const VkShaderModuleCreateInfo* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkShaderModule* pShaderModule) = 0;
    virtual VkResult on_vkCreatePipelineLayout(VkDevice device,
                                               const VkPipelineLayoutCreateInfo* pCreateInfo,
                                               const VkAllocationCallbacks* pAllocator,
                                               VkPipelineLayout* pPipelineLayout) = 0;
    virtual void on_vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                                            const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                                      const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreatePipelineCache(VkDevice device,
                                              const VkPipelineCacheCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkPipelineCache* pPipelineCache) = 0;
    virtual void on_vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                                           const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateRenderPass(VkDevice device,
                                           const VkRenderPassCreateInfo* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkRenderPass* pRenderPass) = 0;
    virtual void on_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                        const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateFramebuffer(VkDevice device,
                                            const VkFramebufferCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkFramebuffer* pFramebuffer) = 0;
    virtual void on_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                                         const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateDescriptorSetLayout(
        VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) = 0;
    virtual void on_vkDestroyDescriptorSetLayout(VkDevice device,
                                                 VkDescriptorSetLayout descriptorSetLayout,
                                                 const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkCreateDescriptorPool(VkDevice device,
                                               const VkDescriptorPoolCreateInfo* pCreateInfo,
                                               const VkAllocationCallbacks* pAllocator,
                                               VkDescriptorPool* pDescriptorPool) = 0;
    virtual void on_vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                            const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                         uint32_t commandBufferCount,
                                         const VkCommandBuffer* pCommandBuffers) = 0;
    virtual VkResult on_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                      const VkSubmitInfo* pSubmits, VkFence fence) = 0;
    virtual void on_vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                                      VkPipelineBindPoint pipelineBindPoint,
                                      VkPipeline pipeline) = 0;
    virtual void on_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                                     uint32_t viewportCount, const VkViewport* pViewports) = 0;
    virtual void on_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor,
                                    uint32_t scissorCount, const VkRect2D* pScissors) = 0;
    virtual void on_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                           uint32_t bindingCount, const VkBuffer* pBuffers,
                                           const VkDeviceSize* pOffsets) = 0;
    virtual void on_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                         VkDeviceSize offset, VkIndexType indexType) = 0;
    virtual void
    on_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                               VkPipelineLayout layout, uint32_t firstSet,
                               uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets,
                               uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) = 0;
    virtual void on_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                              uint32_t instanceCount, uint32_t firstVertex,
                              uint32_t firstInstance) = 0;
    virtual void on_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                     uint32_t instanceCount, uint32_t firstIndex,
                                     int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void on_vkCmdPipelineBarrier(
        VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
        uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) = 0;
    virtual void on_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                    VkBuffer dstBuffer, uint32_t regionCount,
                                    const VkBufferCopy* pRegions) = 0;
    virtual void on_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                         const VkRenderPassBeginInfo* pRenderPassBegin,
                                         VkSubpassContents contents) = 0;
    virtual void on_vkCmdEndRenderPass(VkCommandBuffer commandBuffer) = 0;
    virtual void on_vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) = 0;
    virtual void on_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                                       VkShaderStageFlags stageFlags, uint32_t offset,
                                       uint32_t size, const void* pValues) = 0;
    virtual VkResult on_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                                             uint32_t queueFamilyIndex,
                                                             VkSurfaceKHR surface,
                                                             VkBool32* pSupported) = 0;
    virtual VkResult on_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
        VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) = 0;
    virtual VkResult
    on_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                            uint32_t* pSurfaceFormatCount,
                                            VkSurfaceFormatKHR* pSurfaceFormats) = 0;
    virtual VkResult
    on_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                                 VkSurfaceKHR surface, uint32_t* pPresentModeCount,
                                                 VkPresentModeKHR* pPresentModes) = 0;
    virtual VkResult on_vkCreateSwapchainKHR(VkDevice device,
                                             const VkSwapchainCreateInfoKHR* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkSwapchainKHR* pSwapchain) = 0;
    virtual void on_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                          const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                uint32_t* pSwapchainImageCount,
                                                VkImage* pSwapchainImages) = 0;
    virtual VkResult on_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                              uint64_t timeout, VkSemaphore semaphore,
                                              VkFence fence, uint32_t* pImageIndex) = 0;
    virtual VkResult on_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) = 0;
    virtual VkResult on_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                                  uint32_t createInfoCount,
                                                  const VkGraphicsPipelineCreateInfo* pCreateInfos,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkPipeline* pPipelines) = 0;
    virtual void on_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                           const VkWriteDescriptorSet* pDescriptorWrites,
                                           uint32_t descriptorCopyCount,
                                           const VkCopyDescriptorSet* pDescriptorCopies) = 0;
    virtual VkResult on_vkAllocateCommandBuffers(VkDevice device,
                                                 const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                 VkCommandBuffer* pCommandBuffers) = 0;
    virtual VkResult on_vkAllocateDescriptorSets(VkDevice device,
                                                 const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                                 VkDescriptorSet* pDescriptorSets) = 0;
    virtual VkResult on_vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                                             uint32_t descriptorSetCount,
                                             const VkDescriptorSet* pDescriptorSets) = 0;
    virtual VkResult on_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                                                 uint32_t createInfoCount,
                                                 const VkComputePipelineCreateInfo* pCreateInfos,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkPipeline* pPipelines) = 0;
    virtual VkResult on_vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                              VkDescriptorPoolResetFlags flags) = 0;
    virtual VkResult on_vkCreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) = 0;
    virtual void on_vkDestroyEvent(VkDevice device, VkEvent event,
                                   const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkGetEventStatus(VkDevice device, VkEvent event) = 0;
    virtual VkResult on_vkSetEvent(VkDevice device, VkEvent event) = 0;
    virtual VkResult on_vkResetEvent(VkDevice device, VkEvent event) = 0;
    virtual void on_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                  VkPipelineStageFlags stageMask) = 0;
    virtual void on_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                    VkPipelineStageFlags stageMask) = 0;
    virtual void on_vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount,
                                    const VkEvent* pEvents, VkPipelineStageFlags srcStageMask,
                                    VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount,
                                    const VkMemoryBarrier* pMemoryBarriers,
                                    uint32_t bufferMemoryBarrierCount,
                                    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                    uint32_t imageMemoryBarrierCount,
                                    const VkImageMemoryBarrier* pImageMemoryBarriers) = 0;
    virtual void on_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                   VkImageLayout srcImageLayout, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount,
                                   const VkImageCopy* pRegions) = 0;
    virtual void on_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                           VkImage dstImage, VkImageLayout dstImageLayout,
                                           uint32_t regionCount,
                                           const VkBufferImageCopy* pRegions) = 0;
    virtual void on_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                           VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                                           uint32_t regionCount,
                                           const VkBufferImageCopy* pRegions) = 0;
    virtual void on_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                   VkImageLayout srcImageLayout, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount,
                                   const VkImageBlit* pRegions, VkFilter filter) = 0;
    virtual void on_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                         VkImageLayout imageLayout, const VkClearColorValue* pColor,
                                         uint32_t rangeCount,
                                         const VkImageSubresourceRange* pRanges) = 0;
    virtual void on_vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                                                VkImageLayout imageLayout,
                                                const VkClearDepthStencilValue* pDepthStencil,
                                                uint32_t rangeCount,
                                                const VkImageSubresourceRange* pRanges) = 0;
    virtual void on_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                          const VkClearAttachment* pAttachments, uint32_t rectCount,
                                          const VkClearRect* pRects) = 0;
    virtual void on_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                    VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) = 0;
    virtual void on_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                      VkDeviceSize dstOffset, VkDeviceSize dataSize,
                                      const void* pData) = 0;
    virtual void on_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) = 0;
    virtual void on_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                                      float depthBiasClamp, float depthBiasSlopeFactor) = 0;
    virtual void on_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer,
                                           const float* blendConstants) = 0;
    virtual void on_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds,
                                        float maxDepthBounds) = 0;
    virtual void on_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                               VkStencilFaceFlags faceMask,
                                               uint32_t compareMask) = 0;
    virtual void on_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                                             VkStencilFaceFlags faceMask, uint32_t writeMask) = 0;
    virtual void on_vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                                             VkStencilFaceFlags faceMask, uint32_t reference) = 0;
    virtual void on_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX,
                                  uint32_t groupCountY, uint32_t groupCountZ) = 0;
    virtual void on_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                          VkDeviceSize offset) = 0;
    virtual void on_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                      VkDeviceSize offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void on_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                             VkDeviceSize offset, uint32_t drawCount,
                                             uint32_t stride) = 0;
    virtual void on_vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                         const VkCommandBuffer* pCommandBuffers) = 0;
    virtual VkResult on_vkAcquireNextImage2KHR(VkDevice device,
                                               const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                               uint32_t* pImageIndex) = 0;
    virtual VkResult on_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                                            const VkBindBufferMemoryInfo* pBindInfos) = 0;
    virtual VkResult on_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                                           const VkBindImageMemoryInfo* pBindInfos) = 0;
    virtual void on_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                    uint32_t query, VkQueryControlFlags flags) = 0;
    virtual void on_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                          const VkRenderPassBeginInfo* pRenderPassBegin,
                                          const VkSubpassBeginInfo* pSubpassBeginInfo) = 0;
    virtual void on_vkCmdBeginRendering(VkCommandBuffer commandBuffer,
                                        const VkRenderingInfo* pRenderingInfo) = 0;
    virtual void on_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                                            uint32_t bindingCount, const VkBuffer* pBuffers,
                                            const VkDeviceSize* pOffsets,
                                            const VkDeviceSize* pSizes,
                                            const VkDeviceSize* pStrides) = 0;
    virtual void on_vkCmdBlitImage2(VkCommandBuffer commandBuffer,
                                    const VkBlitImageInfo2* pBlitImageInfo) = 0;
    virtual void on_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer,
                                     const VkCopyBufferInfo2* pCopyBufferInfo) = 0;
    virtual void
    on_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                               const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) = 0;
    virtual void on_vkCmdCopyImage2(VkCommandBuffer commandBuffer,
                                    const VkCopyImageInfo2* pCopyImageInfo) = 0;
    virtual void
    on_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                               const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) = 0;
    virtual void on_vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                              uint32_t firstQuery, uint32_t queryCount,
                                              VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                              VkDeviceSize stride, VkQueryResultFlags flags) = 0;
    virtual void on_vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX,
                                      uint32_t baseGroupY, uint32_t baseGroupZ,
                                      uint32_t groupCountX, uint32_t groupCountY,
                                      uint32_t groupCountZ) = 0;
    virtual void on_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                  VkDeviceSize offset, VkBuffer countBuffer,
                                                  VkDeviceSize countBufferOffset,
                                                  uint32_t maxDrawCount, uint32_t stride) = 0;
    virtual void on_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                           VkDeviceSize offset, VkBuffer countBuffer,
                                           VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                           uint32_t stride) = 0;
    virtual void on_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                  uint32_t query) = 0;
    virtual void on_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer,
                                        const VkSubpassEndInfo* pSubpassEndInfo) = 0;
    virtual void on_vkCmdEndRendering(VkCommandBuffer commandBuffer) = 0;
    virtual void on_vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                                      const VkSubpassBeginInfo* pSubpassBeginInfo,
                                      const VkSubpassEndInfo* pSubpassEndInfo) = 0;
    virtual void on_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                          const VkDependencyInfo* pDependencyInfo) = 0;
    virtual void on_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                     VkPipelineStageFlags2 stageMask) = 0;
    virtual void on_vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                        uint32_t firstQuery, uint32_t queryCount) = 0;
    virtual void on_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                      VkImageLayout srcImageLayout, VkImage dstImage,
                                      VkImageLayout dstImageLayout, uint32_t regionCount,
                                      const VkImageResolve* pRegions) = 0;
    virtual void on_vkCmdResolveImage2(VkCommandBuffer commandBuffer,
                                       const VkResolveImageInfo2* pResolveImageInfo) = 0;
    virtual void on_vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode) = 0;
    virtual void on_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer,
                                            VkBool32 depthBiasEnable) = 0;
    virtual void on_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer,
                                                  VkBool32 depthBoundsTestEnable) = 0;
    virtual void on_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer,
                                           VkCompareOp depthCompareOp) = 0;
    virtual void on_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer,
                                            VkBool32 depthTestEnable) = 0;
    virtual void on_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer,
                                             VkBool32 depthWriteEnable) = 0;
    virtual void on_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) = 0;
    virtual void on_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                   const VkDependencyInfo* pDependencyInfo) = 0;
    virtual void on_vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace) = 0;
    virtual void on_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer,
                                                   VkBool32 primitiveRestartEnable) = 0;
    virtual void on_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer,
                                              VkPrimitiveTopology primitiveTopology) = 0;
    virtual void on_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer,
                                                    VkBool32 rasterizerDiscardEnable) = 0;
    virtual void on_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount,
                                             const VkRect2D* pScissors) = 0;
    virtual void on_vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                      VkStencilOp failOp, VkStencilOp passOp,
                                      VkStencilOp depthFailOp, VkCompareOp compareOp) = 0;
    virtual void on_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer,
                                              VkBool32 stencilTestEnable) = 0;
    virtual void on_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount,
                                              const VkViewport* pViewports) = 0;
    virtual void on_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                                     const VkEvent* pEvents,
                                     const VkDependencyInfo* pDependencyInfos) = 0;
    virtual void on_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer,
                                        VkPipelineStageFlagBits pipelineStage,
                                        VkQueryPool queryPool, uint32_t query) = 0;
    virtual void on_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                                         VkQueryPool queryPool, uint32_t query) = 0;
    virtual VkResult on_vkCreateBufferView(VkDevice device,
                                           const VkBufferViewCreateInfo* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkBufferView* pView) = 0;
    virtual VkResult
    on_vkCreateDescriptorUpdateTemplate(VkDevice device,
                                        const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) = 0;
    virtual VkResult on_vkCreatePrivateDataSlot(VkDevice device,
                                                const VkPrivateDataSlotCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkPrivateDataSlot* pPrivateDataSlot) = 0;
    virtual VkResult on_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkQueryPool* pQueryPool) = 0;
    virtual VkResult on_vkCreateRenderPass2(VkDevice device,
                                            const VkRenderPassCreateInfo2* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkRenderPass* pRenderPass) = 0;
    virtual VkResult on_vkCreateSamplerYcbcrConversion(
        VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) = 0;
    virtual void on_vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                                        const VkAllocationCallbacks* pAllocator) = 0;
    virtual void
    on_vkDestroyDescriptorUpdateTemplate(VkDevice device,
                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                         const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkDestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot,
                                             const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                                       const VkAllocationCallbacks* pAllocator) = 0;
    virtual void on_vkDestroySamplerYcbcrConversion(VkDevice device,
                                                    VkSamplerYcbcrConversion ycbcrConversion,
                                                    const VkAllocationCallbacks* pAllocator) = 0;
    virtual VkResult on_vkEnumeratePhysicalDeviceGroups(
        VkInstance instance, uint32_t* pPhysicalDeviceGroupCount,
        VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) = 0;
    virtual VkResult on_vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                                  const VkMappedMemoryRange* pMemoryRanges) = 0;
    virtual VkDeviceAddress on_vkGetBufferDeviceAddress(VkDevice device,
                                                        const VkBufferDeviceAddressInfo* pInfo) = 0;
    virtual void on_vkGetBufferMemoryRequirements2(VkDevice device,
                                                   const VkBufferMemoryRequirementsInfo2* pInfo,
                                                   VkMemoryRequirements2* pMemoryRequirements) = 0;
    virtual uint64_t on_vkGetBufferOpaqueCaptureAddress(VkDevice device,
                                                        const VkBufferDeviceAddressInfo* pInfo) = 0;
    virtual void
    on_vkGetDescriptorSetLayoutSupport(VkDevice device,
                                       const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                       VkDescriptorSetLayoutSupport* pSupport) = 0;
    virtual void
    on_vkGetDeviceBufferMemoryRequirements(VkDevice device,
                                           const VkDeviceBufferMemoryRequirements* pInfo,
                                           VkMemoryRequirements2* pMemoryRequirements) = 0;
    virtual void
    on_vkGetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex,
                                          uint32_t localDeviceIndex, uint32_t remoteDeviceIndex,
                                          VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) = 0;
    virtual VkResult on_vkGetDeviceGroupPresentCapabilitiesKHR(
        VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) = 0;
    virtual VkResult
    on_vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
                                              VkDeviceGroupPresentModeFlagsKHR* pModes) = 0;
    virtual void
    on_vkGetDeviceImageMemoryRequirements(VkDevice device,
                                          const VkDeviceImageMemoryRequirements* pInfo,
                                          VkMemoryRequirements2* pMemoryRequirements) = 0;
    virtual void on_vkGetDeviceImageSparseMemoryRequirements(
        VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
        uint32_t* pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) = 0;
    virtual void on_vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                                                VkDeviceSize* pCommittedMemoryInBytes) = 0;
    virtual uint64_t on_vkGetDeviceMemoryOpaqueCaptureAddress(
        VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) = 0;
    virtual void on_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo,
                                      VkQueue* pQueue) = 0;
    virtual void on_vkGetImageMemoryRequirements2(VkDevice device,
                                                  const VkImageMemoryRequirementsInfo2* pInfo,
                                                  VkMemoryRequirements2* pMemoryRequirements) = 0;
    virtual void on_vkGetImageSparseMemoryRequirements(
        VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements* pSparseMemoryRequirements) = 0;
    virtual void on_vkGetImageSparseMemoryRequirements2(
        VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo,
        uint32_t* pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) = 0;
    virtual void on_vkGetImageSubresourceLayout(VkDevice device, VkImage image,
                                                const VkImageSubresource* pSubresource,
                                                VkSubresourceLayout* pLayout) = 0;
    virtual void on_vkGetPhysicalDeviceExternalBufferProperties(
        VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
        VkExternalBufferProperties* pExternalBufferProperties) = 0;
    virtual void on_vkGetPhysicalDeviceExternalFenceProperties(
        VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
        VkExternalFenceProperties* pExternalFenceProperties) = 0;
    virtual void on_vkGetPhysicalDeviceExternalSemaphoreProperties(
        VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
        VkExternalSemaphoreProperties* pExternalSemaphoreProperties) = 0;
    virtual VkResult on_vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
                                                                VkSurfaceKHR surface,
                                                                uint32_t* pRectCount,
                                                                VkRect2D* pRects) = 0;
    virtual VkResult
    on_vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t* pToolCount,
                                         VkPhysicalDeviceToolProperties* pToolProperties) = 0;
    virtual void on_vkGetPrivateData(VkDevice device, VkObjectType objectType,
                                     uint64_t objectHandle, VkPrivateDataSlot privateDataSlot,
                                     uint64_t* pData) = 0;
    virtual void on_vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                                               VkExtent2D* pGranularity) = 0;
    virtual VkResult on_vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore,
                                                   uint64_t* pValue) = 0;
    virtual VkResult
    on_vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                      const VkMappedMemoryRange* pMemoryRanges) = 0;
    virtual VkResult on_vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                                              uint32_t srcCacheCount,
                                              const VkPipelineCache* pSrcCaches) = 0;
    virtual VkResult on_vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                                          const VkBindSparseInfo* pBindInfo, VkFence fence) = 0;
    virtual VkResult on_vkQueueSubmit2(VkQueue queue, uint32_t submitCount,
                                       const VkSubmitInfo2* pSubmits, VkFence fence) = 0;
    virtual void on_vkResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                                     uint32_t queryCount) = 0;
    virtual VkResult on_vkSetPrivateData(VkDevice device, VkObjectType objectType,
                                         uint64_t objectHandle, VkPrivateDataSlot privateDataSlot,
                                         uint64_t data) = 0;
    virtual VkResult on_vkSignalSemaphore(VkDevice device,
                                          const VkSemaphoreSignalInfo* pSignalInfo) = 0;
    virtual void on_vkTrimCommandPool(VkDevice device, VkCommandPool commandPool,
                                      VkCommandPoolTrimFlags flags) = 0;
    virtual void on_vkUnmapMemory(VkDevice device, VkDeviceMemory memory) = 0;
    virtual VkResult on_vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo,
                                         uint64_t timeout) = 0;
};

// Decode and dispatch a single command. Returns false on protocol error.
bool dispatch_command(ServerDispatch& s, std::uint32_t cmd_id, ::lsw::protocol::BufferReader& r,
                      ::lsw::protocol::Buffer& reply);

}  // namespace lsw::vkproto
