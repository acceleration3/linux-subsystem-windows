#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

#include <string>

#define LSW_VK_INSTANCE_FUNCTIONS(X)           \
    X(vkDestroyInstance)                       \
    X(vkEnumeratePhysicalDevices)              \
    X(vkGetPhysicalDeviceProperties)           \
    X(vkGetPhysicalDeviceProperties2)          \
    X(vkGetPhysicalDeviceFeatures)             \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties)     \
    X(vkGetPhysicalDeviceFormatProperties)     \
    X(vkEnumerateDeviceExtensionProperties)    \
    X(vkCreateDevice)                          \
    X(vkGetDeviceProcAddr)

#define LSW_VK_OPTIONAL_INSTANCE_FUNCTIONS(X)        \
    X(vkDestroySurfaceKHR)                           \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)          \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)     \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)          \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define LSW_VK_DEVICE_FUNCTIONS(X)       \
    X(vkDestroyDevice)                   \
    X(vkGetDeviceQueue)                  \
    X(vkQueueSubmit)                     \
    X(vkQueueWaitIdle)                   \
    X(vkDeviceWaitIdle)                  \
    X(vkCreateCommandPool)               \
    X(vkDestroyCommandPool)              \
    X(vkAllocateCommandBuffers)          \
    X(vkFreeCommandBuffers)              \
    X(vkBeginCommandBuffer)              \
    X(vkEndCommandBuffer)                \
    X(vkResetCommandBuffer)              \
    X(vkCreateBuffer)                    \
    X(vkDestroyBuffer)                   \
    X(vkGetBufferMemoryRequirements)     \
    X(vkAllocateMemory)                  \
    X(vkFreeMemory)                      \
    X(vkBindBufferMemory)                \
    X(vkMapMemory)                       \
    X(vkUnmapMemory)                     \
    X(vkFlushMappedMemoryRanges)         \
    X(vkInvalidateMappedMemoryRanges)    \
    X(vkCreateImage)                     \
    X(vkDestroyImage)                    \
    X(vkGetImageMemoryRequirements)      \
    X(vkBindImageMemory)                 \
    X(vkCreateImageView)                 \
    X(vkDestroyImageView)                \
    X(vkCreateSampler)                   \
    X(vkDestroySampler)                  \
    X(vkCreateShaderModule)              \
    X(vkDestroyShaderModule)             \
    X(vkCreatePipelineLayout)            \
    X(vkDestroyPipelineLayout)           \
    X(vkCreateGraphicsPipelines)         \
    X(vkCreateComputePipelines)          \
    X(vkDestroyPipeline)                 \
    X(vkCreateRenderPass)                \
    X(vkDestroyRenderPass)               \
    X(vkCreateFramebuffer)               \
    X(vkDestroyFramebuffer)              \
    X(vkCreateDescriptorSetLayout)       \
    X(vkDestroyDescriptorSetLayout)      \
    X(vkCreateDescriptorPool)            \
    X(vkDestroyDescriptorPool)           \
    X(vkAllocateDescriptorSets)          \
    X(vkUpdateDescriptorSets)            \
    X(vkCreateFence)                     \
    X(vkDestroyFence)                    \
    X(vkWaitForFences)                   \
    X(vkResetFences)                     \
    X(vkCreateSemaphore)                 \
    X(vkDestroySemaphore)                \
    X(vkCmdBeginRenderPass)              \
    X(vkCmdEndRenderPass)                \
    X(vkCmdBindPipeline)                 \
    X(vkCmdBindVertexBuffers)            \
    X(vkCmdBindIndexBuffer)              \
    X(vkCmdBindDescriptorSets)           \
    X(vkCmdDraw)                         \
    X(vkCmdDrawIndexed)                  \
    X(vkCmdSetViewport)                  \
    X(vkCmdSetScissor)                   \
    X(vkCmdSetStencilReference)          \
    X(vkCmdSetBlendConstants)            \
    X(vkCmdPushConstants)                \
    X(vkCmdPipelineBarrier)              \
    X(vkCmdCopyBufferToImage)            \
    X(vkCmdCopyImageToBuffer)            \
    X(vkCmdCopyBuffer)                   \
    X(vkCmdUpdateBuffer)                 \
    X(vkCmdBlitImage)                    \
    X(vkCmdClearColorImage)              \
    X(vkCmdDispatch)

#define LSW_VK_OPTIONAL_DEVICE_FUNCTIONS(X) \
    X(vkCreateSwapchainKHR)                 \
    X(vkDestroySwapchainKHR)                \
    X(vkGetSwapchainImagesKHR)              \
    X(vkAcquireNextImageKHR)                \
    X(vkQueuePresentKHR)

namespace lsw::render::vk {
class Api {
public:
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
#define LSW_VK_MEMBER(name) PFN_##name name = nullptr;
    LSW_VK_INSTANCE_FUNCTIONS(LSW_VK_MEMBER)
    LSW_VK_OPTIONAL_INSTANCE_FUNCTIONS(LSW_VK_MEMBER)
    LSW_VK_DEVICE_FUNCTIONS(LSW_VK_MEMBER)
    LSW_VK_OPTIONAL_DEVICE_FUNCTIONS(LSW_VK_MEMBER)
#undef LSW_VK_MEMBER

    [[nodiscard]] bool load_global(PFN_vkGetInstanceProcAddr loader, std::string& error);
    [[nodiscard]] bool load_instance(VkInstance instance, std::string& error);
    [[nodiscard]] bool load_device(VkDevice device, std::string& error);

    [[nodiscard]] static std::string result_name(VkResult result);
};
}  // namespace lsw::render::vk
