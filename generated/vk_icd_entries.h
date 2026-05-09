#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan_core.h>

namespace lsw::icd::generated {

// Wrapper functions matching the application-facing Vulkan signatures.
// Each one looks up the per-VkInstance Connection (or opens a fresh
// one in vkCreateInstance), unwraps dispatchable handles, calls the
// generated lsw_vk* dispatch over that Connection's transport, and
// wraps any returned dispatchable handles back into LswWrappedHandle
// pointers carrying the same Connection.

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyInstance(VkInstance instance,
                                                  const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDevice(VkDevice device,
                                                const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                                                 uint32_t queueIndex, VkQueue* pQueue);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumeratePhysicalDevices(VkInstance instance,
                                                               uint32_t* pPhysicalDeviceCount,
                                                               VkPhysicalDevice* pPhysicalDevices);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkInstance* pInstance);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDevice(VkPhysicalDevice physicalDevice,
                                                   const VkDeviceCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkDevice* pDevice);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                            VkPhysicalDeviceFeatures* pFeatures);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
    uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceFeatures2* pFeatures);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkDeviceWaitIdle(VkDevice device);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueWaitIdle(VkQueue queue);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAllocateMemory(VkDevice device,
                                                     const VkMemoryAllocateInfo* pAllocateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkDeviceMemory* pMemory);
VKAPI_ATTR void VKAPI_CALL Wrap_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                             const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindBufferMemory(VkDevice device, VkBuffer buffer,
                                                       VkDeviceMemory memory,
                                                       VkDeviceSize memoryOffset);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindImageMemory(VkDevice device, VkImage image,
                                                      VkDeviceMemory memory,
                                                      VkDeviceSize memoryOffset);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetBufferMemoryRequirements(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateBuffer(VkDevice device,
                                                   const VkBufferCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkBuffer* pBuffer);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                                const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateImage(VkDevice device,
                                                  const VkImageCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkImage* pImage);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyImage(VkDevice device, VkImage image,
                                               const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateImageView(VkDevice device,
                                                      const VkImageViewCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkImageView* pView);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyImageView(VkDevice device, VkImageView imageView,
                                                   const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSampler(VkDevice device,
                                                    const VkSamplerCreateInfo* pCreateInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkSampler* pSampler);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySampler(VkDevice device, VkSampler sampler,
                                                 const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSemaphore(VkDevice device,
                                                      const VkSemaphoreCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkSemaphore* pSemaphore);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                                   const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateFence(VkDevice device,
                                                  const VkFenceCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkFence* pFence);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyFence(VkDevice device, VkFence fence,
                                               const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetFences(VkDevice device, uint32_t fenceCount,
                                                  const VkFence* pFences);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkWaitForFences(VkDevice device, uint32_t fenceCount,
                                                    const VkFence* pFences, VkBool32 waitAll,
                                                    uint64_t timeout);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetFenceStatus(VkDevice device, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateCommandPool(VkDevice device,
                                                        const VkCommandPoolCreateInfo* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkCommandPool* pCommandPool);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                                     const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                                       VkCommandPoolResetFlags flags);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEndCommandBuffer(VkCommandBuffer commandBuffer);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetCommandBuffer(VkCommandBuffer commandBuffer,
                                                         VkCommandBufferResetFlags flags);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                                                      const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo,
                          const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipelineLayout(VkDevice device,
                                                        VkPipelineLayout pipelineLayout,
                                                        const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                  const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipelineCache(VkDevice device,
                                                       VkPipelineCache pipelineCache,
                                                       const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateRenderPass(VkDevice device,
                                                       const VkRenderPassCreateInfo* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkRenderPass* pRenderPass);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                    const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateFramebuffer(VkDevice device,
                                                        const VkFramebufferCreateInfo* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkFramebuffer* pFramebuffer);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                                                     const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout);
VKAPI_ATTR void VKAPI_CALL
Wrap_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                  const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDescriptorPool(VkDevice device,
                                                        VkDescriptorPool descriptorPool,
                                                        const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer* pCommandBuffers);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                                  const VkSubmitInfo* pSubmits, VkFence fence);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                                                  VkPipelineBindPoint pipelineBindPoint,
                                                  VkPipeline pipeline);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                                 uint32_t firstViewport, uint32_t viewportCount,
                                                 const VkViewport* pViewports);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                                uint32_t firstScissor, uint32_t scissorCount,
                                                const VkRect2D* pScissors);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                                                       uint32_t firstBinding, uint32_t bindingCount,
                                                       const VkBuffer* pBuffers,
                                                       const VkDeviceSize* pOffsets);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                     VkDeviceSize offset, VkIndexType indexType);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
    uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets,
    uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                                          uint32_t instanceCount, uint32_t firstVertex,
                                          uint32_t firstInstance);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                                 uint32_t instanceCount, uint32_t firstIndex,
                                                 int32_t vertexOffset, uint32_t firstInstance);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                                VkBuffer dstBuffer, uint32_t regionCount,
                                                const VkBufferCopy* pRegions);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                                     const VkRenderPassBeginInfo* pRenderPassBegin,
                                                     VkSubpassContents contents);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRenderPass(VkCommandBuffer commandBuffer);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                                 VkSubpassContents contents);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPushConstants(VkCommandBuffer commandBuffer,
                                                   VkPipelineLayout layout,
                                                   VkShaderStageFlags stageFlags, uint32_t offset,
                                                   uint32_t size, const void* pValues);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
    VkBool32* pSupported);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes);
VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                          const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                      const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetSwapchainImagesKHR(VkDevice device,
                                                            VkSwapchainKHR swapchain,
                                                            uint32_t* pSwapchainImageCount,
                                                            VkImage* pSwapchainImages);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                          uint64_t timeout, VkSemaphore semaphore,
                                                          VkFence fence, uint32_t* pImageIndex);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueuePresentKHR(VkQueue queue,
                                                      const VkPresentInfoKHR* pPresentInfo);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines);
VKAPI_ATTR void VKAPI_CALL Wrap_vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);
VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo,
                              VkCommandBuffer* pCommandBuffers);
VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo,
                              VkDescriptorSet* pDescriptorSets);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkFreeDescriptorSets(VkDevice device,
                                                         VkDescriptorPool descriptorPool,
                                                         uint32_t descriptorSetCount,
                                                         const VkDescriptorSet* pDescriptorSets);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateComputePipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetDescriptorPool(VkDevice device,
                                                          VkDescriptorPool descriptorPool,
                                                          VkDescriptorPoolResetFlags flags);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateEvent(VkDevice device,
                                                  const VkEventCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkEvent* pEvent);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyEvent(VkDevice device, VkEvent event,
                                               const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetEventStatus(VkDevice device, VkEvent event);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSetEvent(VkDevice device, VkEvent event);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetEvent(VkDevice device, VkEvent event);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                              VkPipelineStageFlags stageMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                                VkPipelineStageFlags stageMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWaitEvents(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
    VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageCopy* pRegions);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                       VkBuffer srcBuffer, VkImage dstImage,
                                                       VkImageLayout dstImageLayout,
                                                       uint32_t regionCount,
                                                       const VkBufferImageCopy* pRegions);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                                                       VkImage srcImage,
                                                       VkImageLayout srcImageLayout,
                                                       VkBuffer dstBuffer, uint32_t regionCount,
                                                       const VkBufferImageCopy* pRegions);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageBlit* pRegions, VkFilter filter);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                                     VkImageLayout imageLayout,
                                                     const VkClearColorValue* pColor,
                                                     uint32_t rangeCount,
                                                     const VkImageSubresourceRange* pRanges);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount,
    const VkImageSubresourceRange* pRanges);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearAttachments(VkCommandBuffer commandBuffer,
                                                      uint32_t attachmentCount,
                                                      const VkClearAttachment* pAttachments,
                                                      uint32_t rectCount,
                                                      const VkClearRect* pRects);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                VkDeviceSize dstOffset, VkDeviceSize size,
                                                uint32_t data);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                  VkDeviceSize dstOffset, VkDeviceSize dataSize,
                                                  const void* pData);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBias(VkCommandBuffer commandBuffer,
                                                  float depthBiasConstantFactor,
                                                  float depthBiasClamp, float depthBiasSlopeFactor);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer,
                                                       const float* blendConstants);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer,
                                                    float minDepthBounds, float maxDepthBounds);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                                           VkStencilFaceFlags faceMask,
                                                           uint32_t compareMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                                                         VkStencilFaceFlags faceMask,
                                                         uint32_t writeMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                                                         VkStencilFaceFlags faceMask,
                                                         uint32_t reference);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX,
                                              uint32_t groupCountY, uint32_t groupCountZ);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                      VkBuffer buffer, VkDeviceSize offset);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                  VkDeviceSize offset, uint32_t drawCount,
                                                  uint32_t stride);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                                                         VkBuffer buffer, VkDeviceSize offset,
                                                         uint32_t drawCount, uint32_t stride);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdExecuteCommands(VkCommandBuffer commandBuffer,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer* pCommandBuffers);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAcquireNextImage2KHR(
    VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                                                        const VkBindBufferMemoryInfo* pBindInfos);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                                                       const VkBindImageMemoryInfo* pBindInfos);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginQuery(VkCommandBuffer commandBuffer,
                                                VkQueryPool queryPool, uint32_t query,
                                                VkQueryControlFlags flags);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                                      const VkRenderPassBeginInfo* pRenderPassBegin,
                                                      const VkSubpassBeginInfo* pSubpassBeginInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRendering(VkCommandBuffer commandBuffer,
                                                    const VkRenderingInfo* pRenderingInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindVertexBuffers2(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes,
    const VkDeviceSize* pStrides);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBlitImage2(VkCommandBuffer commandBuffer,
                                                const VkBlitImageInfo2* pBlitImageInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer,
                                                 const VkCopyBufferInfo2* pCopyBufferInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImage2(VkCommandBuffer commandBuffer,
                                                const VkCopyImageInfo2* pCopyImageInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImageToBuffer2(
    VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount,
    VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatchBase(VkCommandBuffer commandBuffer,
                                                  uint32_t baseGroupX, uint32_t baseGroupY,
                                                  uint32_t baseGroupZ, uint32_t groupCountX,
                                                  uint32_t groupCountY, uint32_t groupCountZ);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexedIndirectCount(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer,
    VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                                                       VkBuffer buffer, VkDeviceSize offset,
                                                       VkBuffer countBuffer,
                                                       VkDeviceSize countBufferOffset,
                                                       uint32_t maxDrawCount, uint32_t stride);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                              uint32_t query);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer,
                                                    const VkSubpassEndInfo* pSubpassEndInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRendering(VkCommandBuffer commandBuffer);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                                                  const VkSubpassBeginInfo* pSubpassBeginInfo,
                                                  const VkSubpassEndInfo* pSubpassEndInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                                      const VkDependencyInfo* pDependencyInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                                 VkPipelineStageFlags2 stageMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetQueryPool(VkCommandBuffer commandBuffer,
                                                    VkQueryPool queryPool, uint32_t firstQuery,
                                                    uint32_t queryCount);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                  VkImageLayout srcImageLayout, VkImage dstImage,
                                                  VkImageLayout dstImageLayout,
                                                  uint32_t regionCount,
                                                  const VkImageResolve* pRegions);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResolveImage2(VkCommandBuffer commandBuffer,
                                                   const VkResolveImageInfo2* pResolveImageInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetCullMode(VkCommandBuffer commandBuffer,
                                                 VkCullModeFlags cullMode);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer,
                                                        VkBool32 depthBiasEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer,
                                                              VkBool32 depthBoundsTestEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer,
                                                       VkCompareOp depthCompareOp);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer,
                                                        VkBool32 depthTestEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer,
                                                         VkBool32 depthWriteEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer,
                                                   uint32_t deviceMask);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                               const VkDependencyInfo* pDependencyInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetFrontFace(VkCommandBuffer commandBuffer,
                                                  VkFrontFace frontFace);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer,
                                                               VkBool32 primitiveRestartEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer,
                                                          VkPrimitiveTopology primitiveTopology);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer,
                                                                VkBool32 rasterizerDiscardEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer,
                                                         uint32_t scissorCount,
                                                         const VkRect2D* pScissors);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilOp(VkCommandBuffer commandBuffer,
                                                  VkStencilFaceFlags faceMask, VkStencilOp failOp,
                                                  VkStencilOp passOp, VkStencilOp depthFailOp,
                                                  VkCompareOp compareOp);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer,
                                                          VkBool32 stencilTestEnable);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer,
                                                          uint32_t viewportCount,
                                                          const VkViewport* pViewports);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                                                 const VkEvent* pEvents,
                                                 const VkDependencyInfo* pDependencyInfos);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer,
                                                    VkPipelineStageFlagBits pipelineStage,
                                                    VkQueryPool queryPool, uint32_t query);
VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                                                     VkPipelineStageFlags2 stage,
                                                     VkQueryPool queryPool, uint32_t query);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateBufferView(VkDevice device,
                                                       const VkBufferViewCreateInfo* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkBufferView* pView);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorUpdateTemplate(
    VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePrivateDataSlot(
    VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateQueryPool(VkDevice device,
                                                      const VkQueryPoolCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkQueryPool* pQueryPool);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateRenderPass2(VkDevice device,
                                                        const VkRenderPassCreateInfo2* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkRenderPass* pRenderPass);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSamplerYcbcrConversion(
    VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                                                    const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDescriptorUpdateTemplate(
    VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPrivateDataSlot(VkDevice device,
                                                         VkPrivateDataSlot privateDataSlot,
                                                         const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                                                   const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL
Wrap_vkDestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion,
                                     const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumeratePhysicalDeviceGroups(
    VkInstance instance, uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges);
VKAPI_ATTR VkDeviceAddress VKAPI_CALL
Wrap_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VKAPI_ATTR void VKAPI_CALL
Wrap_vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo,
                                    VkMemoryRequirements2* pMemoryRequirements);
VKAPI_ATTR uint64_t VKAPI_CALL
Wrap_vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDescriptorSetLayoutSupport(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    VkDescriptorSetLayoutSupport* pSupport);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceBufferMemoryRequirements(
    VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceGroupPeerMemoryFeatures(
    VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex,
    VkPeerMemoryFeatureFlags* pPeerMemoryFeatures);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceImageMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceImageSparseMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                                                            VkDeviceSize* pCommittedMemoryInBytes);
VKAPI_ATTR uint64_t VKAPI_CALL Wrap_vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceQueue2(VkDevice device,
                                                  const VkDeviceQueueInfo2* pQueueInfo,
                                                  VkQueue* pQueue);
VKAPI_ATTR void VKAPI_CALL
Wrap_vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo,
                                   VkMemoryRequirements2* pMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSparseMemoryRequirements(
    VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements* pSparseMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSparseMemoryRequirements2(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSubresourceLayout(VkDevice device, VkImage image,
                                                            const VkImageSubresource* pSubresource,
                                                            VkSubresourceLayout* pLayout);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
    VkExternalBufferProperties* pExternalBufferProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
    VkExternalFenceProperties* pExternalFenceProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties* pExternalSemaphoreProperties);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects);
VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t* pToolCount,
                                       VkPhysicalDeviceToolProperties* pToolProperties);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPrivateData(VkDevice device, VkObjectType objectType,
                                                 uint64_t objectHandle,
                                                 VkPrivateDataSlot privateDataSlot,
                                                 uint64_t* pData);
VKAPI_ATTR void VKAPI_CALL Wrap_vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                                                           VkExtent2D* pGranularity);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetSemaphoreCounterValue(VkDevice device,
                                                               VkSemaphore semaphore,
                                                               uint64_t* pValue);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                                                          uint32_t srcCacheCount,
                                                          const VkPipelineCache* pSrcCaches);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                                                      const VkBindSparseInfo* pBindInfo,
                                                      VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueSubmit2(VkQueue queue, uint32_t submitCount,
                                                   const VkSubmitInfo2* pSubmits, VkFence fence);
VKAPI_ATTR void VKAPI_CALL Wrap_vkResetQueryPool(VkDevice device, VkQueryPool queryPool,
                                                 uint32_t firstQuery, uint32_t queryCount);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSetPrivateData(VkDevice device, VkObjectType objectType,
                                                     uint64_t objectHandle,
                                                     VkPrivateDataSlot privateDataSlot,
                                                     uint64_t data);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSignalSemaphore(VkDevice device,
                                                      const VkSemaphoreSignalInfo* pSignalInfo);
VKAPI_ATTR void VKAPI_CALL Wrap_vkTrimCommandPool(VkDevice device, VkCommandPool commandPool,
                                                  VkCommandPoolTrimFlags flags);
VKAPI_ATTR void VKAPI_CALL Wrap_vkUnmapMemory(VkDevice device, VkDeviceMemory memory);
VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkWaitSemaphores(VkDevice device,
                                                     const VkSemaphoreWaitInfo* pWaitInfo,
                                                     uint64_t timeout);

// Name -> function-pointer entries for vk_icdGetInstanceProcAddr.
struct Entry {
    const char* name;
    PFN_vkVoidFunction fn;
};

// Span of generated wrapper entries. The ICD layer prepends its own
// non-wire entries (loader negotiation, GetInstanceProcAddr, etc.).
std::span<const Entry> wrapper_entries() noexcept;

}  // namespace lsw::icd::generated
