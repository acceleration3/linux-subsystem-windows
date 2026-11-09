#include "render/vk/rig.hpp"

#include "render/common/pattern.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

Target& Rig::make_target(const TargetDesc& desc) {
    auto target = std::make_unique<Target>();
    std::string error;
    if (!target->create(kit, desc, error)) {
        fail(error);
    }
    targets_.push_back(std::move(target));
    return *targets_.back();
}

VkShaderModule Rig::sh(const char* name) {
    std::string error;
    const VkShaderModule module = kit.shader(name, error);
    must(module != VK_NULL_HANDLE, error);
    return module;
}

Buffer Rig::buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer out;
    std::string error;
    must(kit.make_buffer_with(out, data, size, usage, error), error);
    return out;
}

Buffer Rig::blank(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool address) {
    Buffer out;
    std::string error;
    must(kit.make_buffer_ex(out, size, usage, host_visible, address, error), error);
    return out;
}

Texture Rig::texture(const TextureDesc& desc) {
    Texture out;
    std::string error;
    must(kit.make_texture(out, desc, error), error);
    return out;
}

Texture Rig::sampled(std::uint32_t width, std::uint32_t height, VkFormat format, const std::vector<std::uint8_t>& rgba,
                     std::uint32_t mips) {
    Texture out;
    std::string error;
    std::vector<std::vector<std::uint8_t>> levels;
    if (mips <= 1) {
        levels.push_back(rgba);
    } else {
        levels = Pattern::mip_chain(rgba, width, height, mips);
    }
    must(kit.make_sampled_texture(out, width, height, format, levels, error), error);
    return out;
}

VkSampler Rig::sampler(const VkSamplerCreateInfo& info) {
    VkSampler out = VK_NULL_HANDLE;
    vk_ok(kit.api.vkCreateSampler(kit.device, &info, nullptr, &out), "vkCreateSampler");
    Kit* k = &kit;
    kit.defer([k, out] { k->api.vkDestroySampler(k->device, out, nullptr); });
    return out;
}

VkSampler Rig::sampler(VkFilter filter, VkSamplerAddressMode address, float max_lod, VkSamplerMipmapMode mip) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mip;
    info.addressModeU = info.addressModeV = info.addressModeW = address;
    info.maxLod = max_lod;
    info.maxAnisotropy = 1.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    return sampler(info);
}

VkDescriptorSetLayout Rig::dlayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayoutCreateFlags flags,
                                   const void* next) {
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = next;
    info.flags = flags;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    VkDescriptorSetLayout out = VK_NULL_HANDLE;
    vk_ok(kit.api.vkCreateDescriptorSetLayout(kit.device, &info, nullptr, &out), "vkCreateDescriptorSetLayout");
    Kit* k = &kit;
    kit.defer([k, out] { k->api.vkDestroyDescriptorSetLayout(k->device, out, nullptr); });
    return out;
}

VkDescriptorSet Rig::dset(VkDescriptorSetLayout layout, const std::vector<DescriptorWrite>& writes) {
    VkDescriptorSet out = VK_NULL_HANDLE;
    std::string error;
    must(kit.make_descriptor_set(out, layout, writes, error), error);
    return out;
}

VkPipelineLayout Rig::playout(const std::vector<VkDescriptorSetLayout>& sets, const std::vector<VkPushConstantRange>& push) {
    VkPipelineLayout out = VK_NULL_HANDLE;
    std::string error;
    must(kit.make_pipeline_layout(out, sets, push, error), error);
    return out;
}

VkPipelineLayout Rig::playout(const std::vector<VkDescriptorSetLayout>& sets, std::uint32_t bytes, VkShaderStageFlags stages) {
    std::vector<VkPushConstantRange> push;
    if (bytes != 0) {
        push.push_back({stages, 0, bytes});
    }
    return playout(sets, push);
}

VkPipeline Rig::pipe(const PipelineDesc& desc) {
    VkPipeline out = VK_NULL_HANDLE;
    std::string error;
    must(kit.make_pipeline(out, desc, error), error);
    return out;
}

VkPipeline Rig::compute(const char* shader, VkPipelineLayout layout) {
    VkPipeline out = VK_NULL_HANDLE;
    std::string error;
    must(kit.make_compute_pipeline(out, sh(shader), layout, error), error);
    return out;
}

VkRenderPass Rig::render_pass(const VkRenderPassCreateInfo& info) {
    VkRenderPass out = VK_NULL_HANDLE;
    vk_ok(kit.api.vkCreateRenderPass(kit.device, &info, nullptr, &out), "vkCreateRenderPass");
    Kit* k = &kit;
    kit.defer([k, out] { k->api.vkDestroyRenderPass(k->device, out, nullptr); });
    return out;
}

