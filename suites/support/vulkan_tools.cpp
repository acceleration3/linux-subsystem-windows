#include "support/vulkan_tools.hpp"

#include <cstring>

namespace lsw::suites {

std::string VulkanTools::result_name(VkResult result) {
    switch (result) {
#define LSW_VK_RESULT(value) \
    case value:              \
        return #value;
        LSW_VK_RESULT(VK_SUCCESS)
        LSW_VK_RESULT(VK_NOT_READY)
        LSW_VK_RESULT(VK_TIMEOUT)
        LSW_VK_RESULT(VK_EVENT_SET)
        LSW_VK_RESULT(VK_EVENT_RESET)
        LSW_VK_RESULT(VK_INCOMPLETE)
        LSW_VK_RESULT(VK_ERROR_OUT_OF_HOST_MEMORY)
        LSW_VK_RESULT(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        LSW_VK_RESULT(VK_ERROR_INITIALIZATION_FAILED)
        LSW_VK_RESULT(VK_ERROR_DEVICE_LOST)
        LSW_VK_RESULT(VK_ERROR_MEMORY_MAP_FAILED)
        LSW_VK_RESULT(VK_ERROR_LAYER_NOT_PRESENT)
        LSW_VK_RESULT(VK_ERROR_EXTENSION_NOT_PRESENT)
        LSW_VK_RESULT(VK_ERROR_FEATURE_NOT_PRESENT)
        LSW_VK_RESULT(VK_ERROR_INCOMPATIBLE_DRIVER)
        LSW_VK_RESULT(VK_ERROR_TOO_MANY_OBJECTS)
        LSW_VK_RESULT(VK_ERROR_FORMAT_NOT_SUPPORTED)
        LSW_VK_RESULT(VK_ERROR_FRAGMENTED_POOL)
        LSW_VK_RESULT(VK_ERROR_UNKNOWN)
        LSW_VK_RESULT(VK_ERROR_OUT_OF_POOL_MEMORY)
        LSW_VK_RESULT(VK_ERROR_INVALID_EXTERNAL_HANDLE)
        LSW_VK_RESULT(VK_ERROR_SURFACE_LOST_KHR)
        LSW_VK_RESULT(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
        LSW_VK_RESULT(VK_SUBOPTIMAL_KHR)
        LSW_VK_RESULT(VK_ERROR_OUT_OF_DATE_KHR)
#undef LSW_VK_RESULT
        default:
            return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

bool VulkanTools::code_in(VkResult got, std::initializer_list<VkResult> accepted) {
    for (const VkResult code : accepted) {
        if (code == got) {
            return true;
        }
    }
    return false;
}

std::string VulkanTools::accepted_text(std::initializer_list<VkResult> accepted) {
    std::string text;
    for (const VkResult code : accepted) {
        text += (text.empty() ? "" : " or ") + result_name(code);
    }
    return text;
}

std::string VulkanTools::bytes_text(std::uint64_t bytes) {
    if (bytes >= (1ull << 30) && bytes % (1ull << 20) == 0) {
        return std::to_string(bytes >> 20) + " MiB";
    }
    if (bytes >= (1ull << 20) && bytes % (1ull << 10) == 0) {
        return std::to_string(bytes >> 10) + " KiB";
    }
    return std::to_string(bytes) + " bytes";
}

std::string VulkanTools::memory_flags_text(VkMemoryPropertyFlags flags) {
    std::string text;
    const auto add = [&](VkMemoryPropertyFlags bit, const char* name) {
        if ((flags & bit) != 0) {
            text += (text.empty() ? "" : "|") + std::string(name);
        }
    };
    add(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "DEVICE_LOCAL");
    add(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "HOST_VISIBLE");
    add(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "HOST_COHERENT");
    add(VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "HOST_CACHED");
    add(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "LAZILY_ALLOCATED");
    add(VK_MEMORY_PROPERTY_PROTECTED_BIT, "PROTECTED");
    return text.empty() ? "none" : text;
}

std::string VulkanTools::version_text(std::uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

std::string VulkanTools::format_name(VkFormat format) {
    switch (format) {
#define LSW_VK_FORMAT(value) \
    case value:              \
        return #value;
        LSW_VK_FORMAT(VK_FORMAT_R8_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_R8G8_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_R8G8B8A8_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_B8G8R8A8_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_R8G8B8A8_SRGB)
        LSW_VK_FORMAT(VK_FORMAT_R8_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R16_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R16G16_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R16G16B16A16_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R16_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_R16G16B16A16_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_R32_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R32G32_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R32G32B32A32_UINT)
        LSW_VK_FORMAT(VK_FORMAT_R32_SFLOAT)
        LSW_VK_FORMAT(VK_FORMAT_R16G16B16A16_SFLOAT)
        LSW_VK_FORMAT(VK_FORMAT_R32G32B32A32_SFLOAT)
        LSW_VK_FORMAT(VK_FORMAT_A2B10G10R10_UNORM_PACK32)
        LSW_VK_FORMAT(VK_FORMAT_D16_UNORM)
        LSW_VK_FORMAT(VK_FORMAT_D32_SFLOAT)
        LSW_VK_FORMAT(VK_FORMAT_S8_UINT)
        LSW_VK_FORMAT(VK_FORMAT_D24_UNORM_S8_UINT)
        LSW_VK_FORMAT(VK_FORMAT_D32_SFLOAT_S8_UINT)
        LSW_VK_FORMAT(VK_FORMAT_X8_D24_UNORM_PACK32)
#undef LSW_VK_FORMAT
        default:
            return "VkFormat(" + std::to_string(static_cast<int>(format)) + ")";
    }
}

std::uint32_t VulkanTools::texel_size(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_S8_UINT:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_D16_UNORM:
            return 2;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_UINT:
            return 8;
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        default:
            return 0;
    }
}

bool VulkanTools::is_depth_stencil(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_X8_D24_UNORM_PACK32;
}

bool VulkanTools::has_extension(const char* const* names, std::uint32_t count, const char* wanted) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (std::strcmp(names[i], wanted) == 0) {
            return true;
        }
    }
    return false;
}

std::uint64_t VulkanTools::align_up(std::uint64_t value, std::uint64_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

}  // namespace lsw::suites
