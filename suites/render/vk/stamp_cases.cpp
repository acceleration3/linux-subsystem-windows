#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

struct Env {
    Rig* rig = nullptr;
    Target* target = nullptr;
    std::unique_ptr<QuadKit> quads;
    std::vector<VkPipeline> pipelines;
    std::vector<Buffer> buffers;
    std::vector<Texture> textures;
    std::vector<VkDescriptorSet> sets;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
};

struct StampCase {
    std::string name;
    unsigned frames = 5;
    bool depth = false;
    bool stencil_capture = false;
    bool depth_capture = false;
    std::function<void(Rig&, Env&)> setup;
    std::function<void(Rig&, Env&, unsigned, VkCommandBuffer)> draw;
    std::function<std::string(Kit&)> caps;
    std::function<void(Rig&, Env&, unsigned)> before;
};

class StampScene : public RigScene {
public:
    explicit StampScene(const StampCase* entry) : entry_(entry) {}

protected:
    std::string caps(Kit& kit) override { return entry_->caps ? entry_->caps(kit) : std::string(); }

    void init(Rig& rig) override {
        env_.rig = &rig;
        TargetDesc td;
        if (entry_->depth) {
            td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
        env_.target = &rig.make_target(td);
        std::copy(kClear, kClear + 4, env_.target->clear_color);
        env_.quads = std::make_unique<QuadKit>(rig);
        env_.layout = env_.quads->layout;
        entry_->setup(rig, env_);
    }

    void step(Rig& rig, unsigned frame) override {
        if (entry_->before) {
            entry_->before(rig, env_, frame);
        }
        rig.run([&](VkCommandBuffer cb) {
            env_.target->begin(rig.kit, cb, frame == 0);
            if (!env_.pipelines.empty()) {
                rig.bind_graphics(cb, env_.pipelines[0]);
            }
            entry_->draw(rig, env_, frame, cb);
            env_.target->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override {
        out.add("color", rig.color(*env_.target));
        std::string error;
        if (entry_->stencil_capture) {
            Image image;
            rig.must(env_.target->read_stencil(rig.kit, image, error), error);
            out.add("stencil", std::move(image));
        }
        if (entry_->depth_capture) {
            Image image;
            rig.must(env_.target->read_depth(rig.kit, image, error), error);
            out.add("depth", std::move(image));
        }
    }

private:
    const StampCase* entry_;
    Env env_;
};

std::vector<std::unique_ptr<StampCase>>& registry() {
    static std::vector<std::unique_ptr<StampCase>> cases;
    return cases;
}

void add_stamp(StampCase value, const char* feature = nullptr, const char* extension = nullptr, unsigned minor = 0) {
    registry().push_back(std::make_unique<StampCase>(std::move(value)));
    const StampCase* entry = registry().back().get();
    CaseSpec spec("stamp", entry->name);
    spec.size(128, 128).frames(entry->frames, {0, entry->frames / 2, entry->frames - 1}).tolerance(Tolerance::tight(1));
    if (feature != nullptr) {
        spec.feature(feature);
    }
    if (extension != nullptr) {
        spec.extension(extension);
    }
    if (minor != 0) {
        spec.min_version(1, minor);
    }
    Cases::add_scene(std::move(spec), [entry]() -> std::unique_ptr<VkScene> { return std::make_unique<StampScene>(entry); });
}

PipelineDesc base_desc(Rig& rig, Env& env) {
    PipelineDesc p = env.quads->desc(*env.target);
    p.depth_write = true;
    return p;
}

void register_stamps() {
    {
        StampCase c;
        c.name = "blend_constants_walk";
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = base_desc(rig, e);
            p.blend = true;
            p.src_color = VK_BLEND_FACTOR_CONSTANT_COLOR;
            p.dst_color = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            p.src_alpha = VK_BLEND_FACTOR_ONE;
            p.dst_alpha = VK_BLEND_FACTOR_ZERO;
            p.dynamic = {VK_DYNAMIC_STATE_BLEND_CONSTANTS};
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const float t = static_cast<float>(f) / 5.0f;
            const float k[4] = {0.2f + 0.7f * t, 0.9f - 0.7f * t, 0.5f, 1.0f};
            rig.kit.api.vkCmdSetBlendConstants(cb, k);
            e.quads->quad(cb, -0.9f + 0.15f * static_cast<float>(f), -0.9f + 0.1f * static_cast<float>(f), 1.2f, 1.2f, palette(f));
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "depth_bias_layers";
        c.depth = true;
        c.depth_capture = true;
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = base_desc(rig, e);
            p.depth_test = true;
            p.depth_bias = true;
            p.dynamic = {VK_DYNAMIC_STATE_DEPTH_BIAS};
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const float bias[5] = {0.0f, -200000.0f, 300000.0f, -500000.0f, -100000.0f};
            rig.kit.api.vkCmdSetDepthBias(cb, bias[f % 5], 0.0f, 0.0f);
            e.quads->quad(cb, -0.9f + 0.36f * static_cast<float>(f), -0.9f, 0.5f, 1.8f, palette(f), 0.5f);
            e.quads->quad(cb, -0.9f + 0.36f * static_cast<float>(f), -0.9f, 0.5f, 0.9f, palette(f + 3), 0.5f);
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "stencil_reference_accumulate";
        c.depth = true;
        c.stencil_capture = true;
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = base_desc(rig, e);
            p.stencil_test = true;
            p.stencil = VkStencilOpState{VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL, 0xff, 0xff, 0};
            p.dynamic = {VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const std::uint32_t refs[5] = {0x10, 0x30, 0x20, 0x50, 0x2a};
            rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, refs[f % 5]);
            rig.kit.api.vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, f % 2 == 0 ? 0xff : 0xf0);
            rig.kit.api.vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0xff);
            e.quads->quad(cb, -0.9f + 0.3f * static_cast<float>(f), -0.9f + 0.2f * static_cast<float>(f), 1.1f, 1.1f, palette(f));
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "scissor_and_viewport_walk";
        c.setup = [](Rig& rig, Env& e) { e.pipelines.push_back(rig.pipe(base_desc(rig, e))); };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const float size = 128.0f - 18.0f * static_cast<float>(f);
            rig.viewport(cb, 9.0f * static_cast<float>(f), 9.0f * static_cast<float>(f), size, size);
            rig.scissor(cb, static_cast<int>(10 * f), static_cast<int>(4 * f), 90, 100 - static_cast<std::uint32_t>(10 * f));
            e.quads->quad(cb, -1, -1, 2, 2, palette(f));
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "line_width_growth";
        c.setup = [](Rig& rig, Env& e) {
            const std::vector<Vertex2> v = {vertex(-0.9f, -0.1f, 0), vertex(0.9f, 0.1f, 1)};
            e.buffers.push_back(rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            PipelineDesc p = rig.pos2c(*e.target);
            p.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            p.dynamic = {VK_DYNAMIC_STATE_LINE_WIDTH};
            e.pipelines.push_back(rig.pipe(p));
        };
        c.caps = [](Kit& kit) -> std::string { return kit.properties.limits.lineWidthRange[1] < 8.0f ? "lineWidthRange" : std::string(); };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.kit.api.vkCmdSetLineWidth(cb, 1.0f + 1.5f * static_cast<float>(f));
            rig.bind_vertex(cb, e.buffers[0]);
            rig.viewport(cb, 0, 20.0f * static_cast<float>(f), 128, 128);
            rig.kit.api.vkCmdDraw(cb, 2, 1, 0, 0);
        };
        add_stamp(std::move(c), "wideLines");
    }
    {
        StampCase c;
        c.name = "cull_and_front_face_cycle";
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = base_desc(rig, e);
            p.dynamic = {VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const VkCullModeFlags culls[5] = {VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT};
            rig.kit.api.vkCmdSetCullMode(cb, culls[f % 5]);
            rig.kit.api.vkCmdSetFrontFace(cb, f % 2 == 0 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
            e.quads->quad(cb, -0.9f + 0.34f * static_cast<float>(f), -0.8f + 0.1f * static_cast<float>(f), 0.5f, 1.2f, palette(f));
        };
        add_stamp(std::move(c), nullptr, nullptr, 3);
    }
    {
        StampCase c;
        c.name = "polygon_mode_cycle";
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = rig.pos2c(*e.target);
            p.dynamic = {VK_DYNAMIC_STATE_POLYGON_MODE_EXT};
            std::vector<Vertex2> v;
            for (int i = 0; i < 5; ++i) {
                const float x = -0.8f + 0.34f * static_cast<float>(i);
                v.push_back(vertex(x, -0.7f, static_cast<unsigned>(i)));
                v.push_back(vertex(x + 0.3f, -0.7f + 0.15f * static_cast<float>(i), static_cast<unsigned>(i + 1)));
                v.push_back(vertex(x + 0.15f, 0.6f, static_cast<unsigned>(i + 2)));
            }
            e.buffers.push_back(rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            e.pipelines.push_back(rig.pipe(p));
        };
        c.caps = [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT);
            return f.extendedDynamicState3PolygonMode != VK_FALSE ? std::string() : std::string("extendedDynamicState3PolygonMode");
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const VkPolygonMode modes[3] = {VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE, VK_POLYGON_MODE_POINT};
            rig.kit.api.vkCmdSetPolygonModeEXT(cb, modes[f % 3]);
            rig.bind_vertex(cb, e.buffers[0]);
            rig.kit.api.vkCmdDraw(cb, 3, 1, f * 3, 0);
        };
        add_stamp(std::move(c), "fillModeNonSolid", "VK_EXT_extended_dynamic_state3");
    }
    {
        StampCase c;
        c.name = "color_write_mask_cycle";
        c.setup = [](Rig& rig, Env& e) {
            for (VkColorComponentFlags mask : {0x1u, 0x2u, 0x4u, 0x3u, 0x6u}) {
                PipelineDesc p = base_desc(rig, e);
                p.write_mask = mask;
                e.pipelines.push_back(rig.pipe(p));
            }
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.bind_graphics(cb, e.pipelines[f % 5]);
            const float white[4] = {1.0f, 0.9f, 0.7f, 1.0f};
            e.quads->quad(cb, -0.9f + 0.2f * static_cast<float>(f), -0.9f + 0.2f * static_cast<float>(f), 1.2f, 1.2f, white);
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "logic_op_xor_repeats";
        c.setup = [](Rig& rig, Env& e) {
            PipelineDesc p = base_desc(rig, e);
            p.logic_op = true;
            p.logic = VK_LOGIC_OP_XOR;
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            const float k[4] = {0.9f, 0.5f, 0.2f, 1.0f};
            e.quads->quad(cb, -0.9f + 0.1f * static_cast<float>(f), -0.9f + 0.1f * static_cast<float>(f), 1.3f, 1.3f, k);
        };
        add_stamp(std::move(c), "logicOp");
    }
    {
        StampCase c;
        c.name = "depth_compare_sequence";
        c.depth = true;
        c.depth_capture = true;
        c.setup = [](Rig& rig, Env& e) {
            const VkCompareOp ops[5] = {VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_LESS, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL};
            for (VkCompareOp op : ops) {
                PipelineDesc p = base_desc(rig, e);
                p.depth_test = true;
                p.depth_op = op;
                e.pipelines.push_back(rig.pipe(p));
            }
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.bind_graphics(cb, e.pipelines[f % 5]);
            const float z = 0.3f + 0.1f * static_cast<float>((f * 3) % 5);
            e.quads->quad(cb, -0.9f + 0.12f * static_cast<float>(f), -0.9f + 0.1f * static_cast<float>(f), 1.3f, 1.3f, palette(f), z, 0.1f * static_cast<float>(f % 3));
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "indirect_arguments_rewritten";
        c.setup = [](Rig& rig, Env& e) {
            const std::vector<Vertex2> tri = {vertex(-1, -1, 0), vertex(1, -1, 1), vertex(0, 1, 2)};
            e.buffers.push_back(rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            e.buffers.push_back(rig.blank(sizeof(VkDrawIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
            PipelineDesc p = rig.pos2c(*e.target);
            p.vertex = rig.sh("instidx.vert");
            e.pipelines.push_back(rig.pipe(p));
        };
        c.before = [](Rig& rig, Env& e, unsigned f) {
            const VkDrawIndirectCommand cmd{3, 2 + f, 0, f * 3};
            std::string error;
            rig.must(rig.kit.write(e.buffers[1], 0, &cmd, sizeof cmd, error), error);
        };
        c.draw = [](Rig& rig, Env& e, unsigned, VkCommandBuffer cb) {
            rig.bind_vertex(cb, e.buffers[0]);
            rig.kit.api.vkCmdDrawIndirect(cb, e.buffers[1].buffer, 0, 1, sizeof(VkDrawIndirectCommand));
        };
        add_stamp(std::move(c), "drawIndirectFirstInstance");
    }
    {
        StampCase c;
        c.name = "descriptor_swapped_each_frame";
        c.setup = [](Rig& rig, Env& e) {
            for (unsigned i = 0; i < 5; ++i) {
                e.textures.push_back(rig.sampled(8, 8, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(8, 8, 1 + i % 3, Pattern::pack(250 - 40 * i, 80 + 30 * i, 60), Pattern::pack(20, 40 * i, 200 - 25 * i))));
            }
            e.sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
            e.set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
            e.sets.push_back(rig.dset(e.set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {e.sampler, e.textures[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}}));
            e.layout = rig.playout({e.set_layout}, 32, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            PipelineDesc p;
            p.vertex = rig.sh("quad.vert");
            p.fragment = rig.sh("quad_tex.frag");
            p.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            p.layout = e.layout;
            p.pass = e.target->clear_pass;
            e.pipelines.push_back(rig.pipe(p));
        };
        c.before = [](Rig& rig, Env& e, unsigned f) {
            const VkDescriptorImageInfo info{e.sampler, e.textures[f % 5].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = e.sets[0];
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &info;
            rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 1, &w, 0, nullptr);
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, e.layout, 0, 1, &e.sets[0], 0, nullptr);
            const float rect[8] = {-0.9f + 0.3f * static_cast<float>(f), -0.8f + 0.1f * static_cast<float>(f), 0.5f, 1.4f, 1, 1, 1, 1};
            rig.push(cb, e.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, rect, 32);
            rig.kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "mip_level_walk";
        c.setup = [](Rig& rig, Env& e) {
            e.textures.push_back(rig.sampled(32, 32, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(32, 32, 4, Pattern::pack(240, 240, 240), Pattern::pack(220, 40, 40)), 6));
            e.set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
            e.layout = rig.playout({e.set_layout}, 32, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            e.sets.resize(5);
            for (unsigned i = 0; i < 5; ++i) {
                VkSamplerCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = info.minFilter = VK_FILTER_NEAREST;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                info.minLod = info.maxLod = static_cast<float>(i);
                info.maxAnisotropy = 1.0f;
                const VkSampler s = rig.sampler(info);
                e.sets[i] = rig.dset(e.set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {s, e.textures[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}});
            }
            PipelineDesc p;
            p.vertex = rig.sh("quad.vert");
            p.fragment = rig.sh("quad_tex.frag");
            p.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            p.layout = e.layout;
            p.pass = e.target->clear_pass;
            e.pipelines.push_back(rig.pipe(p));
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, e.layout, 0, 1, &e.sets[f % 5], 0, nullptr);
            const float rect[8] = {-0.95f + 0.38f * static_cast<float>(f), -0.7f, 0.36f, 1.4f, 1, 1, 1, 1};
            rig.push(cb, e.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, rect, 32);
            rig.kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
        };
        add_stamp(std::move(c));
    }
    {
        StampCase c;
        c.name = "pipeline_topology_switch";
        c.setup = [](Rig& rig, Env& e) {
            std::vector<Vertex2> v;
            for (int i = 0; i < 6; ++i) {
                v.push_back(vertex(-0.85f + 0.34f * static_cast<float>(i), (i % 2 == 0 ? -0.6f : 0.6f), static_cast<unsigned>(i)));
            }
            e.buffers.push_back(rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            for (VkPrimitiveTopology t : {VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                          VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN}) {
                PipelineDesc p = rig.pos2c(*e.target);
                p.topology = t;
                e.pipelines.push_back(rig.pipe(p));
            }
        };
        c.draw = [](Rig& rig, Env& e, unsigned f, VkCommandBuffer cb) {
            rig.bind_graphics(cb, e.pipelines[f % 5]);
            rig.bind_vertex(cb, e.buffers[0]);
            rig.viewport(cb, 0, 22.0f * static_cast<float>(f) - 40.0f, 128, 90);
            rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
        };
        add_stamp(std::move(c));
    }
}

const int registered = (register_stamps(), 0);

}  // namespace

}  // namespace lsw::render::vk
