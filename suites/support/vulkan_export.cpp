#include "support/vulkan_export.hpp"

#include <cstring>

namespace lsw::suites {

namespace {

constexpr VkExternalMemoryHandleTypeFlagBits kOpaqueWin32 = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
constexpr VkStructureType kGetWin32HandleType = static_cast<VkStructureType>(1000073003);

struct GetWin32HandleInfo {
    VkStructureType sType;
    const void* pNext;
    VkDeviceMemory memory;
    VkExternalMemoryHandleTypeFlagBits handleType;
};

using GetMemoryWin32Handle = VkResult(VKAPI_PTR*)(VkDevice, const GetWin32HandleInfo*, HANDLE*);

}  // namespace

VulkanExport::~VulkanExport() {
    if (state_.device != nullptr) {
        if (state_.device_wait != nullptr) {
            state_.device_wait(state_.device);
        }
        if (memory_ != VK_NULL_HANDLE) {
            state_.free_memory(state_.device, memory_, nullptr);
        }
        if (buffer_ != VK_NULL_HANDLE) {
            state_.destroy_buffer(state_.device, buffer_, nullptr);
        }
    }
}

bool VulkanExport::create(const LUID& luid, std::string& error) {
    if (!state_.create(luid, error)) {
        return false;
    }
    const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(state_.get_instance_proc(state_.instance, "vkGetPhysicalDeviceProperties2"));
    if (properties2 == nullptr) {
        error = "vkGetPhysicalDeviceProperties2 does not resolve";
        return false;
    }
    VkPhysicalDeviceIDProperties id{};
    id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &id;
    properties2(state_.physical, &properties);
    std::memcpy(uuid_, id.deviceUUID, VK_UUID_SIZE);
    return true;
}

bool VulkanExport::exportable(std::string& detail) const {
    if (!state_.external_win32) {
        detail = "VK_KHR_external_memory_win32 is not exposed by the device";
        return false;
    }
    const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
        state_.get_instance_proc(state_.instance, "vkGetPhysicalDeviceExternalBufferProperties"));
    if (query == nullptr) {
        detail = "vkGetPhysicalDeviceExternalBufferProperties does not resolve";
        return false;
    }
    VkPhysicalDeviceExternalBufferInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.handleType = kOpaqueWin32;
    VkExternalBufferProperties properties{};
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    query(state_.physical, &info, &properties);
    if ((properties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
        detail = "OPAQUE_WIN32 buffer memory is not exportable";
        return false;
    }
    return true;
}

bool VulkanExport::export_memory(VkDeviceSize size, HANDLE& handle, std::string& error) {
    VkExternalMemoryBufferCreateInfo external{};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    external.handleTypes = kOpaqueWin32;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext = &external;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = state_.create_buffer(state_.device, &info, nullptr, &buffer_);
    if (result != VK_SUCCESS) {
        error = "vkCreateBuffer failed: " + std::to_string(result);
        return false;
    }
    VkMemoryRequirements requirements{};
    state_.buffer_requirements(state_.device, buffer_, &requirements);
    const int type = state_.find_memory_type(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) {
        error = "no memory type for the exportable buffer";
        return false;
    }
    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = kOpaqueWin32;
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.pNext = &export_info;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
    result = state_.allocate_memory(state_.device, &allocate, nullptr, &memory_);
    if (result != VK_SUCCESS) {
        error = "vkAllocateMemory with an export request failed: " + std::to_string(result);
        return false;
    }
    const auto get_handle = reinterpret_cast<GetMemoryWin32Handle>(state_.get_device_proc(state_.device, "vkGetMemoryWin32HandleKHR"));
    if (get_handle == nullptr) {
        error = "vkGetMemoryWin32HandleKHR does not resolve";
        return false;
    }
    GetWin32HandleInfo get{kGetWin32HandleType, nullptr, memory_, kOpaqueWin32};
    result = get_handle(state_.device, &get, &handle);
    if (result != VK_SUCCESS || handle == nullptr) {
        error = "vkGetMemoryWin32HandleKHR failed: " + std::to_string(result);
        return false;
    }
    return true;
}

}  // namespace lsw::suites
