#include "render/vk/api.hpp"

namespace lsw::render::vk {

namespace {
#define LSW_VK_MISSING(name) [[noreturn]] void VKAPI_CALL missing_##name() { throw MissingFunction{#name}; }
LSW_VK_LAZY_DEVICE_FUNCTIONS(LSW_VK_MISSING)
#undef LSW_VK_MISSING
}  // namespace

bool Api::load_global(PFN_vkGetInstanceProcAddr loader, std::string& error) {
    vkGetInstanceProcAddr = loader;
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(loader(nullptr, "vkCreateInstance"));
    vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        loader(nullptr, "vkEnumerateInstanceExtensionProperties"));
    vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(loader(nullptr, "vkEnumerateInstanceVersion"));
    if (vkCreateInstance == nullptr || vkEnumerateInstanceExtensionProperties == nullptr) {
        error = "the Vulkan loader does not export the global entry points";
        return false;
    }
    return true;
}

bool Api::load_instance(VkInstance instance, std::string& error) {
#define LSW_VK_LOAD(name)                                                                  \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));           \
    if (name == nullptr) {                                                                 \
        error = "missing Vulkan instance function " #name;                                 \
        return false;                                                                      \
    }
    LSW_VK_INSTANCE_FUNCTIONS(LSW_VK_LOAD)
#undef LSW_VK_LOAD
#define LSW_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    LSW_VK_OPTIONAL_INSTANCE_FUNCTIONS(LSW_VK_LOAD)
#undef LSW_VK_LOAD
    return true;
}

bool Api::load_device(VkDevice device, std::string& error) {
#define LSW_VK_LOAD(name)                                                              \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));           \
    if (name == nullptr) {                                                             \
        error = "missing Vulkan device function " #name;                               \
        return false;                                                                  \
    }
    LSW_VK_DEVICE_FUNCTIONS(LSW_VK_LOAD)
#undef LSW_VK_LOAD
#define LSW_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
    LSW_VK_OPTIONAL_DEVICE_FUNCTIONS(LSW_VK_LOAD)
#undef LSW_VK_LOAD
#define LSW_VK_LOAD(name)                                                              \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));           \
    if (name == nullptr) {                                                             \
        name = reinterpret_cast<PFN_##name>(&missing_##name);                          \
    }
    LSW_VK_LAZY_DEVICE_FUNCTIONS(LSW_VK_LOAD)
#undef LSW_VK_LOAD
    return true;
}

std::string Api::result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult " + std::to_string(static_cast<int>(result));
    }
}

}  // namespace lsw::render::vk
