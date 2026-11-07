#pragma once

#include "support/vulkan_buffer.hpp"
#include "support/vulkan_image.hpp"
#include "support/vulkan_shaders.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
enum class SceneKind { Rect, Geometry, Tessellation, Line, Point, Vertex };

enum class SceneDraw { Direct, Indexed, Indirect, IndexedIndirect };

struct SceneRect {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    float color[4] = {1, 1, 1, 1};
    float depth = 0.5f;
};

struct SceneOptions {
    SceneKind kind = SceneKind::Rect;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t width = 32;
    std::uint32_t height = 32;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool depth = false;
    bool blend = false;
    bool second = false;
    bool dynamic = false;
    bool secondary = false;
    bool depth_clamp = false;
    VkCompareOp depth_op = VK_COMPARE_OP_LESS;
    float line_width = 1.0f;
    VkPipelineCache cache = VK_NULL_HANDLE;
    SceneDraw draw = SceneDraw::Direct;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkBuffer indirect_buffer = VK_NULL_HANDLE;
};

struct SceneResult {
    std::vector<std::uint8_t> color;
    std::vector<std::uint8_t> second;
    std::vector<float> depth;
};

class VulkanScene {
public:
    [[nodiscard]] VkResult create(VulkanRig& rig, const SceneOptions& options);

    [[nodiscard]] VkResult render(const std::vector<SceneRect>& rects, const float clear_color[4], float clear_depth,
                                  SceneResult& result);

    [[nodiscard]] std::vector<std::uint8_t> expected_color(const std::vector<SceneRect>& rects, const float clear_color[4],
                                                           float clear_depth, bool second_attachment) const;

    [[nodiscard]] std::vector<float> expected_depth(const std::vector<SceneRect>& rects, float clear_depth) const;

    [[nodiscard]] static std::uint8_t unorm8(float value);

    [[nodiscard]] bool bgra() const { return options_.format == VK_FORMAT_B8G8R8A8_UNORM; }

    [[nodiscard]] const SceneOptions& options() const { return options_; }

    [[nodiscard]] VkFormat depth_format() const { return depth_format_; }

private:
    [[nodiscard]] VkResult build_pipeline();

    void record(VkCommandBuffer commands, const std::vector<SceneRect>& rects, const float clear_color[4], float clear_depth);

    void draw_all(VkCommandBuffer commands, const std::vector<SceneRect>& rects);

    [[nodiscard]] std::uint32_t viewport_width() const { return options_.viewport_width != 0 ? options_.viewport_width : options_.width; }

    [[nodiscard]] std::uint32_t viewport_height() const { return options_.viewport_height != 0 ? options_.viewport_height : options_.height; }

    VulkanRig* rig_ = nullptr;
    SceneOptions options_;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkShaderStageFlags push_stages_ = 0;
    VulkanImage color_;
    VulkanImage resolve_;
    VulkanImage second_;
    VulkanImage second_resolve_;
    VulkanImage depth_;
    VulkanOwned<VkImageView> color_view_;
    VulkanOwned<VkImageView> resolve_view_;
    VulkanOwned<VkImageView> second_view_;
    VulkanOwned<VkImageView> second_resolve_view_;
    VulkanOwned<VkImageView> depth_view_;
    VulkanOwned<VkRenderPass> pass_;
    VulkanOwned<VkFramebuffer> framebuffer_;
    VulkanOwned<VkPipelineLayout> layout_;
    VulkanOwned<VkPipeline> pipeline_;
    VulkanOwned<VkShaderModule> modules_[5];
    VulkanBuffer readback_color_;
    VulkanBuffer readback_second_;
    VulkanBuffer readback_depth_;
};
}  // namespace lsw::suites
