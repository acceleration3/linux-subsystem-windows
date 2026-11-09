#include "render/common/pattern.hpp"
#include "render/vk/target.hpp"
#include "render/vk/vk.hpp"

namespace lsw::render::vk {

namespace {

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class MippedQuadBlend : public VkSingleScene {
public:
    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        constexpr unsigned kSize = 64;
        const unsigned levels = Pattern::level_count(kSize, kSize);
        const Pattern::Bytes base = Pattern::checker(kSize, kSize, 4, Pattern::pack(240, 240, 240), Pattern::pack(220, 40, 40));
        Texture texture;
        if (!kit.make_sampled_texture(texture, kSize, kSize, VK_FORMAT_R8G8B8A8_UNORM, Pattern::mip_chain(base, kSize, kSize, levels),
                                      error)) {
            return false;
        }
        VkSampler sampler = VK_NULL_HANDLE;
        if (!kit.make_sampler(sampler, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                              VK_SAMPLER_ADDRESS_MODE_REPEAT, static_cast<float>(levels), error)) {
            return false;
        }
        Target target;
        if (!target.create(kit, TargetDesc{}, error)) {
            return false;
        }
        target.clear_color[0] = 0.15f;
        target.clear_color[1] = 0.3f;
        target.clear_color[2] = 0.55f;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        DescriptorWrite write;
        write.binding = 0;
        write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.image = {sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if (!kit.make_layout(set_layout, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}, error) ||
            !kit.make_descriptor_set(set, set_layout, {write}, error)) {
            return false;
        }
        const VkPipelineLayout layout = VkCases::push_layout(kit, 16, VK_SHADER_STAGE_FRAGMENT_BIT, {set_layout}, error);
        PipelineDesc pipe;
        pipe.vertex = kit.shader("tex.vert", error);
        pipe.fragment = kit.shader("tex.frag", error);
        if (layout == VK_NULL_HANDLE || pipe.vertex == VK_NULL_HANDLE || pipe.fragment == VK_NULL_HANDLE) {
            return false;
        }
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.bindings = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}};
        pipe.blend = true;
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!kit.make_pipeline(pipeline, pipe, error)) {
            return false;
        }
        const QuadVertex quads[8] = {
            {-0.9f, -0.9f, 0.0f, 0.0f}, {0.5f, -0.9f, 1.0f, 0.0f}, {-0.9f, 0.5f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f, 1.0f},
            {-0.4f, -0.4f, 0.0f, 0.0f}, {0.9f, -0.4f, 6.0f, 0.0f}, {-0.4f, 0.9f, 0.0f, 6.0f}, {0.9f, 0.9f, 6.0f, 6.0f},
        };
        Buffer vertices;
        if (!kit.make_buffer_with(vertices, quads, sizeof quads, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, error)) {
            return false;
        }
        const float opaque[4] = {1, 1, 1, 1};
        const float ghost[4] = {1, 1, 1, 0.5f};
        const bool ok = kit.submit(
            [&](VkCommandBuffer cb) {
                target.begin(kit, cb, true);
                kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
                const VkDeviceSize offset = 0;
                kit.api.vkCmdBindVertexBuffers(cb, 0, 1, &vertices.buffer, &offset);
                kit.api.vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, opaque);
                kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
                kit.api.vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, ghost);
                kit.api.vkCmdDraw(cb, 4, 1, 4, 0);
                target.end(kit, cb);
            },
            error);
        Image color;
        if (!ok || !target.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const VkRegister<MippedQuadBlend> kMippedQuadBlend{CaseSpec("texture", "mipped_quad_blend").tolerance(Tolerance::blend(2))};

}  // namespace

}  // namespace lsw::render::vk
