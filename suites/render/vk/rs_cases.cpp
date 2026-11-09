#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/vk/quadkit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.06f, 0.07f, 0.1f, 1.0f};

struct CubeMesh {
    Buffer vertex;
    Buffer index;
    std::uint32_t count = 0;
};

CubeMesh make_cube(Rig& rig) {
    CubeMesh mesh;
    const auto vertices = Mesh::cube_vertices();
    const auto indices = Mesh::cube_indices();
    mesh.count = static_cast<std::uint32_t>(indices.size());
    mesh.vertex = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.index = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    return mesh;
}

PipelineDesc cube_pipe(Rig& rig, Target& target, VkPipelineLayout layout, const char* vertex = "mesh.vert") {
    PipelineDesc p;
    p.vertex = rig.sh(vertex);
    p.fragment = rig.sh("mesh.frag");
    p.bindings = {{0, sizeof(ColoredVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    p.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
    p.layout = layout;
    p.pass = target.clear_pass;
    p.depth_test = target.desc.depth != VK_FORMAT_UNDEFINED;
    return p;
}

Mat4 cube_transform(float yaw = 0.7f, float pitch = -0.5f) {
    return Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true) * Mat4::translate(0, 0, -5.0f) * Mat4::rotate_y(yaw) * Mat4::rotate_x(pitch);
}

Image draw_cube(Rig& rig, Target& target, VkPipeline pipeline, VkPipelineLayout layout, const CubeMesh& mesh, const Mat4& m) {
    return rig.draw(target, pipeline, &mesh.vertex, [&](VkCommandBuffer cb) {
        rig.kit.api.vkCmdBindIndexBuffer(cb, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT16);
        rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, m.m, 64);
        rig.kit.api.vkCmdDrawIndexed(cb, mesh.count, 1, 0, 0, 0);
    }, layout);
}

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("rs", name);
    spec.tolerance(Tolerance::tight(1));
    return spec;
}

void register_polygon_and_cull() {
    struct Mode {
        const char* name;
        VkPolygonMode mode;
        const char* vertex;
    };
    for (const Mode& entry : {Mode{"fill", VK_POLYGON_MODE_FILL, "mesh.vert"}, Mode{"line", VK_POLYGON_MODE_LINE, "mesh.vert"},
                              Mode{"point", VK_POLYGON_MODE_POINT, "mesh_p.vert"}}) {
        CaseSpec spec = spec_of(std::string("polygon_") + entry.name);
        if (entry.mode != VK_POLYGON_MODE_FILL) {
            spec.feature("fillModeNonSolid");
        }
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            const CubeMesh mesh = make_cube(rig);
            const VkPipelineLayout layout = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
            PipelineDesc pipe = cube_pipe(rig, target, layout, entry.vertex);
            pipe.polygon = entry.mode;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", draw_cube(rig, target, pipeline, layout, mesh, cube_transform()));
        });
    }
    struct Cull {
        const char* name;
        VkCullModeFlags mode;
    };
    for (const Cull& cull : {Cull{"none", VK_CULL_MODE_NONE}, Cull{"front", VK_CULL_MODE_FRONT_BIT}, Cull{"back", VK_CULL_MODE_BACK_BIT},
                             Cull{"front_and_back", VK_CULL_MODE_FRONT_AND_BACK}}) {
        for (VkFrontFace front : {VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FRONT_FACE_CLOCKWISE}) {
            CaseSpec spec = spec_of(std::string("cull_") + cull.name + (front == VK_FRONT_FACE_CLOCKWISE ? "_cw" : "_ccw"));
            spec.uniform_ok();
            Cases::add(std::move(spec), [cull, front](Rig& rig, CaptureSet& out) {
                TargetDesc td;
                td.depth = VK_FORMAT_D32_SFLOAT;
                Target& target = rig.make_target(td);
                std::copy(kClear, kClear + 4, target.clear_color);
                const CubeMesh mesh = make_cube(rig);
                const VkPipelineLayout layout = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
                PipelineDesc pipe = cube_pipe(rig, target, layout);
                pipe.cull = cull.mode;
                pipe.front = front;
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", draw_cube(rig, target, pipeline, layout, mesh, cube_transform(0.9f, -0.4f)));
            });
        }
    }
}

