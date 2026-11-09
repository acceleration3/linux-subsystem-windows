#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

const float kBlack[4] = {0.04f, 0.04f, 0.07f, 1.0f};

VkStencilOpState stencil_state(VkCompareOp compare, VkStencilOp pass, VkStencilOp fail = VK_STENCIL_OP_KEEP,
                               VkStencilOp depth_fail = VK_STENCIL_OP_KEEP) {
    VkStencilOpState s{};
    s.compareOp = compare;
    s.passOp = pass;
    s.failOp = fail;
    s.depthFailOp = depth_fail;
    s.compareMask = 0xff;
    s.writeMask = 0xff;
    return s;
}

CaseSpec core(const char* name) {
    CaseSpec spec("pipe", name);
    spec.tolerance(Tolerance::tight(1));
    return spec;
}

void register_core_dynamic() {
    Cases::add(core("dyn_viewport_scissor"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        const VkPipeline pipeline = rig.pipe(quads.desc(target));
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 4; ++i) {
                const float qx = static_cast<float>(i % 2) * 64.0f;
                const float qy = static_cast<float>(i / 2) * 64.0f;
                rig.viewport(cb, qx, qy, 64, 64);
                rig.scissor(cb, static_cast<int>(qx) + 6, static_cast<int>(qy) + 6, 52, 52);
                quads.quad(cb, -1, -1, 2, 2, palette(i));
            }
            rig.viewport(cb, 20, 20, 88, 88);
            rig.scissor(cb, 44, 44, 40, 40);
            quads.quad(cb, -0.5f, -0.5f, 1.0f, 1.0f, palette(7));
        }, quads.layout));
    });
    Cases::add(core("dyn_blend_constants"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        PipelineDesc bg = quads.desc(target);
        bg.vertex = rig.sh("full.vert");
        bg.fragment = rig.sh("grad.frag");
        bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PipelineDesc fg = quads.desc(target);
        fg.blend = true;
        fg.src_color = VK_BLEND_FACTOR_CONSTANT_COLOR;
        fg.dst_color = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        fg.src_alpha = VK_BLEND_FACTOR_CONSTANT_ALPHA;
        fg.dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        fg.dynamic = {VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        const VkPipeline p_bg = rig.pipe(bg);
        const VkPipeline p_fg = rig.pipe(fg);
        out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            rig.bind_graphics(cb, p_fg);
            const float constants[4][4] = {{0.25f, 0.25f, 0.25f, 0.25f}, {0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, 0.2f, 0.0f, 0.75f}, {0.1f, 0.9f, 0.6f, 1.0f}};
            for (unsigned i = 0; i < 4; ++i) {
                rig.kit.api.vkCmdSetBlendConstants(cb, constants[i]);
                quads.quad(cb, -0.9f + 0.45f * static_cast<float>(i), -0.8f + 0.1f * static_cast<float>(i), 0.4f, 1.4f, palette(i + 3));
            }
        }, quads.layout));
    });
    Cases::add(core("dyn_depth_bias"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_only(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc base = quads.desc(target);
        base.depth_test = true;
        base.depth_op = VK_COMPARE_OP_LESS;
        PipelineDesc biased = base;
        biased.depth_bias = true;
        biased.dynamic = {VK_DYNAMIC_STATE_DEPTH_BIAS};
        const VkPipeline p_base = rig.pipe(base);
        const VkPipeline p_bias = rig.pipe(biased);
        const float factors[4][3] = {{0.0f, 0.0f, 0.0f}, {-300000.0f, 0.0f, 0.0f}, {300000.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -4.0f}};
        out.add("", rig.draw(target, p_base, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -1, -1, 2, 2, palette(0), 0.5f, 0.2f);
            rig.bind_graphics(cb, p_bias);
            for (unsigned i = 0; i < 4; ++i) {
                const float x0 = -1.0f + 0.5f * static_cast<float>(i);
                const float z0 = 0.5f + 0.2f * (static_cast<float>(i) * 0.25f);
                rig.kit.api.vkCmdSetDepthBias(cb, factors[i][0], factors[i][1], factors[i][2]);
                quads.quad(cb, x0 + 0.04f, -0.8f, 0.42f, 1.6f, palette(1 + i), z0 + 0.2f * 0.02f, 0.2f * 0.21f);
            }
        }, quads.layout));
    });
    Cases::add(core("dyn_stencil_state"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_stencil(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc write = quads.desc(target);
        write.stencil_test = true;
        write.depth_write = false;
        write.stencil = stencil_state(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE);
        write.dynamic = {VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK};
        PipelineDesc read = quads.desc(target);
        read.stencil_test = true;
        read.depth_write = false;
        read.stencil = stencil_state(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP);
        read.dynamic = {VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
        const VkPipeline p_write = rig.pipe(write);
        const VkPipeline p_read = rig.pipe(read);
        const std::uint32_t refs[4] = {0x11, 0x22, 0x44, 0x8a};
        const std::uint32_t wmask[4] = {0xf0, 0x0f, 0xff, 0x3c};
        const std::uint32_t rref[4] = {0x10, 0x02, 0x44, 0x08};
        const std::uint32_t rmask[4] = {0xf0, 0x0f, 0xff, 0x3c};
        out.add("", rig.draw(target, p_write, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 4; ++i) {
                rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, refs[i]);
                rig.kit.api.vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, wmask[i]);
                quads.quad(cb, -1.0f + 0.5f * static_cast<float>(i), -1, 0.5f, 2, palette(i));
            }
            rig.bind_graphics(cb, p_read);
            for (unsigned i = 0; i < 4; ++i) {
                rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, rref[i]);
                rig.kit.api.vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, rmask[i]);
                rig.scissor(cb, 0, static_cast<int>(i) * 32, 128, 32);
                quads.quad(cb, -1, -1, 2, 2, palette(i + 4));
            }
        }, quads.layout));
    });
    CaseSpec bounds = core("dyn_depth_bounds");
    bounds.feature("depthBounds");
    Cases::add(std::move(bounds), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_only(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc fill = quads.desc(target);
        fill.depth_test = true;
        fill.depth_op = VK_COMPARE_OP_ALWAYS;
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_test = true;
        pipe.depth_write = false;
        pipe.depth_op = VK_COMPARE_OP_ALWAYS;
        pipe.depth_bounds = true;
        pipe.dynamic = {VK_DYNAMIC_STATE_DEPTH_BOUNDS};
        const VkPipeline p_fill = rig.pipe(fill);
        const VkPipeline pipeline = rig.pipe(pipe);
        const float zs[5] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
        out.add("", rig.draw(target, p_fill, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 5; ++i) {
                quads.quad(cb, -1, -1.0f + 0.4f * static_cast<float>(i), 2, 0.4f, palette(i + 4), zs[i]);
            }
            rig.bind_graphics(cb, pipeline);
            for (unsigned half = 0; half < 2; ++half) {
                rig.scissor(cb, static_cast<int>(half) * 64, 0, 64, 128);
                rig.kit.api.vkCmdSetDepthBounds(cb, half == 0 ? 0.25f : 0.0f, half == 0 ? 0.75f : 0.35f);
                quads.quad(cb, -1, -1, 2, 2, palette(half), 0.5f);
            }
        }, quads.layout));
    });
    CaseSpec width = core("dyn_line_width");
    width.feature("wideLines");
    Cases::add(
        std::move(width),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kBlack, kBlack + 4, target.clear_color);
            std::vector<Vertex2> lines;
            for (int i = 0; i < 6; ++i) {
                const float y = -0.8f + 0.32f * static_cast<float>(i);
                lines.push_back(vertex(-0.85f, y, static_cast<unsigned>(i)));
                lines.push_back(vertex(0.85f, y + 0.1f, static_cast<unsigned>(i + 1)));
            }
            const Buffer vb = rig.buffer(lines, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            pipe.dynamic = {VK_DYNAMIC_STATE_LINE_WIDTH};
            const VkPipeline pipeline = rig.pipe(pipe);
            const float widths[6] = {1.0f, 2.0f, 3.0f, 4.5f, 6.0f, 8.0f};
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                for (unsigned i = 0; i < 6; ++i) {
                    rig.kit.api.vkCmdSetLineWidth(cb, widths[i]);
                    rig.kit.api.vkCmdDraw(cb, 2, 1, i * 2, 0);
                }
            }));
        },
        [](Kit& kit) -> std::string { return kit.properties.limits.lineWidthRange[1] < 8.0f ? "lineWidthRange below 8" : std::string(); });
}

