#include "support/vulkan_interop.hpp"

namespace lsw::suites {

VulkanRigOptions VulkanInterop::options() {
    VulkanRigOptions out;
    out.device_extensions = {vkw::kExternalMemoryWin32, vkw::kExternalSemaphoreWin32, vkw::kExternalFenceWin32};
    return out;
}

bool VulkanInterop::ready(const VulkanRig& rig, std::string& reason) {
    if (!rig.enabled_has(vkw::kExternalMemoryWin32) || rig.fn.vkGetMemoryWin32HandleKHR == nullptr || rig.fn.vkGetPhysicalDeviceExternalBufferProperties == nullptr) {
        reason = "VK_KHR_external_memory_win32 is not available on the device";
        return false;
    }
    return true;
}

VkExternalMemoryFeatureFlags VulkanInterop::buffer_features(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkBufferUsageFlags usage) {
    if (rig.fn.vkGetPhysicalDeviceExternalBufferProperties == nullptr) {
        return 0;
    }
    VkPhysicalDeviceExternalBufferInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    info.usage = usage;
    info.handleType = type;
    VkExternalBufferProperties properties{};
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    rig.fn.vkGetPhysicalDeviceExternalBufferProperties(rig.physical, &info, &properties);
    return properties.externalMemoryProperties.externalMemoryFeatures;
}

VkExternalMemoryFeatureFlags VulkanInterop::image_features(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkFormat format, VkImageUsageFlags usage, VkResult* code) {
    VkPhysicalDeviceExternalImageFormatInfo external{};
    external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external.handleType = type;
    VkPhysicalDeviceImageFormatInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.pNext = &external;
    info.format = format;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VkExternalImageFormatProperties external_properties{};
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    properties.pNext = &external_properties;
    const VkResult result = rig.fn.vkGetPhysicalDeviceImageFormatProperties2(rig.physical, &info, &properties);
    if (code != nullptr) {
        *code = result;
    }
    return result == VK_SUCCESS ? external_properties.externalMemoryProperties.externalMemoryFeatures : 0;
}

VkExternalSemaphoreFeatureFlags VulkanInterop::semaphore_features(VulkanRig& rig, VkExternalSemaphoreHandleTypeFlagBits type, bool timeline) {
    if (rig.fn.vkGetPhysicalDeviceExternalSemaphoreProperties == nullptr) {
        return 0;
    }
    VkSemaphoreTypeCreateInfo kind{};
    kind.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    kind.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkPhysicalDeviceExternalSemaphoreInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    info.pNext = timeline ? &kind : nullptr;
    info.handleType = type;
    VkExternalSemaphoreProperties properties{};
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    rig.fn.vkGetPhysicalDeviceExternalSemaphoreProperties(rig.physical, &info, &properties);
    return properties.externalSemaphoreFeatures;
}

VkResult VulkanInterop::make_semaphore(VulkanRig& rig, VulkanOwned<VkSemaphore>& out, bool timeline, VkExternalSemaphoreHandleTypeFlags exportable) {
    VkSemaphoreTypeCreateInfo kind{};
    kind.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    kind.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkExportSemaphoreCreateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_info.handleTypes = exportable;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    const void* chain = nullptr;
    if (exportable != 0) {
        export_info.pNext = chain;
        chain = &export_info;
    }
    if (timeline) {
        kind.pNext = chain;
        chain = &kind;
    }
    info.pNext = chain;
    return rig.make(out, rig.fn.vkCreateSemaphore, rig.fn.vkDestroySemaphore, info);
}

VkResult VulkanInterop::semaphore_handle(VulkanRig& rig, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits type, HANDLE& handle) {
    if (rig.fn.vkGetSemaphoreWin32HandleKHR == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    vkw::SemaphoreGetHandleInfo info{vkw::kSemaphoreGetWin32Handle, nullptr, semaphore, type};
    return rig.fn.vkGetSemaphoreWin32HandleKHR(rig.device, &info, &handle);
}

VkResult VulkanInterop::import_semaphore(VulkanRig& rig, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits type, HANDLE handle) {
    if (rig.fn.vkImportSemaphoreWin32HandleKHR == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    vkw::ImportSemaphoreInfo info{vkw::kImportSemaphoreWin32, nullptr, semaphore, 0, type, handle, nullptr};
    return rig.fn.vkImportSemaphoreWin32HandleKHR(rig.device, &info);
}

VkResult VulkanInterop::run(VulkanRig& rig, const std::function<void(VkCommandBuffer)>& record, const std::vector<VulkanStep>& waits, const std::vector<VulkanStep>& signals) {
    VulkanQueue& queue = rig.main();
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = queue.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = nullptr;
    VkResult result = rig.fn.vkAllocateCommandBuffers(rig.device, &allocate, &commands);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = rig.fn.vkBeginCommandBuffer(commands, &begin);
    if (result == VK_SUCCESS) {
        record(commands);
        result = rig.fn.vkEndCommandBuffer(commands);
    }
    VulkanOwned<VkFence> fence;
    if (result == VK_SUCCESS) {
        result = rig.make_fence(fence);
    }
    if (result != VK_SUCCESS) {
        rig.fn.vkFreeCommandBuffers(rig.device, queue.pool, 1, &commands);
        return result;
    }
    std::vector<VkSemaphore> wait_handles;
    std::vector<std::uint64_t> wait_values;
    std::vector<VkPipelineStageFlags> stages;
    for (const VulkanStep& step : waits) {
        wait_handles.push_back(step.semaphore);
        wait_values.push_back(step.value);
        stages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    std::vector<VkSemaphore> signal_handles;
    std::vector<std::uint64_t> signal_values;
    for (const VulkanStep& step : signals) {
        signal_handles.push_back(step.semaphore);
        signal_values.push_back(step.value);
    }
    VkTimelineSemaphoreSubmitInfo timeline{};
    timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline.waitSemaphoreValueCount = static_cast<std::uint32_t>(wait_values.size());
    timeline.pWaitSemaphoreValues = wait_values.data();
    timeline.signalSemaphoreValueCount = static_cast<std::uint32_t>(signal_values.size());
    timeline.pSignalSemaphoreValues = signal_values.data();
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = rig.timeline ? static_cast<const void*>(&timeline) : nullptr;
    submit.waitSemaphoreCount = static_cast<std::uint32_t>(wait_handles.size());
    submit.pWaitSemaphores = wait_handles.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    submit.signalSemaphoreCount = static_cast<std::uint32_t>(signal_handles.size());
    submit.pSignalSemaphores = signal_handles.data();
    result = rig.submit(queue, 1, &submit, fence.get());
    if (result == VK_SUCCESS) {
        result = rig.wait_fence(fence.get(), 20ull * 1000000000ull);
    }
    if (result == VK_SUCCESS) {
        rig.fn.vkFreeCommandBuffers(rig.device, queue.pool, 1, &commands);
    }
    return result;
}

void VulkanSharedMemory::destroy() {
    if (rig_ == nullptr) {
        return;
    }
    rig_->wait_idle();
    if (buffer != VK_NULL_HANDLE) {
        rig_->fn.vkDestroyBuffer(rig_->device, buffer, nullptr);
    }
    if (image != VK_NULL_HANDLE) {
        rig_->fn.vkDestroyImage(rig_->device, image, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        rig_->fn.vkFreeMemory(rig_->device, memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    rig_ = nullptr;
}

VkResult VulkanSharedMemory::allocate(VkExternalMemoryHandleTypeFlagBits type, const VkMemoryRequirements& requirements, HANDLE import_handle, bool dedicated_buffer,
                                      bool dedicated_image) {
    std::uint32_t bits = requirements.memoryTypeBits;
    if (import_handle != nullptr && type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT && type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT) {
        vkw::MemoryHandleProperties properties{vkw::kMemoryWin32HandleProperties, nullptr, 0};
        if (rig_->fn.vkGetMemoryWin32HandlePropertiesKHR == nullptr) {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        const VkResult result = rig_->fn.vkGetMemoryWin32HandlePropertiesKHR(rig_->device, type, import_handle, &properties);
        if (result != VK_SUCCESS) {
            return result;
        }
        bits &= properties.memoryTypeBits;
    }
    const int index = rig_->find_type(bits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (index < 0) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.buffer = dedicated_buffer ? buffer : VK_NULL_HANDLE;
    dedicated.image = dedicated_image ? image : VK_NULL_HANDLE;
    const bool use_dedicated = (dedicated_buffer || dedicated_image) && type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT;
    vkw::ImportMemoryInfo import_info{vkw::kImportMemoryWin32, use_dedicated ? &dedicated : nullptr, type, import_handle, nullptr};
    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.pNext = use_dedicated ? &dedicated : nullptr;
    export_info.handleTypes = type;
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.pNext = import_handle != nullptr ? static_cast<const void*>(&import_info) : static_cast<const void*>(&export_info);
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = static_cast<std::uint32_t>(index);
    allocation = requirements.size;
    return rig_->fn.vkAllocateMemory(rig_->device, &info, nullptr, &memory);
}

VkResult VulkanSharedMemory::export_buffer(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkDeviceSize bytes, VkBufferUsageFlags usage) {
    destroy();
    rig_ = &rig;
    size = bytes;
    VkExternalMemoryBufferCreateInfo external{};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    external.handleTypes = type;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext = &external;
    info.size = bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = rig.fn.vkCreateBuffer(rig.device, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return result;
    }
    VkMemoryRequirements requirements{};
    rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer, &requirements);
    result = allocate(type, requirements, nullptr, true, false);
    return result == VK_SUCCESS ? rig.fn.vkBindBufferMemory(rig.device, buffer, memory, 0) : result;
}

VkResult VulkanSharedMemory::import_buffer(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, HANDLE handle, VkDeviceSize bytes, VkBufferUsageFlags usage) {
    destroy();
    rig_ = &rig;
    size = bytes;
    VkExternalMemoryBufferCreateInfo external{};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    external.handleTypes = type;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext = &external;
    info.size = bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = rig.fn.vkCreateBuffer(rig.device, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return result;
    }
    VkMemoryRequirements requirements{};
    rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer, &requirements);
    result = allocate(type, requirements, handle, true, false);
    return result == VK_SUCCESS ? rig.fn.vkBindBufferMemory(rig.device, buffer, memory, 0) : result;
}

VkResult VulkanSharedMemory::import_image(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, HANDLE handle, VkFormat format, std::uint32_t width, std::uint32_t height,
                                          VkImageUsageFlags usage) {
    destroy();
    rig_ = &rig;
    VkExternalMemoryImageCreateInfo external{};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.handleTypes = type;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = &external;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = rig.fn.vkCreateImage(rig.device, &info, nullptr, &image);
    if (result != VK_SUCCESS) {
        image = VK_NULL_HANDLE;
        return result;
    }
    VkMemoryRequirements requirements{};
    rig.fn.vkGetImageMemoryRequirements(rig.device, image, &requirements);
    result = allocate(type, requirements, handle, false, true);
    return result == VK_SUCCESS ? rig.fn.vkBindImageMemory(rig.device, image, memory, 0) : result;
}

VkResult VulkanSharedMemory::handle(VkExternalMemoryHandleTypeFlagBits type, HANDLE& out) const {
    if (rig_ == nullptr || rig_->fn.vkGetMemoryWin32HandleKHR == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    vkw::MemoryGetHandleInfo info{vkw::kMemoryGetWin32Handle, nullptr, memory, type};
    return rig_->fn.vkGetMemoryWin32HandleKHR(rig_->device, &info, &out);
}

}  // namespace lsw::suites