void register_depth_bias() {
    struct Bias {
        const char* name;
        float constant;
        float clamp;
        float slope;
        bool needs_clamp;
        float z1;
        float z2;
    };
    for (const Bias& entry : {Bias{"constant", -300000.0f, 0.0f, 0.0f, false, 0.0f, 0.0f}, Bias{"slope", 0.0f, 0.0f, -6.0f, false, 0.0f, 0.0f},
                              Bias{"clamped", -300000.0f, -0.00002f, -2.0f, true, 0.0f, 0.0f}, Bias{"positive", 60000.0f, 0.0f, 0.0f, false, -0.004f, -0.0003f}}) {
        CaseSpec spec = spec_of(std::string("depth_bias_") + entry.name);
        if (entry.needs_clamp) {
            spec.feature("depthBiasClamp");
        }
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc base = quads.desc(target);
            base.depth_test = true;
            PipelineDesc biased = base;
            biased.depth_bias = true;
            biased.bias_constant = entry.constant;
            biased.bias_clamp = entry.clamp;
            biased.bias_slope = entry.slope;
            const VkPipeline p_base = rig.pipe(base);
            const VkPipeline p_bias = rig.pipe(biased);
            out.add("", rig.draw(target, p_base, nullptr, [&](VkCommandBuffer cb) {
                quads.quad(cb, -1, -1, 2, 2, palette(0), 0.4f, 0.3f);
                rig.bind_graphics(cb, p_bias);
                quads.quad(cb, -0.8f, -0.8f, 0.7f, 1.6f, palette(1), 0.4f + 0.3f * 0.1f + entry.z1, 0.3f * 0.35f);
                quads.quad(cb, 0.1f, -0.8f, 0.7f, 1.6f, palette(2), 0.4f + 0.3f * 0.55f + entry.z2, 0.3f * 0.35f);
            }, quads.layout));
        });
    }
}

void register_depth_range() {
    for (int clamp = 0; clamp < 2; ++clamp) {
        CaseSpec spec = spec_of(clamp != 0 ? "depth_clamp_on" : "depth_clamp_off");
        spec.feature("depthClamp");
        Cases::add(std::move(spec), [clamp](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc pipe = quads.desc(target);
            pipe.depth_test = true;
            pipe.depth_clamp = clamp != 0;
            const VkPipeline pipeline = rig.pipe(pipe);
            Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                quads.quad(cb, -1, -1, 2, 2, palette(3), 0.3f, 0.0f);
                quads.quad(cb, -0.9f, -0.9f, 1.0f, 1.8f, palette(0), -0.3f, 1.6f);
                quads.quad(cb, 0.0f, -0.9f, 0.9f, 1.8f, palette(2), 1.4f, -1.2f);
            }, quads.layout);
            out.add("color", std::move(color));
            Image depth;
            std::string error;
            rig.must(target.read_depth(rig.kit, depth, error), error);
            out.add("depth", std::move(depth));
        });
    }
    CaseSpec clip = spec_of("depth_clip_disabled");
    clip.extension("VK_EXT_depth_clip_enable").feature("depthClamp");
    Cases::add(
        std::move(clip),
        [](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc pipe = quads.desc(target);
            pipe.depth_test = true;
            pipe.depth_clamp = true;
            VkPipelineRasterizationDepthClipStateCreateInfoEXT clip_info{};
            clip_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
            clip_info.depthClipEnable = VK_FALSE;
            pipe.raster_next = &clip_info;
            const VkPipeline pipeline = rig.pipe(pipe);
            Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                quads.quad(cb, -1, -1, 2, 2, palette(3), 0.3f, 0.0f);
                quads.quad(cb, -0.9f, -0.9f, 1.0f, 1.8f, palette(0), -0.3f, 1.6f);
                quads.quad(cb, 0.0f, -0.9f, 0.9f, 1.8f, palette(2), 1.4f, -1.2f);
            }, quads.layout);
            out.add("color", std::move(color));
            Image depth;
            std::string error;
            rig.must(target.read_depth(rig.kit, depth, error), error);
            out.add("depth", std::move(depth));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceDepthClipEnableFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT);
            return f.depthClipEnable != VK_FALSE ? std::string() : std::string("depthClipEnable");
        });
    CaseSpec unrestricted = spec_of("depth_range_unrestricted");
    unrestricted.extension("VK_EXT_depth_range_unrestricted");
    Cases::add(std::move(unrestricted), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_test = true;
        pipe.depth_op = VK_COMPARE_OP_ALWAYS;
        const VkPipeline pipeline = rig.pipe(pipe);
        Image color = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.viewport(cb, 0, 0, 128, 64, -2.0f, 3.0f);
            quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(1), 0.5f);
            rig.viewport(cb, 0, 64, 128, 64, 0.2f, 0.8f);
            quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(4), 0.5f);
        }, quads.layout);
        out.add("color", std::move(color));
        Image depth;
        std::string error;
        rig.must(target.read_depth(rig.kit, depth, error), error);
        out.add("depth", std::move(depth));
    });
}