CaseSpec eds1(const char* name) {
    CaseSpec spec("pipe", name);
    spec.min_version(1, 3).tolerance(Tolerance::tight(1));
    return spec;
}

void register_eds1() {
    Cases::add(eds1("dyn_eds1_cull_front"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.dynamic = {VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
        const VkPipeline pipeline = rig.pipe(pipe);
        const VkCullModeFlags culls[6] = {VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT,
                                          VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT};
        const VkFrontFace fronts[6] = {VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                       VK_FRONT_FACE_CLOCKWISE,         VK_FRONT_FACE_CLOCKWISE,         VK_FRONT_FACE_CLOCKWISE};
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 6; ++i) {
                rig.kit.api.vkCmdSetCullMode(cb, culls[i]);
                rig.kit.api.vkCmdSetFrontFace(cb, fronts[i]);
                quads.quad(cb, -0.95f + 0.64f * static_cast<float>(i % 3), -0.9f + 0.95f * static_cast<float>(i / 3), 0.6f, 0.85f, palette(i));
            }
        }, quads.layout));
    });
    Cases::add(eds1("dyn_eds1_depth_state"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_only(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.dynamic = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
        const VkPipeline pipeline = rig.pipe(pipe);
        struct State {
            VkBool32 test;
            VkBool32 write;
            VkCompareOp op;
        };
        const State states[6] = {{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS},    {VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER},
                                 {VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS},   {VK_FALSE, VK_TRUE, VK_COMPARE_OP_NEVER},
                                 {VK_TRUE, VK_TRUE, VK_COMPARE_OP_ALWAYS},  {VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL}};
        const float white[4] = {0.95f, 0.95f, 0.95f, 1.0f};
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdSetDepthTestEnable(cb, VK_TRUE);
            rig.kit.api.vkCmdSetDepthWriteEnable(cb, VK_TRUE);
            rig.kit.api.vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_LESS);
            quads.quad(cb, -1, -1, 2, 2, palette(7), 0.5f);
            for (unsigned i = 0; i < 6; ++i) {
                rig.kit.api.vkCmdSetDepthTestEnable(cb, states[i].test);
                rig.kit.api.vkCmdSetDepthWriteEnable(cb, states[i].write);
                rig.kit.api.vkCmdSetDepthCompareOp(cb, states[i].op);
                quads.quad(cb, -0.95f + 0.64f * static_cast<float>(i % 3), -0.9f + 0.95f * static_cast<float>(i / 3), 0.6f, 0.85f, palette(i), 0.4f);
            }
            rig.kit.api.vkCmdSetDepthTestEnable(cb, VK_TRUE);
            rig.kit.api.vkCmdSetDepthWriteEnable(cb, VK_FALSE);
            rig.kit.api.vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_LESS);
            for (unsigned i = 0; i < 6; ++i) {
                quads.quad(cb, -0.85f + 0.64f * static_cast<float>(i % 3), -0.8f + 0.95f * static_cast<float>(i / 3), 0.35f, 0.45f, white, 0.45f);
            }
        }, quads.layout));
    });
    Cases::add(eds1("dyn_eds1_stencil_op"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_stencil(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_write = false;
        pipe.stencil = stencil_state(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_KEEP);
        pipe.dynamic = {VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdSetStencilTestEnable(cb, VK_TRUE);
            rig.kit.api.vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                          VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
            rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
            quads.quad(cb, -0.9f, -0.9f, 1.2f, 1.2f, palette(0));
            quads.quad(cb, -0.4f, -0.5f, 1.2f, 1.2f, palette(1));
            quads.quad(cb, -0.1f, -0.1f, 1.0f, 1.0f, palette(2));
            rig.kit.api.vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                                          VK_COMPARE_OP_EQUAL);
            for (unsigned i = 0; i < 4; ++i) {
                rig.kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, i);
                rig.scissor(cb, static_cast<int>(i) * 32, 0, 32, 128);
                quads.quad(cb, -1, -1, 2, 2, palette(i + 4));
            }
            rig.kit.api.vkCmdSetStencilTestEnable(cb, VK_FALSE);
            rig.scissor(cb, 0, 112, 128, 16);
            quads.quad(cb, -1, -1, 2, 2, palette(7));
        }, quads.layout));
    });
    Cases::add(eds1("dyn_eds1_topology"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kBlack, kBlack + 4, target.clear_color);
        std::vector<Vertex2> v;
        for (int i = 0; i < 12; ++i) {
            const float t = static_cast<float>(i) / 11.0f;
            v.push_back(vertex(-0.85f + 1.7f * t, (i % 2 == 0 ? -0.6f : 0.6f) * (0.4f + 0.6f * std::sin(t * 3.0f)), static_cast<unsigned>(i)));
        }
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc lines = rig.pos2c(target);
        lines.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        lines.dynamic = {VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
        PipelineDesc tris = rig.pos2c(target);
        tris.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        tris.dynamic = {VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
        const VkPipeline p_lines = rig.pipe(lines);
        const VkPipeline p_tris = rig.pipe(tris);
        out.add("", rig.draw(target, p_lines, &vb, [&](VkCommandBuffer cb) {
            rig.viewport(cb, 0, 0, 64, 64);
            rig.kit.api.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
            rig.kit.api.vkCmdDraw(cb, 12, 1, 0, 0);
            rig.viewport(cb, 64, 0, 64, 64);
            rig.kit.api.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
            rig.kit.api.vkCmdDraw(cb, 12, 1, 0, 0);
            rig.bind_graphics(cb, p_tris);
            rig.viewport(cb, 0, 64, 64, 64);
            rig.kit.api.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            rig.kit.api.vkCmdDraw(cb, 12, 1, 0, 0);
            rig.viewport(cb, 64, 64, 64, 64);
            rig.kit.api.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
            rig.kit.api.vkCmdDraw(cb, 12, 1, 0, 0);
        }));
    });
    CaseSpec counts = eds1("dyn_eds1_viewport_count");
    counts.feature("multiViewport");
    Cases::add(
        std::move(counts),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kBlack, kBlack + 4, target.clear_color);
            const VkPipelineLayout layout = rig.playout({}, 32, VK_SHADER_STAGE_VERTEX_BIT);
            PipelineDesc pipe;
            pipe.vertex = rig.sh("vpindex.v12.vert");
            pipe.fragment = rig.sh("col4.frag");
            pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            pipe.dynamic = {VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT};
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                const VkViewport viewports[3] = {{0, 0, 64, 64, 0, 1}, {64, 0, 64, 64, 0, 1}, {0, 64, 128, 64, 0, 1}};
                const VkRect2D scissors[3] = {{{4, 4}, {56, 56}}, {{68, 4}, {56, 56}}, {{0, 64}, {128, 64}}};
                rig.kit.api.vkCmdSetViewportWithCount(cb, 3, viewports);
                rig.kit.api.vkCmdSetScissorWithCount(cb, 3, scissors);
                const float quad[8] = {-0.8f, -0.8f, 1.6f, 1.6f, 0.9f, 0.5f, 0.2f, 1.0f};
                rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, quad, 32);
                rig.kit.api.vkCmdDraw(cb, 4, 3, 0, 0);
            }, layout));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            return f.shaderOutputViewportIndex != VK_FALSE ? std::string() : std::string("shaderOutputViewportIndex");
        });
    Cases::add(eds1("dyn_eds1_bind_vertex_buffers2"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kBlack, kBlack + 4, target.clear_color);
        std::vector<Vertex2> v;
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(i) * 0.6f;
            v.push_back(vertex(0.7f * std::cos(a) * (0.5f + 0.04f * static_cast<float>(i)), 0.7f * std::sin(a) * (0.5f + 0.04f * static_cast<float>(i)), static_cast<unsigned>(i)));
        }
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.dynamic = {VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            VkDeviceSize offset = 0;
            VkDeviceSize size = sizeof(Vertex2) * 9;
            VkDeviceSize stride = sizeof(Vertex2);
            rig.viewport(cb, 0, 0, 128, 64);
            rig.kit.api.vkCmdBindVertexBuffers2(cb, 0, 1, &vb.buffer, &offset, &size, &stride);
            rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
            rig.viewport(cb, 0, 64, 128, 64);
            stride = sizeof(Vertex2) * 2;
            size = sizeof(Vertex2) * 12;
            rig.kit.api.vkCmdBindVertexBuffers2(cb, 0, 1, &vb.buffer, &offset, &size, &stride);
            rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
        }));
    });
}

