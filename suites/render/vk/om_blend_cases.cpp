#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

struct FactorEntry {
    const char* name;
    VkBlendFactor factor;
    bool dual;
};

const std::vector<FactorEntry>& factors() {
    static const std::vector<FactorEntry> table = {
        {"zero", VK_BLEND_FACTOR_ZERO, false},
        {"one", VK_BLEND_FACTOR_ONE, false},
        {"src_color", VK_BLEND_FACTOR_SRC_COLOR, false},
        {"one_minus_src_color", VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, false},
        {"dst_color", VK_BLEND_FACTOR_DST_COLOR, false},
        {"one_minus_dst_color", VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, false},
        {"src_alpha", VK_BLEND_FACTOR_SRC_ALPHA, false},
        {"one_minus_src_alpha", VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false},
        {"dst_alpha", VK_BLEND_FACTOR_DST_ALPHA, false},
        {"one_minus_dst_alpha", VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, false},
        {"constant_color", VK_BLEND_FACTOR_CONSTANT_COLOR, false},
        {"one_minus_constant_color", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, false},
        {"constant_alpha", VK_BLEND_FACTOR_CONSTANT_ALPHA, false},
        {"one_minus_constant_alpha", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA, false},
        {"src_alpha_saturate", VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, false},
        {"src1_color", VK_BLEND_FACTOR_SRC1_COLOR, true},
        {"one_minus_src1_color", VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, true},
        {"src1_alpha", VK_BLEND_FACTOR_SRC1_ALPHA, true},
        {"one_minus_src1_alpha", VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA, true},
    };
    return table;
}

struct BlendSetup {
    VkBlendFactor src_color;
    VkBlendFactor dst_color;
    VkBlendFactor src_alpha;
    VkBlendFactor dst_alpha;
    VkBlendOp color_op;
    VkBlendOp alpha_op;
    bool dual;
    const void* next;
};

void blend_cells(const QuadKit& quads, VkCommandBuffer cb, float x0) {
    const float alphas[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (unsigned col = 0; col < 2; ++col) {
        for (unsigned row = 0; row < 4; ++row) {
            const float* base = palette(col * 3 + row);
            const float color[4] = {base[0], base[1], base[2], alphas[row]};
            quads.quad(cb, x0 + 0.05f + 0.47f * static_cast<float>(col), -0.94f + 0.48f * static_cast<float>(row), 0.4f, 0.42f, color);
        }
    }
}

PipelineDesc blend_pipe(Rig& rig, QuadKit& quads, Target& target, const BlendSetup& setup) {
    PipelineDesc p = quads.desc(target);
    if (setup.dual) {
        p.fragment = rig.sh("quad_dual.frag");
    }
    p.blend = true;
    p.src_color = setup.src_color;
    p.dst_color = setup.dst_color;
    p.src_alpha = setup.src_alpha;
    p.dst_alpha = setup.dst_alpha;
    p.color_op = setup.color_op;
    p.alpha_op = setup.alpha_op;
    p.blend_constants[0] = 0.3f;
    p.blend_constants[1] = 0.6f;
    p.blend_constants[2] = 0.9f;
    p.blend_constants[3] = 0.5f;
    p.blend_next = setup.next;
    return p;
}

void run_blend(Rig& rig, CaptureSet& out, const BlendSetup& left, const BlendSetup& right) {
    Target& target = rig.make_target(TargetDesc{});
    QuadKit quads(rig);
    PipelineDesc bg = quads.desc(target);
    bg.vertex = rig.sh("full.vert");
    bg.fragment = rig.sh("grad.frag");
    bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkPipeline p_bg = rig.pipe(bg);
    const VkPipeline p_left = rig.pipe(blend_pipe(rig, quads, target, left));
    const VkPipeline p_right = rig.pipe(blend_pipe(rig, quads, target, right));
    out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
        rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        rig.bind_graphics(cb, p_left);
        blend_cells(quads, cb, -1.0f);
        rig.bind_graphics(cb, p_right);
        blend_cells(quads, cb, 0.0f);
    }, quads.layout));
}

