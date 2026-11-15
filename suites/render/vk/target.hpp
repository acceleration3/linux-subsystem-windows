#pragma once

#include "render/vk/kit.hpp"

#include <vector>

namespace lsw::render::vk {
struct TargetDesc {
    std::uint32_t width = 128;
    std::uint32_t height = 128;
    std::vector<VkFormat> colors{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags color_usage = 0;
};

class Target {
public:
    [[nodiscard]] bool create(Kit& kit, const TargetDesc& desc, std::string& error);

    void begin(Kit& kit, VkCommandBuffer cb, bool clear) const;
    void end(Kit& kit, VkCommandBuffer cb) const;
    void set_viewport(Kit& kit, VkCommandBuffer cb) const;

    [[nodiscard]] bool read_color(Kit& kit, std::size_t index, Image& out, std::string& error) const;
    [[nodiscard]] bool read_depth(Kit& kit, Image& out, std::string& error) const;
    [[nodiscard]] bool read_stencil(Kit& kit, Image& out, std::string& error) const;

    [[nodiscard]] const Texture& color(std::size_t index) const { return resolved_.empty() ? color_[index] : resolved_[index]; }

    TargetDesc desc;
    VkRenderPass clear_pass = VK_NULL_HANDLE;
    VkRenderPass load_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    std::vector<Texture> color_;
    std::vector<Texture> resolved_;
    Texture depth_;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clear_depth = 1.0f;
    std::uint32_t clear_stencil = 0;

private:
    [[nodiscard]] bool make_pass(Kit& kit, bool clear, VkRenderPass& out, std::string& error) const;
};
}  // namespace lsw::render::vk