CaseSpec eds_ext(const char* name, const char* extension) {
    CaseSpec spec("pipe", name);
    spec.extension(extension).tolerance(Tolerance::tight(1));
    return spec;
}

void register_eds2() {
    Cases::add(eds1("dyn_eds2_rasterizer_discard"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.dynamic = {VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 8; ++i) {
                rig.kit.api.vkCmdSetRasterizerDiscardEnable(cb, (i % 3 == 1) ? VK_TRUE : VK_FALSE);
                quads.quad(cb, -0.95f + 0.48f * static_cast<float>(i % 4), -0.9f + 0.95f * static_cast<float>(i / 4), 0.44f, 0.85f, palette(i));
            }
        }, quads.layout));
    });
    Cases::add(eds1("dyn_eds2_depth_bias_enable"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = Cases::depth_only(rig.kit);
        Target& target = rig.make_target(td);
        std::copy(kBlack, kBlack + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc base = quads.desc(target);
        base.depth_test = true;
        PipelineDesc biased = base;
        biased.depth_bias = true;
        biased.bias_constant = -400000.0f;
        biased.dynamic = {VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE};
        const VkPipeline p_base = rig.pipe(base);
        const VkPipeline p_bias = rig.pipe(biased);
        out.add("", rig.draw(target, p_base, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -1, -1, 2, 2, palette(0), 0.5f, 0.0f);
            rig.bind_graphics(cb, p_bias);
            for (unsigned i = 0; i < 4; ++i) {
                rig.kit.api.vkCmdSetDepthBiasEnable(cb, (i % 2) ? VK_TRUE : VK_FALSE);
                quads.quad(cb, -0.95f + 0.5f * static_cast<float>(i), -0.8f, 0.42f, 1.6f, palette(i + 1), 0.5f, 0.0f);
            }
        }, quads.layout));
    });
    Cases::add(eds1("dyn_eds2_restart_enable"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::vector<Vertex2> v;
        for (int s = 0; s < 3; ++s) {
            for (int i = 0; i < 3; ++i) {
                v.push_back(vertex(-0.85f + 0.55f * static_cast<float>(i) + 0.1f * static_cast<float>(s), -0.8f + 0.55f * static_cast<float>(s), static_cast<unsigned>(i + s)));
                v.push_back(vertex(-0.8f + 0.55f * static_cast<float>(i) + 0.1f * static_cast<float>(s), -0.4f + 0.55f * static_cast<float>(s), static_cast<unsigned>(i + s + 3)));
            }
        }
        const std::vector<std::uint16_t> with = {0, 1, 2, 3, 4, 5, 0xFFFF, 6, 7, 8, 9, 10, 11};
        const std::vector<std::uint16_t> without = {12, 13, 14, 15, 16, 17};
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer ib = rig.buffer(with, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.dynamic = {VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdSetPrimitiveRestartEnable(cb, VK_TRUE);
            rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(with.size()), 1, 0, 0, 0);
            rig.kit.api.vkCmdSetPrimitiveRestartEnable(cb, VK_FALSE);
            rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(without.size()), 1, 0, 0, 0);
        }));
    });
    CaseSpec logic = eds_ext("dyn_eds2_logic_op", "VK_EXT_extended_dynamic_state2");
    logic.feature("logicOp");
    Cases::add(
        std::move(logic),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            QuadKit quads(rig);
            PipelineDesc bg = quads.desc(target);
            bg.vertex = rig.sh("full.vert");
            bg.fragment = rig.sh("grad.frag");
            bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            PipelineDesc fg = quads.desc(target);
            fg.logic_op = true;
            fg.dynamic = {VK_DYNAMIC_STATE_LOGIC_OP_EXT};
            const VkPipeline p_bg = rig.pipe(bg);
            const VkPipeline p_fg = rig.pipe(fg);
            const VkLogicOp ops[6] = {VK_LOGIC_OP_XOR, VK_LOGIC_OP_AND, VK_LOGIC_OP_OR, VK_LOGIC_OP_INVERT, VK_LOGIC_OP_NAND, VK_LOGIC_OP_EQUIVALENT};
            out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                rig.bind_graphics(cb, p_fg);
                for (unsigned i = 0; i < 6; ++i) {
                    rig.kit.api.vkCmdSetLogicOpEXT(cb, ops[i]);
                    quads.quad(cb, -0.95f + 0.64f * static_cast<float>(i % 3), -0.9f + 0.95f * static_cast<float>(i / 3), 0.6f, 0.85f, palette(i));
                }
            }, quads.layout));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceExtendedDynamicState2FeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT);
            return f.extendedDynamicState2LogicOp != VK_FALSE ? std::string() : std::string("extendedDynamicState2LogicOp");
        });
    Cases::add(
        eds_ext("dyn_eds2_patch_points", "VK_EXT_extended_dynamic_state2"),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::vector<Vertex2> v;
            for (int p = 0; p < 2; ++p) {
                const float x = -0.9f + 0.95f * static_cast<float>(p);
                v.push_back(vertex(x, -0.8f, 0));
                v.push_back(vertex(x + 0.85f, -0.75f, 1));
                v.push_back(vertex(x + 0.8f, 0.85f, 2));
                v.push_back(vertex(x + 0.05f, 0.8f, 3));
            }
            const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            pipe.patch_points = 4;
            pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, rig.sh("patch4.tesc"), nullptr});
            pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, rig.sh("quad4.tese"), nullptr});
            pipe.dynamic = {VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT};
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                rig.viewport(cb, 0, 0, 128, 64);
                rig.kit.api.vkCmdSetPatchControlPointsEXT(cb, 4);
                rig.kit.api.vkCmdDraw(cb, 8, 1, 0, 0);
                rig.viewport(cb, 0, 64, 128, 64);
                rig.kit.api.vkCmdSetPatchControlPointsEXT(cb, 8);
                rig.kit.api.vkCmdDraw(cb, 8, 1, 0, 0);
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceExtendedDynamicState2FeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT);
            if (kit.features.tessellationShader == VK_FALSE) {
                return "tessellationShader";
            }
            return f.extendedDynamicState2PatchControlPoints != VK_FALSE ? std::string() : std::string("extendedDynamicState2PatchControlPoints");
        });
}

const int registered = (register_core_dynamic(), register_eds1(), register_eds2(), 0);

}  // namespace

}  // namespace lsw::render::vk
