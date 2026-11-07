#pragma once

#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites {
struct VulkanExtensionFeature {
    const char* extension;
    VkStructureType type;
};

class VulkanExtensionTable {
public:
    [[nodiscard]] static std::vector<std::string> instance_functions(const std::string& extension);

    [[nodiscard]] static std::vector<std::string> device_functions(const std::string& extension);

    [[nodiscard]] static std::vector<VulkanExtensionFeature> feature_structs();
};
}  // namespace lsw::suites