void register_lines_and_points() {
    for (float width : {1.0f, 3.0f, 8.0f}) {
        CaseSpec spec = spec_of("line_width_" + std::to_string(static_cast<int>(width)));
        if (width > 1.0f) {
            spec.feature("wideLines");
        }
        Cases::add(
            std::move(spec),
            [width](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                std::copy(kClear, kClear + 4, target.clear_color);
                std::vector<Vertex2> v;
                for (int i = 0; i < 9; ++i) {
                    const float a = 6.2831853f * static_cast<float>(i) / 9.0f;
                    v.push_back(vertex(0.1f * std::cos(a), 0.1f * std::sin(a), static_cast<unsigned>(i)));
                    v.push_back(vertex(0.8f * std::cos(a + 0.3f), 0.8f * std::sin(a + 0.3f), static_cast<unsigned>(i + 2)));
                }
                const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                PipelineDesc pipe = rig.pos2c(target);
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                pipe.line_width = width;
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 18, 1, 0, 0); }));
            },
            [width](Kit& kit) -> std::string { return kit.properties.limits.lineWidthRange[1] < width ? "lineWidthRange" : std::string(); });
    }
    struct LineMode {
        const char* name;
        VkLineRasterizationModeEXT mode;
        bool stipple;
    };
    for (const LineMode& entry : {LineMode{"rectangular", VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT, false},
                                  LineMode{"bresenham", VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT, false},
                                  LineMode{"rectangular_smooth", VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT, false},
                                  LineMode{"rectangular_stippled", VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT, true},
                                  LineMode{"bresenham_stippled", VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT, true}}) {
        CaseSpec spec = spec_of(std::string("line_mode_") + entry.name);
        spec.extension("VK_EXT_line_rasterization").tolerance(Tolerance::raster(2, 1));
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                std::copy(kClear, kClear + 4, target.clear_color);
                std::vector<Vertex2> v;
                for (int i = 0; i < 7; ++i) {
                    v.push_back(vertex(-0.85f, -0.8f + 0.26f * static_cast<float>(i), static_cast<unsigned>(i)));
                    v.push_back(vertex(0.85f, -0.7f + 0.22f * static_cast<float>(i), static_cast<unsigned>(i + 1)));
                }
                const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                VkPipelineRasterizationLineStateCreateInfoEXT line{};
                line.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
                line.lineRasterizationMode = entry.mode;
                line.stippledLineEnable = entry.stipple ? VK_TRUE : VK_FALSE;
                line.lineStippleFactor = 3;
                line.lineStipplePattern = 0xAAF0;
                PipelineDesc pipe = rig.pos2c(target);
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                pipe.raster_next = &line;
                pipe.line_width = 1.0f;
                if (entry.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT) {
                    pipe.blend = true;
                }
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 14, 1, 0, 0); }));
            },
            [entry](Kit& kit) -> std::string {
                const auto f = kit.query_features<VkPhysicalDeviceLineRasterizationFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT);
                VkBool32 mode_ok = VK_FALSE;
                switch (entry.mode) {
                    case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT: mode_ok = entry.stipple ? f.stippledRectangularLines : f.rectangularLines; break;
                    case VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT: mode_ok = entry.stipple ? f.stippledBresenhamLines : f.bresenhamLines; break;
                    default: mode_ok = f.smoothLines; break;
                }
                return mode_ok != VK_FALSE ? std::string() : std::string("line rasterization mode feature");
            });
    }
    for (float size : {2.0f, 9.0f, 24.0f}) {
        CaseSpec spec = spec_of("point_size_" + std::to_string(static_cast<int>(size)));
        spec.tolerance(Tolerance::raster(1, 1));
        if (size > 1.0f) {
            spec.feature("largePoints");
        }
        Cases::add(
            std::move(spec),
            [size](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                std::copy(kClear, kClear + 4, target.clear_color);
                std::vector<Vertex2> v;
                for (int y = 0; y < 3; ++y) {
                    for (int x = 0; x < 3; ++x) {
                        v.push_back(vertex(-0.6f + 0.6f * static_cast<float>(x) + 0.013f * static_cast<float>(y), -0.6f + 0.6f * static_cast<float>(y) + 0.021f * static_cast<float>(x), static_cast<unsigned>(x + y)));
                    }
                }
                const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                const VkPipelineLayout layout = rig.playout({}, 16, VK_SHADER_STAGE_VERTEX_BIT);
                PipelineDesc pipe = rig.pos2c(target, layout);
                pipe.vertex = rig.sh("points.vert");
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                    const float constants[4] = {size, 0, 0, 0};
                    rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, constants, 16);
                    rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
                }, layout));
            },
            [size](Kit& kit) -> std::string { return kit.properties.limits.pointSizeRange[1] < size ? "pointSizeRange" : std::string(); });
    }
}

