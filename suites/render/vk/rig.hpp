#pragma once

#include "render/common/case.hpp"
#include "render/vk/kit.hpp"
#include "render/vk/target.hpp"
#include "render/vk/vk.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::render::vk {

struct CaseFailure {
    std::string what;
};

class Rig {
public:
    explicit Rig(Kit& kit) : kit(kit) {}

    [[noreturn]] void fail(const std::string& what) const { throw CaseFailure{what}; }
    void must(bool ok, const std::string& what) const {
        if (!ok) {
            throw CaseFailure{what};
        }
    }
    void vk_ok(VkResult result, const char* what) const {
        std::string error;
        if (!kit.check(result, what, error)) {
            throw CaseFailure{error};
        }
    }

    Target& make_target(const TargetDesc& desc);
    VkShaderModule sh(const char* name);
    Buffer buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    template <class T>
    Buffer buffer(const std::vector<T>& values, VkBufferUsageFlags usage) {
        return buffer(values.data(), values.size() * sizeof(T), usage);
    }
    Buffer blank(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible = true, bool address = false);
    Texture texture(const TextureDesc& desc);
    Texture sampled(std::uint32_t width, std::uint32_t height, VkFormat format, const std::vector<std::uint8_t>& rgba,
                    std::uint32_t mips = 1);
    VkSampler sampler(const VkSamplerCreateInfo& info);
    VkSampler sampler(VkFilter filter, VkSamplerAddressMode address, float max_lod = 0.0f,
                      VkSamplerMipmapMode mip = VK_SAMPLER_MIPMAP_MODE_NEAREST);
    VkDescriptorSetLayout dlayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayoutCreateFlags flags = 0,
                                  const void* next = nullptr);
    VkDescriptorSet dset(VkDescriptorSetLayout layout, const std::vector<DescriptorWrite>& writes);
    VkPipelineLayout playout(const std::vector<VkDescriptorSetLayout>& sets, const std::vector<VkPushConstantRange>& push = {});
    VkPipelineLayout playout(const std::vector<VkDescriptorSetLayout>& sets, std::uint32_t bytes, VkShaderStageFlags stages);
    VkPipeline pipe(const PipelineDesc& desc);
    VkPipeline compute(const char* shader, VkPipelineLayout layout);
    VkRenderPass render_pass(const VkRenderPassCreateInfo& info);
    VkFramebuffer framebuffer(VkRenderPass pass, const std::vector<VkImageView>& views, std::uint32_t width, std::uint32_t height,
                              std::uint32_t layers = 1);
    VkSemaphore semaphore();
    void run(const std::function<void(VkCommandBuffer)>& record);
    Image color(Target& target, std::size_t index = 0);
    Image read(const Texture& texture, VkImageLayout layout, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
               std::uint32_t layer = 0);
    std::vector<std::uint8_t> bytes(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size);

    void bind_vertex(VkCommandBuffer cb, const Buffer& buffer, VkDeviceSize offset = 0, std::uint32_t binding = 0) const;
    void bind_graphics(VkCommandBuffer cb, VkPipeline pipeline, VkPipelineLayout layout = VK_NULL_HANDLE,
                       const std::vector<VkDescriptorSet>& sets = {}) const;
    void push(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages, const void* data, std::uint32_t size,
              std::uint32_t offset = 0) const;
    void scissor(VkCommandBuffer cb, int x, int y, std::uint32_t w, std::uint32_t h) const;
    void viewport(VkCommandBuffer cb, float x, float y, float w, float h, float min_depth = 0.0f, float max_depth = 1.0f) const;
    void barrier_all(VkCommandBuffer cb) const;

    PipelineDesc pos2c(Target& target, VkPipelineLayout layout = VK_NULL_HANDLE);
    VkPipelineLayout empty_layout();
    Image draw(Target& target, VkPipeline pipeline, const Buffer* vertices, const std::function<void(VkCommandBuffer)>& commands,
               VkPipelineLayout layout = VK_NULL_HANDLE, const std::vector<VkDescriptorSet>& sets = {}, std::size_t capture = 0);

    Kit& kit;

private:
    VkPipelineLayout empty_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Target>> targets_;
};

struct QuadPush {
    float rect[4];
    float color[4];
};

struct Vertex2 {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

using CaseBody = std::function<void(Rig&, CaptureSet&)>;
using CaseCaps = std::function<std::string(Kit&)>;

class RigScene : public VkScene {
public:
    std::string unsupported(Kit& kit) final;
    bool setup(Kit& kit, std::string& error) final;
    bool frame(Kit& kit, unsigned index, std::string& error) final;
    bool capture(Kit& kit, unsigned index, CaptureSet& out, std::string& error) final;

protected:
    virtual std::string caps(Kit&) { return {}; }
    virtual void init(Rig&) {}
    virtual void step(Rig&, unsigned) {}
    virtual void grab(Rig& rig, unsigned frame, CaptureSet& out) = 0;

    std::unique_ptr<Rig> rig_;
};

class Cases {
public:
    static void add(CaseSpec spec, CaseBody body, CaseCaps caps = {});
    static void add_scene(CaseSpec spec, std::function<std::unique_ptr<VkScene>()> make);
    template <class SceneType>
    static void add_class(CaseSpec spec) {
        add_scene(std::move(spec), [] { return std::unique_ptr<VkScene>(new SceneType()); });
    }

    [[nodiscard]] static std::string need_format(Kit& kit, VkFormat format, VkFormatFeatureFlags features, const char* what);
    [[nodiscard]] static std::string need_buffer_format(Kit& kit, VkFormat format, VkFormatFeatureFlags features, const char* what);
    [[nodiscard]] static std::string need_extension(Kit& kit, const char* name);
    [[nodiscard]] static std::string first(const std::vector<std::string>& reasons);
    [[nodiscard]] static std::string need_version(Kit& kit, unsigned major, unsigned minor);
    [[nodiscard]] static VkFormat depth_stencil(Kit& kit);
    [[nodiscard]] static VkFormat depth_only(Kit& kit);
    [[nodiscard]] static std::uint32_t mip_count(std::uint32_t width, std::uint32_t height);
};

const float* palette(unsigned index);
Vertex2 vertex(float x, float y, unsigned color);

std::vector<Vertex2> make_disc(float cx, float cy, float radius, int segments, const float color[4]);
std::vector<std::uint8_t> checker_rgba(unsigned size, unsigned cell, std::uint32_t a, std::uint32_t b);
std::vector<std::uint8_t> gradient_rgba(unsigned width, unsigned height);

}  // namespace lsw::render::vk
