#pragma once

#include "render/vk/api.hpp"

#include <cstdint>
#include <string>

namespace lsw::render::vk {
class Kit;

class Presenter {
public:
    virtual ~Presenter() = default;
    [[nodiscard]] virtual bool open(Kit& kit, std::uint32_t width, std::uint32_t height, std::string& error) = 0;
    [[nodiscard]] virtual VkFormat format() const = 0;
    [[nodiscard]] virtual std::uint32_t image_count() const = 0;
    [[nodiscard]] virtual VkImage image(std::uint32_t index) const = 0;
    [[nodiscard]] virtual VkImageLayout present_layout() const = 0;
    [[nodiscard]] virtual bool acquire(Kit& kit, std::uint32_t& index, VkSemaphore signal, std::string& error) = 0;
    [[nodiscard]] virtual bool present(Kit& kit, std::uint32_t index, VkSemaphore wait, std::string& error) = 0;
    virtual void close(Kit& kit) = 0;
};
}  // namespace lsw::render::vk
