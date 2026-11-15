#include "support/vulkan_table.hpp"

namespace lsw::suites {

bool VulkanTable::load_global(PFN_vkGetInstanceProcAddr loader_entry) {
    vkGetInstanceProcAddr = loader_entry;
    if (vkGetInstanceProcAddr == nullptr) {
        return false;
    }
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    vkEnumerateInstanceVersion =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
    vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
    return vkCreateInstance != nullptr && vkEnumerateInstanceExtensionProperties != nullptr &&
           vkEnumerateInstanceLayerProperties != nullptr;
}

void VulkanTable::load_instance(VkInstance instance) {
#define LSW_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
#define LSW_VK_LOAD_ALIAS(name, alias)                                                           \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));                 \
    if (name == nullptr) {                                                                       \
        name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #alias));            \
    }
    LSW_VK_INSTANCE_FUNCTIONS(LSW_VK_LOAD)
    LSW_VK_INSTANCE_ALIASED(LSW_VK_LOAD_ALIAS)
    LSW_VK_INSTANCE_EXTENSION(LSW_VK_LOAD)
#undef LSW_VK_LOAD
#undef LSW_VK_LOAD_ALIAS
    vkCreateWin32SurfaceKHR =
        reinterpret_cast<vkw::CreateWin32Surface>(vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    vkGetPhysicalDeviceWin32PresentationSupportKHR = reinterpret_cast<vkw::GetPresentationSupport>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
}

void VulkanTable::load_device(VkDevice device) {
#define LSW_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
#define LSW_VK_LOAD_ALIAS(name, alias)                                                           \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                     \
    if (name == nullptr) {                                                                       \
        name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #alias));                \
    }
    LSW_VK_DEVICE_FUNCTIONS(LSW_VK_LOAD)
    LSW_VK_DEVICE_ALIASED(LSW_VK_LOAD_ALIAS)
    LSW_VK_DEVICE_EXTENSION(LSW_VK_LOAD)
#undef LSW_VK_LOAD
#undef LSW_VK_LOAD_ALIAS
    vkGetMemoryWin32HandleKHR =
        reinterpret_cast<vkw::GetMemoryHandle>(vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
    vkGetMemoryWin32HandlePropertiesKHR = reinterpret_cast<vkw::GetMemoryHandleProperties>(vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR"));
    vkGetSemaphoreWin32HandleKHR =
        reinterpret_cast<vkw::GetSemaphoreHandle>(vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
    vkImportSemaphoreWin32HandleKHR =
        reinterpret_cast<vkw::ImportSemaphoreHandle>(vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR"));
    vkGetFenceWin32HandleKHR =
        reinterpret_cast<vkw::GetFenceHandle>(vkGetDeviceProcAddr(device, "vkGetFenceWin32HandleKHR"));
    vkImportFenceWin32HandleKHR =
        reinterpret_cast<vkw::ImportFenceHandle>(vkGetDeviceProcAddr(device, "vkImportFenceWin32HandleKHR"));
}

}  // namespace lsw::suites