void register_viewports() {
    CaseSpec multi = spec_of("viewports_four");
    multi.feature("multiViewport").min_version(1, 2);
    Cases::add(
        std::move(multi),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const VkPipelineLayout layout = rig.playout({}, 32, VK_SHADER_STAGE_VERTEX_BIT);
            PipelineDesc pipe;
            pipe.vertex = rig.sh("vpindex.v12.vert");
            pipe.fragment = rig.sh("col4.frag");
            pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            pipe.viewport_count = 4;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                const VkViewport viewports[4] = {{0, 0, 64, 64, 0, 1}, {64, 0, 64, 64, 0, 1}, {0, 64, 64, 64, 0, 1}, {64, 64, 64, 64, 0, 1}};
                const VkRect2D scissors[4] = {{{4, 4}, {56, 56}}, {{68, 4}, {50, 56}}, {{4, 68}, {56, 50}}, {{64, 64}, {64, 64}}};
                rig.kit.api.vkCmdSetViewport(cb, 0, 4, viewports);
                rig.kit.api.vkCmdSetScissor(cb, 0, 4, scissors);
                const float quad[8] = {-0.9f, -0.9f, 1.8f, 1.8f, 0.9f, 0.6f, 0.3f, 1.0f};
                rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, quad, 32);
                rig.kit.api.vkCmdDraw(cb, 4, 4, 0, 0);
            }, layout));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            if (kit.properties.limits.maxViewports < 4) {
                return "maxViewports";
            }
            return f.shaderOutputViewportIndex != VK_FALSE ? std::string() : std::string("shaderOutputViewportIndex");
        });
    Cases::add(spec_of("viewport_negative_height"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        const CubeMesh mesh = make_cube(rig);
        const VkPipelineLayout layout = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
        PipelineDesc pipe = cube_pipe(rig, target, layout);
        pipe.cull = VK_CULL_MODE_BACK_BIT;
        pipe.front = VK_FRONT_FACE_CLOCKWISE;
        const VkPipeline pipeline = rig.pipe(pipe);
        const Mat4 m = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, false) * Mat4::translate(0, 0, -5.0f) * Mat4::rotate_y(0.7f) * Mat4::rotate_x(-0.5f);
        out.add("", rig.draw(target, pipeline, &mesh.vertex, [&](VkCommandBuffer cb) {
            rig.viewport(cb, 0, 128, 128, -128);
            rig.kit.api.vkCmdBindIndexBuffer(cb, mesh.index.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, m.m, 64);
            rig.kit.api.vkCmdDrawIndexed(cb, mesh.count, 1, 0, 0, 0);
        }, layout));
    });
    Cases::add(spec_of("scissor_clip_rects"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        const VkPipeline pipeline = rig.pipe(quads.desc(target));
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 8; ++i) {
                rig.scissor(cb, static_cast<int>(i * 13), static_cast<int>(i * 9), 40, 33);
                quads.quad(cb, -1, -1, 2, 2, palette(i));
            }
        }, quads.layout));
    });
}

