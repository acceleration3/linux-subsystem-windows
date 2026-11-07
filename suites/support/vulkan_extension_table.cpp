#include "support/vulkan_extension_table.hpp"

#include <map>

namespace lsw::suites {

std::vector<std::string> VulkanExtensionTable::instance_functions(const std::string& extension) {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"VK_KHR_surface",
         {"vkDestroySurfaceKHR", "vkGetPhysicalDeviceSurfaceSupportKHR", "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
          "vkGetPhysicalDeviceSurfaceFormatsKHR", "vkGetPhysicalDeviceSurfacePresentModesKHR"}},
        {"VK_KHR_win32_surface", {"vkCreateWin32SurfaceKHR", "vkGetPhysicalDeviceWin32PresentationSupportKHR"}},
        {"VK_EXT_debug_utils", {"vkCreateDebugUtilsMessengerEXT", "vkDestroyDebugUtilsMessengerEXT", "vkSubmitDebugUtilsMessageEXT"}},
        {"VK_EXT_debug_report", {"vkCreateDebugReportCallbackEXT", "vkDestroyDebugReportCallbackEXT", "vkDebugReportMessageEXT"}},
        {"VK_KHR_get_physical_device_properties2",
         {"vkGetPhysicalDeviceFeatures2KHR", "vkGetPhysicalDeviceProperties2KHR", "vkGetPhysicalDeviceFormatProperties2KHR",
          "vkGetPhysicalDeviceImageFormatProperties2KHR", "vkGetPhysicalDeviceQueueFamilyProperties2KHR",
          "vkGetPhysicalDeviceMemoryProperties2KHR", "vkGetPhysicalDeviceSparseImageFormatProperties2KHR"}},
        {"VK_KHR_external_memory_capabilities", {"vkGetPhysicalDeviceExternalBufferPropertiesKHR"}},
        {"VK_KHR_external_semaphore_capabilities", {"vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"}},
        {"VK_KHR_external_fence_capabilities", {"vkGetPhysicalDeviceExternalFencePropertiesKHR"}},
        {"VK_KHR_get_surface_capabilities2",
         {"vkGetPhysicalDeviceSurfaceCapabilities2KHR", "vkGetPhysicalDeviceSurfaceFormats2KHR"}},
        {"VK_KHR_device_group_creation", {"vkEnumeratePhysicalDeviceGroupsKHR"}},
    };
    const auto found = table.find(extension);
    return found == table.end() ? std::vector<std::string>() : found->second;
}

