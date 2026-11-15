#pragma once

#include "render/vk/api.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::render::vk {
class Presenter;
}

namespace lsw::render::platform {
struct VkDeviceIdentity {
    std::uint32_t index = 0;
    std::uint8_t uuid[16] = {};
    std::uint8_t luid[8] = {};
    bool luid_valid = false;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::string name;
};

struct VkPlatformConfig {
    std::string selector;
};

class VkPlatform {
public:
    virtual ~VkPlatform() = default;
    [[nodiscard]] virtual PFN_vkGetInstanceProcAddr loader(std::string& error) = 0;
    [[nodiscard]] virtual std::vector<std::string> instance_extensions(bool present) = 0;
    [[nodiscard]] virtual std::vector<std::string> device_extensions(bool present) = 0;
    [[nodiscard]] virtual bool accepts(const VkDeviceIdentity& identity) = 0;
    [[nodiscard]] virtual std::unique_ptr<vk::Presenter> make_presenter(std::string& error) = 0;
};

[[nodiscard]] std::unique_ptr<VkPlatform> make_vk_platform(const VkPlatformConfig& config, std::string& error);
}  // namespace lsw::render::platform
