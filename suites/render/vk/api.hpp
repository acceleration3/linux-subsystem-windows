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
    X(vkGetPhysicalDeviceFeatures2)            \
    X(vkGetPhysicalDeviceFormatProperties2)    \
    X(vkGetPhysicalDeviceImageFormatProperties) \
    X(vkGetPhysicalDeviceImageFormatProperties2) \
    X(vkGetPhysicalDeviceFeatures)             \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties)     \
    X(vkGetPhysicalDeviceFormatProperties)     \
    X(vkEnumerateDeviceExtensionProperties)    \
    X(vkCreateDevice)                          \
    X(vkGetDeviceProcAddr)

#define LSW_VK_OPTIONAL_INSTANCE_FUNCTIONS(X)        \
    X(vkGetPhysicalDeviceFragmentShadingRatesKHR)    \
    X(vkGetPhysicalDeviceSparseImageFormatProperties) \
    X(vkGetPhysicalDeviceMultisamplePropertiesEXT)   \
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

#define LSW_VK_LAZY_DEVICE_FUNCTIONS(X) \
    X(vkCreateQueryPool)                    \
    X(vkDestroyQueryPool)                   \
    X(vkGetQueryPoolResults)                \
    X(vkCmdResetQueryPool)                  \
    X(vkCmdBeginQuery)                      \
    X(vkCmdEndQuery)                        \
    X(vkCmdWriteTimestamp)                  \
    X(vkCmdCopyQueryPoolResults)            \
    X(vkCmdDrawIndirect)                    \
    X(vkCmdDrawIndexedIndirect)             \
    X(vkCmdDispatchIndirect)                \
    X(vkCmdFillBuffer)                      \
    X(vkCmdCopyImage)                       \
    X(vkCmdResolveImage)                    \
    X(vkCmdClearDepthStencilImage)          \
    X(vkCmdClearAttachments)                \
    X(vkCmdNextSubpass)                     \
    X(vkCmdSetLineWidth)                    \
    X(vkCmdSetDepthBias)                    \
    X(vkCmdSetDepthBounds)                  \
    X(vkCmdSetStencilCompareMask)           \
    X(vkCmdSetStencilWriteMask)             \
    X(vkCmdExecuteCommands)                 \
    X(vkCmdSetEvent)                        \
    X(vkCmdResetEvent)                      \
    X(vkCmdWaitEvents)                      \
    X(vkCreateEvent)                        \
    X(vkDestroyEvent)                       \
    X(vkSetEvent)                           \
    X(vkGetEventStatus)                     \
    X(vkCreatePipelineCache)                \
    X(vkDestroyPipelineCache)               \
    X(vkGetPipelineCacheData)               \
    X(vkMergePipelineCaches)                \
    X(vkCreateBufferView)                   \
    X(vkDestroyBufferView)                  \
    X(vkResetDescriptorPool)                \
    X(vkFreeDescriptorSets)                 \
    X(vkResetCommandPool)                   \
    X(vkGetImageSubresourceLayout)          \
    X(vkGetFenceStatus)                     \
    X(vkQueueBindSparse)                    \
    X(vkGetImageSparseMemoryRequirements)   \
    X(vkCreateSamplerYcbcrConversion)       \
    X(vkDestroySamplerYcbcrConversion)      \
    X(vkCreateDescriptorUpdateTemplate)     \
    X(vkDestroyDescriptorUpdateTemplate)    \
    X(vkUpdateDescriptorSetWithTemplate)    \
    X(vkCmdPushDescriptorSetKHR)            \
    X(vkCmdPushDescriptorSetWithTemplateKHR) \
    X(vkBindBufferMemory2)                  \
    X(vkBindImageMemory2)                   \
    X(vkCmdDrawIndirectCount)               \
    X(vkCmdDrawIndexedIndirectCount)        \
    X(vkCreateRenderPass2)                  \
    X(vkCmdBeginRenderPass2)                \
    X(vkCmdNextSubpass2)                    \
    X(vkCmdEndRenderPass2)                  \
    X(vkGetSemaphoreCounterValue)           \
    X(vkWaitSemaphores)                     \
    X(vkSignalSemaphore)                    \
    X(vkGetBufferDeviceAddress)             \
    X(vkQueueSubmit2)                       \
    X(vkCmdBeginRendering)                  \
    X(vkCmdEndRendering)                    \
    X(vkCmdPipelineBarrier2)                \
    X(vkCmdSetEvent2)                       \
    X(vkCmdWaitEvents2)                     \
    X(vkCmdResetEvent2)                     \
    X(vkCmdWriteTimestamp2)                 \
    X(vkCmdCopyBuffer2)                     \
    X(vkCmdCopyImage2)                      \
    X(vkCmdCopyBufferToImage2)              \
    X(vkCmdCopyImageToBuffer2)              \
    X(vkCmdBlitImage2)                      \
    X(vkCmdResolveImage2)                   \
    X(vkCmdSetCullMode)                     \
    X(vkCmdSetFrontFace)                    \
    X(vkCmdSetPrimitiveTopology)            \
    X(vkCmdSetViewportWithCount)            \
    X(vkCmdSetScissorWithCount)             \
    X(vkCmdBindVertexBuffers2)              \
    X(vkCmdSetDepthTestEnable)              \
    X(vkCmdSetDepthWriteEnable)             \
    X(vkCmdSetDepthCompareOp)               \
    X(vkCmdSetDepthBoundsTestEnable)        \
    X(vkCmdSetStencilTestEnable)            \
    X(vkCmdSetStencilOp)                    \
    X(vkCmdSetRasterizerDiscardEnable)      \
    X(vkCmdSetDepthBiasEnable)              \
    X(vkCmdSetPrimitiveRestartEnable)       \
    X(vkCmdSetPatchControlPointsEXT)        \
    X(vkCmdSetLogicOpEXT)                   \
    X(vkCmdSetPolygonModeEXT)               \
    X(vkCmdSetRasterizationSamplesEXT)      \
    X(vkCmdSetSampleMaskEXT)                \
    X(vkCmdSetAlphaToCoverageEnableEXT)     \
    X(vkCmdSetAlphaToOneEnableEXT)          \
    X(vkCmdSetLogicOpEnableEXT)             \
    X(vkCmdSetColorBlendEnableEXT)          \
    X(vkCmdSetColorBlendEquationEXT)        \
    X(vkCmdSetColorWriteMaskEXT)            \
    X(vkCmdSetDepthClampEnableEXT)          \
    X(vkCmdSetDepthClipEnableEXT)           \
    X(vkCmdSetLineRasterizationModeEXT)     \
    X(vkCmdSetLineStippleEnableEXT)         \
    X(vkCmdSetLineStippleEXT)               \
    X(vkCmdSetProvokingVertexModeEXT)       \
    X(vkCmdSetVertexInputEXT)               \
    X(vkCmdSetColorWriteEnableEXT)          \
    X(vkCmdDrawMultiEXT)                    \
    X(vkCmdDrawMultiIndexedEXT)             \
    X(vkCmdBindTransformFeedbackBuffersEXT) \
    X(vkCmdBeginTransformFeedbackEXT)       \
    X(vkCmdEndTransformFeedbackEXT)         \
    X(vkCmdDrawIndirectByteCountEXT)        \
    X(vkCmdBeginConditionalRenderingEXT)    \
    X(vkCmdEndConditionalRenderingEXT)      \
    X(vkCmdDrawMeshTasksEXT)                \
    X(vkCmdDrawMeshTasksIndirectEXT)        \
    X(vkCmdDrawMeshTasksIndirectCountEXT)   \
    X(vkCmdSetSampleLocationsEXT)           \
    X(vkCmdSetFragmentShadingRateKHR)       \
    X(vkCreateShadersEXT)                   \
    X(vkDestroyShaderEXT)                   \
    X(vkCmdBindShadersEXT)                  \
    X(vkGetDescriptorSetLayoutSizeEXT)      \
    X(vkGetDescriptorSetLayoutBindingOffsetEXT) \
    X(vkGetDescriptorEXT)                   \
    X(vkCmdBindDescriptorBuffersEXT)        \
    X(vkCmdSetDescriptorBufferOffsetsEXT)   \
    X(vkCreateAccelerationStructureKHR)     \
    X(vkDestroyAccelerationStructureKHR)    \
    X(vkGetAccelerationStructureBuildSizesKHR) \
    X(vkCmdBuildAccelerationStructuresKHR)  \
    X(vkGetAccelerationStructureDeviceAddressKHR) \
    X(vkCmdCopyAccelerationStructureKHR)    \
    X(vkCmdWriteAccelerationStructuresPropertiesKHR) \
    X(vkCreateRayTracingPipelinesKHR)       \
    X(vkGetRayTracingShaderGroupHandlesKHR) \
    X(vkCmdTraceRaysKHR)                    \
    X(vkCopyMemoryToImageEXT)               \
    X(vkCopyImageToMemoryEXT)               \
    X(vkTransitionImageLayoutEXT)

#define LSW_VK_OPTIONAL_DEVICE_FUNCTIONS(X) \
    X(vkCreateSwapchainKHR)                 \
    X(vkDestroySwapchainKHR)                \
    X(vkGetSwapchainImagesKHR)              \
    X(vkAcquireNextImageKHR)                \
    X(vkQueuePresentKHR)

namespace lsw::render::vk {
struct MissingFunction {
    const char* name;
};

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
    LSW_VK_LAZY_DEVICE_FUNCTIONS(LSW_VK_MEMBER)
#undef LSW_VK_MEMBER

    [[nodiscard]] bool load_global(PFN_vkGetInstanceProcAddr loader, std::string& error);
    [[nodiscard]] bool load_instance(VkInstance instance, std::string& error);
    [[nodiscard]] bool load_device(VkDevice device, std::string& error);

    [[nodiscard]] static std::string result_name(VkResult result);
};
}  // namespace lsw::render::vk