void register_special() {
    CaseSpec conservative = spec_of("conservative_overestimate");
    conservative.extension("VK_EXT_conservative_rasterization");
    Cases::add(
        std::move(conservative),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            std::vector<Vertex2> v;
            for (int i = 0; i < 6; ++i) {
                const float x = -0.8f + 0.28f * static_cast<float>(i);
                v.push_back(vertex(x, -0.7f, static_cast<unsigned>(i)));
                v.push_back(vertex(x + 0.012f, -0.7f, static_cast<unsigned>(i + 1)));
                v.push_back(vertex(x + 0.006f, 0.7f - 0.2f * static_cast<float>(i), static_cast<unsigned>(i + 2)));
            }
            const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_info{};
            conservative_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
            conservative_info.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
            PipelineDesc pipe = rig.pos2c(target);
            pipe.raster_next = &conservative_info;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 18, 1, 0, 0); }));
        },
        [](Kit& kit) -> std::string {
            const auto p = kit.query_properties<VkPhysicalDeviceConservativeRasterizationPropertiesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT);
            return p.primitiveOverestimationSize >= 0.0f && p.maxExtraPrimitiveOverestimationSize >= 0.0f ? std::string() : std::string("conservative properties");
        });
    for (int last = 0; last < 2; ++last) {
        CaseSpec provoking = spec_of(last != 0 ? "provoking_vertex_last" : "provoking_vertex_first");
        provoking.extension("VK_EXT_provoking_vertex");
        Cases::add(
            std::move(provoking),
            [last](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                std::copy(kClear, kClear + 4, target.clear_color);
                const std::vector<Vertex2> v = {vertex(-0.9f, -0.9f, 0), vertex(-0.1f, -0.8f, 1), vertex(-0.5f, -0.1f, 2),
                                                vertex(0.1f, 0.1f, 3),   vertex(0.9f, 0.2f, 4),   vertex(0.5f, 0.9f, 5),
                                                vertex(-0.9f, 0.1f, 6),  vertex(-0.2f, 0.2f, 7),  vertex(-0.5f, 0.9f, 0)};
                const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                VkPipelineRasterizationProvokingVertexStateCreateInfoEXT provoke{};
                provoke.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT;
                provoke.provokingVertexMode = last != 0 ? VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT : VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT;
                PipelineDesc pipe = rig.pos2c(target);
                pipe.vertex = rig.sh("posf.vert");
                pipe.fragment = rig.sh("colf.frag");
                pipe.raster_next = &provoke;
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0); }));
            },
            [last](Kit& kit) -> std::string {
                const auto f = kit.query_features<VkPhysicalDeviceProvokingVertexFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT);
                if (last != 0 && f.provokingVertexLast == VK_FALSE) {
                    return "provokingVertexLast";
                }
                return {};
            });
    }
    Cases::add(spec_of("top_left_rule").tolerance(Tolerance::exact()), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const float px = 2.0f / 128.0f;
        std::vector<Vertex2> v;
        for (int i = 0; i < 6; ++i) {
            const float x0 = -1.0f + px * (8.0f + 20.0f * static_cast<float>(i % 3));
            const float y0 = -1.0f + px * (8.0f + 60.0f * static_cast<float>(i / 3));
            const float s = px * (16.0f + 4.0f * static_cast<float>(i));
            v.push_back(vertex(x0, y0, 0));
            v.push_back(vertex(x0 + s, y0, 0));
            v.push_back(vertex(x0 + s, y0 + s, 0));
            v.push_back(vertex(x0, y0, 0));
            v.push_back(vertex(x0 + s, y0 + s, 0));
            v.push_back(vertex(x0, y0 + s, 0));
        }
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.fragment = rig.sh("addtint.frag");
        pipe.blend = true;
        pipe.src_color = VK_BLEND_FACTOR_ONE;
        pipe.dst_color = VK_BLEND_FACTOR_ONE;
        pipe.src_alpha = VK_BLEND_FACTOR_ZERO;
        pipe.dst_alpha = VK_BLEND_FACTOR_ONE;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 36, 1, 0, 0); }));
    });
}

const int registered = (register_polygon_and_cull(), register_depth_bias(), register_depth_range(), register_lines_and_points(),
                        register_viewports(), register_special(), 0);

}  // namespace

}  // namespace lsw::render::vk
