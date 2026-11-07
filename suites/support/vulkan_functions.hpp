#pragma once

#define LSW_VK_INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceFeatures) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceImageFormatProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceSparseImageFormatProperties) \
    X(vkCreateDevice) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkEnumerateDeviceLayerProperties) \
    X(vkGetDeviceProcAddr)

#define LSW_VK_INSTANCE_ALIASED(X) \
    X(vkGetPhysicalDeviceProperties2, vkGetPhysicalDeviceProperties2KHR) \
    X(vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2KHR) \
    X(vkGetPhysicalDeviceFormatProperties2, vkGetPhysicalDeviceFormatProperties2KHR) \
    X(vkGetPhysicalDeviceImageFormatProperties2, vkGetPhysicalDeviceImageFormatProperties2KHR) \
    X(vkGetPhysicalDeviceQueueFamilyProperties2, vkGetPhysicalDeviceQueueFamilyProperties2KHR) \
    X(vkGetPhysicalDeviceMemoryProperties2, vkGetPhysicalDeviceMemoryProperties2KHR) \
    X(vkGetPhysicalDeviceSparseImageFormatProperties2, vkGetPhysicalDeviceSparseImageFormatProperties2KHR) \
    X(vkGetPhysicalDeviceExternalBufferProperties, vkGetPhysicalDeviceExternalBufferPropertiesKHR) \
    X(vkGetPhysicalDeviceExternalSemaphoreProperties, vkGetPhysicalDeviceExternalSemaphorePropertiesKHR) \
    X(vkGetPhysicalDeviceExternalFenceProperties, vkGetPhysicalDeviceExternalFencePropertiesKHR)

#define LSW_VK_INSTANCE_EXTENSION(X) \
    X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkCreateDebugUtilsMessengerEXT) \
    X(vkDestroyDebugUtilsMessengerEXT) \
    X(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)

#define LSW_VK_DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) \
    X(vkGetDeviceQueue) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkDeviceWaitIdle) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkMapMemory) \
    X(vkUnmapMemory) \
    X(vkFlushMappedMemoryRanges) \
    X(vkInvalidateMappedMemoryRanges) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetImageSparseMemoryRequirements) \
    X(vkQueueBindSparse) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkResetFences) \
    X(vkGetFenceStatus) \
    X(vkWaitForFences) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkCreateEvent) \
    X(vkDestroyEvent) \
    X(vkGetEventStatus) \
    X(vkSetEvent) \
    X(vkResetEvent) \
    X(vkCreateQueryPool) \
    X(vkDestroyQueryPool) \
    X(vkGetQueryPoolResults) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkCreateBufferView) \
    X(vkDestroyBufferView) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkGetImageSubresourceLayout) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreatePipelineCache) \
    X(vkDestroyPipelineCache) \
    X(vkGetPipelineCacheData) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateComputePipelines) \
    X(vkDestroyPipeline) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkResetDescriptorPool) \
    X(vkAllocateDescriptorSets) \
    X(vkFreeDescriptorSets) \
    X(vkUpdateDescriptorSets) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkResetCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdSetLineWidth) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdDrawIndirect) \
    X(vkCmdDrawIndexedIndirect) \
    X(vkCmdDispatch) \
    X(vkCmdDispatchIndirect) \
    X(vkCmdCopyBuffer) \
    X(vkCmdCopyImage) \
    X(vkCmdBlitImage) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdUpdateBuffer) \
    X(vkCmdFillBuffer) \
    X(vkCmdClearColorImage) \
    X(vkCmdClearDepthStencilImage) \
    X(vkCmdClearAttachments) \
    X(vkCmdResolveImage) \
    X(vkCmdSetEvent) \
    X(vkCmdResetEvent) \
    X(vkCmdWaitEvents) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdBeginQuery) \
    X(vkCmdEndQuery) \
    X(vkCmdResetQueryPool) \
    X(vkCmdWriteTimestamp) \
    X(vkCmdCopyQueryPoolResults) \
    X(vkCmdPushConstants) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdNextSubpass) \
    X(vkCmdEndRenderPass) \
    X(vkCmdExecuteCommands)

#define LSW_VK_DEVICE_ALIASED(X) \
    X(vkGetBufferMemoryRequirements2, vkGetBufferMemoryRequirements2KHR) \
    X(vkGetImageMemoryRequirements2, vkGetImageMemoryRequirements2KHR) \
    X(vkBindBufferMemory2, vkBindBufferMemory2KHR) \
    X(vkBindImageMemory2, vkBindImageMemory2KHR) \
    X(vkCreateDescriptorUpdateTemplate, vkCreateDescriptorUpdateTemplateKHR) \
    X(vkDestroyDescriptorUpdateTemplate, vkDestroyDescriptorUpdateTemplateKHR) \
    X(vkUpdateDescriptorSetWithTemplate, vkUpdateDescriptorSetWithTemplateKHR) \
    X(vkResetQueryPool, vkResetQueryPoolEXT) \
    X(vkWaitSemaphores, vkWaitSemaphoresKHR) \
    X(vkSignalSemaphore, vkSignalSemaphoreKHR) \
    X(vkGetSemaphoreCounterValue, vkGetSemaphoreCounterValueKHR) \
    X(vkQueueSubmit2, vkQueueSubmit2KHR) \
    X(vkCmdPipelineBarrier2, vkCmdPipelineBarrier2KHR) \
    X(vkCmdSetEvent2, vkCmdSetEvent2KHR) \
    X(vkCmdWaitEvents2, vkCmdWaitEvents2KHR) \
    X(vkCmdResetEvent2, vkCmdResetEvent2KHR) \
    X(vkCmdWriteTimestamp2, vkCmdWriteTimestamp2KHR) \
    X(vkCmdBeginRendering, vkCmdBeginRenderingKHR) \
    X(vkCmdEndRendering, vkCmdEndRenderingKHR)

#define LSW_VK_DEVICE_EXTENSION(X) \
    X(vkCmdPushDescriptorSetKHR) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
    X(vkGetCalibratedTimestampsEXT)