VkFramebuffer Rig::framebuffer(VkRenderPass pass, const std::vector<VkImageView>& views, std::uint32_t width, std::uint32_t height,
                               std::uint32_t layers) {
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = pass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = width;
    info.height = height;
    info.layers = layers;
    VkFramebuffer out = VK_NULL_HANDLE;
    vk_ok(kit.api.vkCreateFramebuffer(kit.device, &info, nullptr, &out), "vkCreateFramebuffer");
    Kit* k = &kit;
    kit.defer([k, out] { k->api.vkDestroyFramebuffer(k->device, out, nullptr); });
    return out;
}

VkSemaphore Rig::semaphore() {
    VkSemaphore out = VK_NULL_HANDLE;
    std::string error;
    must(kit.make_semaphore(out, error), error);
    return out;
}

void Rig::run(const std::function<void(VkCommandBuffer)>& record) {
    std::string error;
    must(kit.submit(record, error), error);
}

Image Rig::color(Target& target, std::size_t index) {
    Image out;
    std::string error;
    must(target.read_color(kit, index, out, error), error);
    return out;
}

Image Rig::read(const Texture& texture, VkImageLayout layout, VkImageAspectFlags aspect, std::uint32_t layer) {
    Image out;
    std::string error;
    must(kit.read_texture(texture, layout, aspect, layer, out, error), error);
    return out;
}

std::vector<std::uint8_t> Rig::bytes(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size) {
    std::vector<std::uint8_t> out;
    std::string error;
    must(kit.read_buffer(buffer, offset, size, out, error), error);
    return out;
}

void Rig::bind_vertex(VkCommandBuffer cb, const Buffer& buffer, VkDeviceSize offset, std::uint32_t binding) const {
    kit.api.vkCmdBindVertexBuffers(cb, binding, 1, &buffer.buffer, &offset);
}

void Rig::bind_graphics(VkCommandBuffer cb, VkPipeline pipeline, VkPipelineLayout layout, const std::vector<VkDescriptorSet>& sets) const {
    kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (!sets.empty()) {
        kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, static_cast<std::uint32_t>(sets.size()), sets.data(),
                                        0, nullptr);
    }
}

void Rig::push(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages, const void* data, std::uint32_t size,
               std::uint32_t offset) const {
    kit.api.vkCmdPushConstants(cb, layout, stages, offset, size, data);
}

void Rig::scissor(VkCommandBuffer cb, int x, int y, std::uint32_t w, std::uint32_t h) const {
    const VkRect2D rect{{x, y}, {w, h}};
    kit.api.vkCmdSetScissor(cb, 0, 1, &rect);
}

void Rig::viewport(VkCommandBuffer cb, float x, float y, float w, float h, float min_depth, float max_depth) const {
    const VkViewport v{x, y, w, h, min_depth, max_depth};
    kit.api.vkCmdSetViewport(cb, 0, 1, &v);
}

void Rig::barrier_all(VkCommandBuffer cb) const {
    VkMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0,
                                 nullptr);
}

VkPipelineLayout Rig::empty_layout() {
    if (empty_ == VK_NULL_HANDLE) {
        empty_ = playout({});
    }
    return empty_;
}

PipelineDesc Rig::pos2c(Target& target, VkPipelineLayout layout) {
    PipelineDesc p;
    p.vertex = sh("pos2c.vert");
    p.fragment = sh("col4.frag");
    p.bindings = {{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX}};
    p.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8}};
    p.layout = layout != VK_NULL_HANDLE ? layout : empty_layout();
    p.pass = target.clear_pass;
    p.color_attachments = static_cast<std::uint32_t>(target.desc.colors.size());
    p.samples = target.desc.samples;
    return p;
}

Image Rig::draw(Target& target, VkPipeline pipeline, const Buffer* vertices, const std::function<void(VkCommandBuffer)>& commands,
                VkPipelineLayout layout, const std::vector<VkDescriptorSet>& sets, std::size_t capture) {
    run([&](VkCommandBuffer cb) {
        target.begin(kit, cb, true);
        bind_graphics(cb, pipeline, layout, sets);
        if (vertices != nullptr) {
            bind_vertex(cb, *vertices);
        }
        commands(cb);
        target.end(kit, cb);
    });
    return color(target, capture);
}

std::string RigScene::unsupported(Kit& kit) { return caps(kit); }

bool RigScene::setup(Kit& kit, std::string& error) {
    rig_ = std::make_unique<Rig>(kit);
    try {
        init(*rig_);
    } catch (const CaseFailure& failure) {
        error = failure.what;
        return false;
    } catch (const MissingFunction& missing) {
        error = std::string("the Vulkan device does not export ") + missing.name;
        return false;
    }
    return true;
}

