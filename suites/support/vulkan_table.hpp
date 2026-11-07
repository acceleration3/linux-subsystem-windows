#pragma once

#include "support/vulkan_functions.hpp"
#include "support/vulkan_win32.hpp"

namespace lsw::suites {
class VulkanTable {
public:
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;

#define LSW_VK_MEMBER(name) PFN_##name name = nullptr;
#define LSW_VK_ALIAS_MEMBER(name, alias) PFN_##name name = nullptr;
    LSW_VK_INSTANCE_FUNCTIONS(LSW_VK_MEMBER)
    LSW_VK_INSTANCE_ALIASED(LSW_VK_ALIAS_MEMBER)
    LSW_VK_INSTANCE_EXTENSION(LSW_VK_MEMBER)
    LSW_VK_DEVICE_FUNCTIONS(LSW_VK_MEMBER)
    LSW_VK_DEVICE_ALIASED(LSW_VK_ALIAS_MEMBER)
    LSW_VK_DEVICE_EXTENSION(LSW_VK_MEMBER)
#undef LSW_VK_MEMBER
#undef LSW_VK_ALIAS_MEMBER

    vkw::CreateWin32Surface vkCreateWin32SurfaceKHR = nullptr;
    vkw::GetPresentationSupport vkGetPhysicalDeviceWin32PresentationSupportKHR = nullptr;
    vkw::GetMemoryHandle vkGetMemoryWin32HandleKHR = nullptr;
    vkw::GetMemoryHandleProperties vkGetMemoryWin32HandlePropertiesKHR = nullptr;
    vkw::GetSemaphoreHandle vkGetSemaphoreWin32HandleKHR = nullptr;
    vkw::ImportSemaphoreHandle vkImportSemaphoreWin32HandleKHR = nullptr;
    vkw::GetFenceHandle vkGetFenceWin32HandleKHR = nullptr;
    vkw::ImportFenceHandle vkImportFenceWin32HandleKHR = nullptr;

    [[nodiscard]] bool load_global(PFN_vkGetInstanceProcAddr loader_entry);

    void load_instance(VkInstance instance);

    void load_device(VkDevice device);
};
}  // namespace lsw::suites