std::vector<std::string> VulkanExtensionTable::device_functions(const std::string& extension) {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"VK_KHR_swapchain",
         {"vkCreateSwapchainKHR", "vkDestroySwapchainKHR", "vkGetSwapchainImagesKHR", "vkAcquireNextImageKHR", "vkQueuePresentKHR"}},
        {"VK_KHR_timeline_semaphore", {"vkGetSemaphoreCounterValueKHR", "vkWaitSemaphoresKHR", "vkSignalSemaphoreKHR"}},
        {"VK_KHR_synchronization2",
         {"vkCmdSetEvent2KHR", "vkCmdResetEvent2KHR", "vkCmdWaitEvents2KHR", "vkCmdPipelineBarrier2KHR", "vkCmdWriteTimestamp2KHR",
          "vkQueueSubmit2KHR"}},
        {"VK_KHR_dynamic_rendering", {"vkCmdBeginRenderingKHR", "vkCmdEndRenderingKHR"}},
        {"VK_KHR_push_descriptor", {"vkCmdPushDescriptorSetKHR"}},
        {"VK_KHR_descriptor_update_template",
         {"vkCreateDescriptorUpdateTemplateKHR", "vkDestroyDescriptorUpdateTemplateKHR", "vkUpdateDescriptorSetWithTemplateKHR"}},
        {"VK_KHR_get_memory_requirements2",
         {"vkGetImageMemoryRequirements2KHR", "vkGetBufferMemoryRequirements2KHR", "vkGetImageSparseMemoryRequirements2KHR"}},
        {"VK_KHR_bind_memory2", {"vkBindBufferMemory2KHR", "vkBindImageMemory2KHR"}},
        {"VK_KHR_maintenance1", {"vkTrimCommandPoolKHR"}},
        {"VK_KHR_maintenance3", {"vkGetDescriptorSetLayoutSupportKHR"}},
        {"VK_KHR_maintenance4",
         {"vkGetDeviceBufferMemoryRequirementsKHR", "vkGetDeviceImageMemoryRequirementsKHR",
          "vkGetDeviceImageSparseMemoryRequirementsKHR"}},
        {"VK_KHR_external_memory_win32", {"vkGetMemoryWin32HandleKHR", "vkGetMemoryWin32HandlePropertiesKHR"}},
        {"VK_KHR_external_semaphore_win32", {"vkImportSemaphoreWin32HandleKHR", "vkGetSemaphoreWin32HandleKHR"}},
        {"VK_KHR_external_fence_win32", {"vkImportFenceWin32HandleKHR", "vkGetFenceWin32HandleKHR"}},
        {"VK_EXT_host_query_reset", {"vkResetQueryPoolEXT"}},
        {"VK_EXT_calibrated_timestamps", {"vkGetCalibratedTimestampsEXT"}},
        {"VK_KHR_calibrated_timestamps", {"vkGetCalibratedTimestampsKHR"}},
        {"VK_KHR_draw_indirect_count", {"vkCmdDrawIndirectCountKHR", "vkCmdDrawIndexedIndirectCountKHR"}},
        {"VK_KHR_create_renderpass2",
         {"vkCreateRenderPass2KHR", "vkCmdBeginRenderPass2KHR", "vkCmdNextSubpass2KHR", "vkCmdEndRenderPass2KHR"}},
        {"VK_KHR_buffer_device_address",
         {"vkGetBufferDeviceAddressKHR", "vkGetBufferOpaqueCaptureAddressKHR", "vkGetDeviceMemoryOpaqueCaptureAddressKHR"}},
        {"VK_KHR_device_group",
         {"vkGetDeviceGroupPeerMemoryFeaturesKHR", "vkCmdSetDeviceMaskKHR", "vkCmdDispatchBaseKHR"}},
        {"VK_KHR_copy_commands2",
         {"vkCmdCopyBuffer2KHR", "vkCmdCopyImage2KHR", "vkCmdCopyBufferToImage2KHR", "vkCmdCopyImageToBuffer2KHR",
          "vkCmdBlitImage2KHR", "vkCmdResolveImage2KHR"}},
        {"VK_KHR_map_memory2", {"vkMapMemory2KHR", "vkUnmapMemory2KHR"}},
        {"VK_KHR_sampler_ycbcr_conversion", {"vkCreateSamplerYcbcrConversionKHR", "vkDestroySamplerYcbcrConversionKHR"}},
        {"VK_KHR_present_wait", {"vkWaitForPresentKHR"}},
        {"VK_EXT_debug_marker", {"vkDebugMarkerSetObjectNameEXT", "vkCmdDebugMarkerBeginEXT", "vkCmdDebugMarkerEndEXT"}},
    };
    const auto found = table.find(extension);
    return found == table.end() ? std::vector<std::string>() : found->second;
}

std::vector<VulkanExtensionFeature> VulkanExtensionTable::feature_structs() {
    return {
        {"VK_KHR_timeline_semaphore", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES},
        {"VK_KHR_synchronization2", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES},
        {"VK_KHR_dynamic_rendering", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES},
        {"VK_KHR_vulkan_memory_model", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES},
        {"VK_EXT_host_query_reset", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES},
        {"VK_KHR_16bit_storage", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES},
        {"VK_KHR_8bit_storage", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES},
        {"VK_KHR_shader_float16_int8", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES},
        {"VK_KHR_buffer_device_address", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES},
        {"VK_EXT_descriptor_indexing", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES},
        {"VK_EXT_scalar_block_layout", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES},
        {"VK_KHR_uniform_buffer_standard_layout", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES},
        {"VK_KHR_separate_depth_stencil_layouts", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES},
        {"VK_KHR_imageless_framebuffer", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES},
        {"VK_KHR_shader_atomic_int64", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES},
        {"VK_KHR_shader_subgroup_extended_types", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES},
        {"VK_KHR_maintenance4", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES},
        {"VK_KHR_variable_pointers", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES},
        {"VK_KHR_multiview", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES},
        {"VK_KHR_sampler_ycbcr_conversion", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES},
        {"VK_KHR_shader_terminate_invocation", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES},
        {"VK_KHR_zero_initialize_workgroup_memory", VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES},
    };
}

}  // namespace lsw::suites