bool RigScene::frame(Kit&, unsigned index, std::string& error) {
    try {
        step(*rig_, index);
    } catch (const CaseFailure& failure) {
        error = failure.what;
        return false;
    } catch (const MissingFunction& missing) {
        error = std::string("the Vulkan device does not export ") + missing.name;
        return false;
    }
    return true;
}

bool RigScene::capture(Kit&, unsigned index, CaptureSet& out, std::string& error) {
    try {
        grab(*rig_, index, out);
    } catch (const CaseFailure& failure) {
        error = failure.what;
        return false;
    } catch (const MissingFunction& missing) {
        error = std::string("the Vulkan device does not export ") + missing.name;
        return false;
    }
    return true;
}

namespace {
class BodyScene : public RigScene {
public:
    BodyScene(CaseBody body, CaseCaps caps) : body_(std::move(body)), caps_(std::move(caps)) {}

protected:
    std::string caps(Kit& kit) override { return caps_ ? caps_(kit) : std::string(); }
    void grab(Rig& rig, unsigned, CaptureSet& out) override { body_(rig, out); }

private:
    CaseBody body_;
    CaseCaps caps_;
};
}  // namespace

void Cases::add(CaseSpec spec, CaseBody body, CaseCaps caps) {
    add_scene(std::move(spec), [body = std::move(body), caps = std::move(caps)]() -> std::unique_ptr<VkScene> {
        return std::make_unique<BodyScene>(body, caps);
    });
}

void Cases::add_scene(CaseSpec spec, std::function<std::unique_ptr<VkScene>()> make) {
    spec.source_hash(LSW_RENDER_SOURCE_HASH);
    CaseTable<Kit>::instance().add(std::move(spec), std::move(make));
}

std::string Cases::need_format(Kit& kit, VkFormat format, VkFormatFeatureFlags features, const char* what) {
    if (!kit.format_supports(format, features)) {
        return std::string(what) + " unsupported";
    }
    return {};
}

std::string Cases::need_buffer_format(Kit& kit, VkFormat format, VkFormatFeatureFlags features, const char* what) {
    VkFormatProperties props{};
    kit.api.vkGetPhysicalDeviceFormatProperties(kit.physical, format, &props);
    if ((props.bufferFeatures & features) != features) {
        return std::string(what) + " unsupported";
    }
    return {};
}

const float* palette(unsigned index) {
    static const float colors[8][4] = {{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.3f, 1.0f}, {0.25f, 0.4f, 1.0f, 1.0f},
                                       {1.0f, 0.9f, 0.2f, 1.0f}, {1.0f, 0.3f, 0.9f, 1.0f}, {0.2f, 0.95f, 0.95f, 1.0f},
                                       {1.0f, 0.6f, 0.15f, 1.0f}, {0.85f, 0.85f, 0.9f, 1.0f}};
    return colors[index % 8];
}

Vertex2 vertex(float x, float y, unsigned color) {
    const float* c = palette(color);
    return {x, y, c[0], c[1], c[2], c[3]};
}

std::string Cases::need_extension(Kit& kit, const char* name) {
    if (!kit.has_device_extension(name)) {
        return std::string(name) + " unsupported";
    }
    return {};
}

std::string Cases::first(const std::vector<std::string>& reasons) {
    for (const std::string& reason : reasons) {
        if (!reason.empty()) {
            return reason;
        }
    }
    return {};
}

std::string Cases::need_version(Kit& kit, unsigned major, unsigned minor) {
    if (!kit.version_at_least(major, minor)) {
        return "Vulkan " + std::to_string(major) + "." + std::to_string(minor) + " unsupported";
    }
    return {};
}

VkFormat Cases::depth_stencil(Kit& kit) {
    return kit.first_supported({VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat Cases::depth_only(Kit& kit) {
    return kit.first_supported({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

std::uint32_t Cases::mip_count(std::uint32_t width, std::uint32_t height) { return Pattern::level_count(width, height); }

std::vector<Vertex2> make_disc(float cx, float cy, float radius, int segments, const float color[4]) {
    std::vector<Vertex2> out;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
        out.push_back({cx, cy, color[0], color[1], color[2], color[3]});
        out.push_back({cx + radius * std::cos(a0), cy + radius * std::sin(a0), color[0], color[1], color[2], color[3]});
        out.push_back({cx + radius * std::cos(a1), cy + radius * std::sin(a1), color[0], color[1], color[2], color[3]});
    }
    return out;
}

std::vector<std::uint8_t> checker_rgba(unsigned size, unsigned cell, std::uint32_t a, std::uint32_t b) {
    return Pattern::checker(size, size, cell, a, b);
}

std::vector<std::uint8_t> gradient_rgba(unsigned width, unsigned height) { return Pattern::gradient(width, height); }

}  // namespace lsw::render::vk
