#pragma once

#include "render/common/image.hpp"
#include "render/platform/vk_platform.hpp"
#include "render/vk/api.hpp"
#include "render/vk/presenter.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::render::vk {


struct KitOptions {
    bool present = false;
    std::uint32_t api_version = VK_API_VERSION_1_1;
    std::vector<std::string> device_extensions;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t layers = 1;
    std::uint32_t mips = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct TextureDesc {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t layers = 1;
    std::uint32_t mips = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
};

struct PipelineDesc {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygon = VK_POLYGON_MODE_FILL;
    bool depth_test = false;
    bool depth_write = true;
    VkCompareOp depth_op = VK_COMPARE_OP_LESS;
    bool stencil_test = false;
    VkStencilOpState stencil{};
    bool blend = false;
    VkBlendFactor src_color = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendFactor src_alpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp color_op = VK_BLEND_OP_ADD;
    VkColorComponentFlags write_mask = 0xf;
    std::uint32_t color_attachments = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    std::uint32_t subpass = 0;
};

struct DescriptorWrite {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorBufferInfo buffer{};
    VkDescriptorImageInfo image{};
};

class Kit {
public:
    Kit() = default;
    Kit(const Kit&) = delete;
    Kit& operator=(const Kit&) = delete;
    ~Kit();

    [[nodiscard]] bool open(platform::VkPlatform& platform, const KitOptions& options, std::string& error);
    void close();

    [[nodiscard]] bool check(VkResult result, const char* what, std::string& error) const;

    [[nodiscard]] bool format_supports(VkFormat format, VkFormatFeatureFlags optimal) const;
    [[nodiscard]] VkFormat first_supported(const std::vector<VkFormat>& candidates, VkFormatFeatureFlags optimal) const;
    [[nodiscard]] std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const;

    [[nodiscard]] VkShaderModule shader(const std::string& name, std::string& error);

    [[nodiscard]] bool make_buffer(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible,
                                   std::string& error);
    [[nodiscard]] bool make_buffer_with(Buffer& out, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                        std::string& error);
    [[nodiscard]] bool write(Buffer& buffer, VkDeviceSize offset, const void* data, VkDeviceSize size, std::string& error);

    [[nodiscard]] bool make_texture(Texture& out, const TextureDesc& desc, std::string& error);
    [[nodiscard]] bool make_sampled_texture(Texture& out, std::uint32_t width, std::uint32_t height, VkFormat format,
                                            const std::vector<std::vector<std::uint8_t>>& levels, std::string& error);

    [[nodiscard]] bool make_sampler(VkSampler& out, VkFilter mag, VkFilter min, VkSamplerMipmapMode mip,
                                    VkSamplerAddressMode address, float max_lod, std::string& error, float anisotropy = 0.0f);
    [[nodiscard]] bool make_layout(VkDescriptorSetLayout& out, const std::vector<VkDescriptorSetLayoutBinding>& bindings,
                                   std::string& error);
    [[nodiscard]] bool make_pipeline_layout(VkPipelineLayout& out, const std::vector<VkDescriptorSetLayout>& sets,
                                            const std::vector<VkPushConstantRange>& push, std::string& error);
    [[nodiscard]] bool make_descriptor_set(VkDescriptorSet& out, VkDescriptorSetLayout layout,
                                           const std::vector<DescriptorWrite>& writes, std::string& error);
    [[nodiscard]] bool make_pipeline(VkPipeline& out, const PipelineDesc& desc, std::string& error);
    [[nodiscard]] bool make_compute_pipeline(VkPipeline& out, VkShaderModule module, VkPipelineLayout layout, std::string& error);
    [[nodiscard]] bool make_semaphore(VkSemaphore& out, std::string& error);

    [[nodiscard]] bool submit(const std::function<void(VkCommandBuffer)>& record, std::string& error);
    [[nodiscard]] bool submit_sync(const std::function<void(VkCommandBuffer)>& record, VkSemaphore wait,
                                   VkPipelineStageFlags wait_stage, VkSemaphore signal, std::string& error);

    void barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                 std::uint32_t mips = 1, std::uint32_t layers = 1) const;

    [[nodiscard]] bool read_texture(const Texture& texture, VkImageLayout layout, VkImageAspectFlags aspect,
                                    std::uint32_t layer, Image& out, std::string& error);
    [[nodiscard]] bool read_buffer(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, std::vector<std::uint8_t>& out,
                                   std::string& error);

    [[nodiscard]] static PixelKind pixel_kind(VkFormat format, VkImageAspectFlags aspect);
    [[nodiscard]] static std::uint32_t texel_bytes(VkFormat format, VkImageAspectFlags aspect);

    Api api;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory{};
    platform::VkDeviceIdentity identity;
    std::string description;
    std::unique_ptr<Presenter> presenter;
    bool present_enabled = false;
    platform::VkPlatform* platform = nullptr;

    void defer(std::function<void()> destroyer) { cleanups_.push_back(std::move(destroyer)); }

private:
    std::vector<std::function<void()>> cleanups_;
    VkFence fence_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
};
}  // namespace lsw::render::vk