void register_factors() {
    for (const FactorEntry& entry : factors()) {
        CaseSpec spec("om", std::string("blend_factor_") + entry.name);
        spec.tolerance(Tolerance::tight(1));
        if (entry.dual) {
            spec.feature("dualSrcBlend");
        }
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            if (entry.dual && rig.kit.properties.limits.maxFragmentDualSrcAttachments < 1) {
                rig.fail("maxFragmentDualSrcAttachments is zero");
            }
            const BlendSetup left{entry.factor, VK_BLEND_FACTOR_ONE, entry.factor, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_OP_ADD, entry.dual, nullptr};
            const BlendSetup right{VK_BLEND_FACTOR_ONE, entry.factor, VK_BLEND_FACTOR_ONE, entry.factor, VK_BLEND_OP_ADD, VK_BLEND_OP_ADD, entry.dual, nullptr};
            run_blend(rig, out, left, right);
        });
    }
}

void register_ops() {
    struct Op {
        const char* name;
        VkBlendOp op;
    };
    const std::vector<Op> ops = {{"add", VK_BLEND_OP_ADD},
                                 {"subtract", VK_BLEND_OP_SUBTRACT},
                                 {"reverse_subtract", VK_BLEND_OP_REVERSE_SUBTRACT},
                                 {"min", VK_BLEND_OP_MIN},
                                 {"max", VK_BLEND_OP_MAX}};
    for (const Op& entry : ops) {
        CaseSpec spec("om", std::string("blend_op_") + entry.name);
        spec.tolerance(Tolerance::tight(1));
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            const BlendSetup left{VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, entry.op, entry.op, false, nullptr};
            const BlendSetup right{VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, entry.op, entry.op, false, nullptr};
            run_blend(rig, out, left, right);
        });
    }
    Cases::add(CaseSpec("om", "blend_separate_alpha").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        const BlendSetup left{VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_OP_MAX, false, nullptr};
        const BlendSetup right{VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_OP_REVERSE_SUBTRACT, false, nullptr};
        run_blend(rig, out, left, right);
    });
    Cases::add(CaseSpec("om", "blend_constant_mix").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        const BlendSetup left{VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_OP_ADD, false, nullptr};
        const BlendSetup right{VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA, VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_SUBTRACT, false, nullptr};
        run_blend(rig, out, left, right);
    });
}

void register_advanced() {
    struct Advanced {
        const char* name;
        VkBlendOp op;
    };
    const std::vector<Advanced> ops = {{"multiply", VK_BLEND_OP_MULTIPLY_EXT},
                                       {"screen", VK_BLEND_OP_SCREEN_EXT},
                                       {"overlay", VK_BLEND_OP_OVERLAY_EXT},
                                       {"difference", VK_BLEND_OP_DIFFERENCE_EXT},
                                       {"hsl_hue", VK_BLEND_OP_HSL_HUE_EXT}};
    for (const Advanced& entry : ops) {
        CaseSpec spec("om", std::string("blend_advanced_") + entry.name);
        spec.extension("VK_EXT_blend_operation_advanced").tolerance(Tolerance::tight(2));
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                VkPipelineColorBlendAdvancedStateCreateInfoEXT advanced{};
                advanced.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_ADVANCED_STATE_CREATE_INFO_EXT;
                advanced.srcPremultiplied = VK_TRUE;
                advanced.dstPremultiplied = VK_TRUE;
                advanced.blendOverlap = VK_BLEND_OVERLAP_UNCORRELATED_EXT;
                const BlendSetup setup{VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, entry.op, entry.op, false, &advanced};
                run_blend(rig, out, setup, setup);
            },
            [](Kit& kit) -> std::string {
                const auto p = kit.query_properties<VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_PROPERTIES_EXT);
                return p.advancedBlendAllOperations != VK_FALSE ? std::string() : std::string("advancedBlendAllOperations");
            });
    }
}

void register_logic_ops() {
    struct Logic {
        const char* name;
        VkLogicOp op;
    };
    const std::vector<Logic> ops = {
        {"clear", VK_LOGIC_OP_CLEAR},         {"and", VK_LOGIC_OP_AND},          {"and_reverse", VK_LOGIC_OP_AND_REVERSE},
        {"copy", VK_LOGIC_OP_COPY},           {"and_inverted", VK_LOGIC_OP_AND_INVERTED}, {"no_op", VK_LOGIC_OP_NO_OP},
        {"xor", VK_LOGIC_OP_XOR},             {"or", VK_LOGIC_OP_OR},            {"nor", VK_LOGIC_OP_NOR},
        {"equivalent", VK_LOGIC_OP_EQUIVALENT}, {"invert", VK_LOGIC_OP_INVERT},  {"or_reverse", VK_LOGIC_OP_OR_REVERSE},
        {"copy_inverted", VK_LOGIC_OP_COPY_INVERTED}, {"or_inverted", VK_LOGIC_OP_OR_INVERTED}, {"nand", VK_LOGIC_OP_NAND},
        {"set", VK_LOGIC_OP_SET},
    };
    for (const Logic& entry : ops) {
        CaseSpec spec("om", std::string("logic_op_") + entry.name);
        spec.feature("logicOp").tolerance(Tolerance::tight(1));
        spec.uniform_ok();
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            QuadKit quads(rig);
            PipelineDesc bg = quads.desc(target);
            bg.vertex = rig.sh("full.vert");
            bg.fragment = rig.sh("grad.frag");
            bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            PipelineDesc fg = quads.desc(target);
            fg.logic_op = true;
            fg.logic = entry.op;
            const VkPipeline p_bg = rig.pipe(bg);
            const VkPipeline p_fg = rig.pipe(fg);
            out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                rig.bind_graphics(cb, p_fg);
                for (unsigned i = 0; i < 9; ++i) {
                    const float color[4] = {0.15f + 0.1f * static_cast<float>(i), 0.9f - 0.09f * static_cast<float>(i), 0.3f + 0.07f * static_cast<float>(i), 0.6f};
                    quads.quad(cb, -0.92f + 0.62f * static_cast<float>(i % 3), -0.92f + 0.62f * static_cast<float>(i / 3), 0.56f, 0.56f, color);
                }
            }, quads.layout));
        });
    }
}

void register_masks_and_independent() {
    Cases::add(CaseSpec("om", "write_mask_all_16").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        PipelineDesc bg = quads.desc(target);
        bg.vertex = rig.sh("full.vert");
        bg.fragment = rig.sh("grad.frag");
        bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        std::vector<VkPipeline> pipes;
        for (unsigned mask = 0; mask < 16; ++mask) {
            PipelineDesc fg = quads.desc(target);
            fg.write_mask = mask;
            pipes.push_back(rig.pipe(fg));
        }
        const VkPipeline p_bg = rig.pipe(bg);
        out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            const float color[4] = {0.95f, 0.45f, 0.75f, 0.35f};
            for (unsigned mask = 0; mask < 16; ++mask) {
                rig.bind_graphics(cb, pipes[mask]);
                quads.quad(cb, -0.96f + 0.5f * static_cast<float>(mask % 4), -0.96f + 0.5f * static_cast<float>(mask / 4), 0.44f, 0.44f, color);
            }
        }, quads.layout));
    });
    CaseSpec independent("om", "blend_independent_8");
    independent.feature("independentBlend").tolerance(Tolerance::tight(1));
    Cases::add(
        std::move(independent),
        [](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.colors.assign(8, VK_FORMAT_R8G8B8A8_UNORM);
            Target& target = rig.make_target(td);
            target.clear_color[0] = 0.25f;
            target.clear_color[1] = 0.3f;
            target.clear_color[2] = 0.45f;
            target.clear_color[3] = 0.5f;
            PipelineDesc pipe = rig.pos2c(target);
            pipe.fragment = rig.sh("mrt8.frag");
            for (unsigned i = 0; i < 8; ++i) {
                VkPipelineColorBlendAttachmentState a{};
                a.colorWriteMask = 0xf;
                a.blendEnable = i == 6 ? VK_FALSE : VK_TRUE;
                a.colorBlendOp = i % 3 == 2 ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
                a.alphaBlendOp = VK_BLEND_OP_ADD;
                a.srcColorBlendFactor = i % 2 == 0 ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
                a.dstColorBlendFactor = i % 2 == 0 ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
                a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                a.dstAlphaBlendFactor = i < 4 ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE;
                if (i == 5) {
                    a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
                }
                pipe.blend_states.push_back(a);
            }
            const std::vector<Vertex2> quad = {vertex(-0.85f, -0.85f, 0), vertex(0.85f, -0.85f, 1), vertex(-0.85f, 0.85f, 2),
                                               vertex(0.85f, -0.85f, 1),  vertex(0.85f, 0.85f, 3),  vertex(-0.85f, 0.85f, 2)};
            const Buffer vb = rig.buffer(quad, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const VkPipeline pipeline = rig.pipe(pipe);
            rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0); });
            for (std::size_t i = 0; i < 8; ++i) {
                out.add("c" + std::to_string(i), rig.color(target, i));
            }
        },
        [](Kit& kit) -> std::string {
            return kit.properties.limits.maxColorAttachments < 8 ? "maxColorAttachments below 8" : std::string();
        });
}

const int registered = (register_factors(), register_ops(), register_advanced(), register_logic_ops(), register_masks_and_independent(), 0);

}  // namespace

}  // namespace lsw::render::vk
