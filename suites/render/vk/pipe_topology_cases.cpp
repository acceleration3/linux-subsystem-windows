#include "render/vk/rig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

constexpr float kPi = 3.14159265f;

std::vector<Vertex2> point_grid() {
    std::vector<Vertex2> v;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            v.push_back(vertex(-0.84f + 0.28f * static_cast<float>(x), -0.84f + 0.28f * static_cast<float>(y), static_cast<unsigned>(x + y * 3)));
        }
    }
    return v;
}

std::vector<Vertex2> spokes() {
    std::vector<Vertex2> v;
    for (int i = 0; i < 12; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / 12.0f;
        v.push_back(vertex(0.15f * std::cos(a), 0.15f * std::sin(a), static_cast<unsigned>(i)));
        v.push_back(vertex(0.85f * std::cos(a), 0.85f * std::sin(a), static_cast<unsigned>(i + 3)));
    }
    return v;
}

std::vector<Vertex2> zigzag(int count) {
    std::vector<Vertex2> v;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        v.push_back(vertex(-0.85f + 1.7f * t, (i % 2 == 0 ? -0.55f : 0.55f) * (0.4f + 0.6f * t), static_cast<unsigned>(i)));
    }
    return v;
}

std::vector<Vertex2> tri_quadrants() {
    std::vector<Vertex2> v;
    const float cx[4] = {-0.5f, 0.5f, -0.5f, 0.5f};
    const float cy[4] = {-0.5f, -0.5f, 0.5f, 0.5f};
    for (int q = 0; q < 4; ++q) {
        const float s = 0.18f + 0.06f * static_cast<float>(q);
        v.push_back(vertex(cx[q] - s, cy[q] - s, static_cast<unsigned>(q * 2)));
        v.push_back(vertex(cx[q] + s, cy[q] - s * 0.8f, static_cast<unsigned>(q * 2 + 1)));
        v.push_back(vertex(cx[q], cy[q] + s, static_cast<unsigned>(q * 2 + 2)));
    }
    return v;
}

std::vector<Vertex2> strip_ribbon(int pairs) {
    std::vector<Vertex2> v;
    for (int i = 0; i < pairs; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(pairs - 1);
        const float x = -0.85f + 1.7f * t;
        const float c = 0.5f * std::sin(t * 2.0f * kPi);
        v.push_back(vertex(x, c - 0.25f, static_cast<unsigned>(i * 2)));
        v.push_back(vertex(x, c + 0.25f, static_cast<unsigned>(i * 2 + 1)));
    }
    return v;
}

std::vector<Vertex2> fan_arc() {
    std::vector<Vertex2> v;
    v.push_back(vertex(-0.2f, -0.1f, 7));
    for (int i = 0; i < 9; ++i) {
        const float a = -0.4f * kPi + 1.7f * kPi * static_cast<float>(i) / 8.0f;
        v.push_back(vertex(-0.2f + 0.75f * std::cos(a), -0.1f + 0.75f * std::sin(a), static_cast<unsigned>(i)));
    }
    return v;
}

std::vector<Vertex2> line_adjacency() {
    std::vector<Vertex2> v;
    const float y[3] = {-0.55f, 0.0f, 0.55f};
    for (int l = 0; l < 3; ++l) {
        v.push_back(vertex(-0.95f, y[l] - 0.2f, static_cast<unsigned>(l)));
        v.push_back(vertex(-0.7f, y[l], static_cast<unsigned>(l + 1)));
        v.push_back(vertex(0.7f, y[l] + 0.1f, static_cast<unsigned>(l + 2)));
        v.push_back(vertex(0.95f, y[l] + 0.4f, static_cast<unsigned>(l + 3)));
    }
    return v;
}

std::vector<Vertex2> strip_adjacency_lines() {
    std::vector<Vertex2> v;
    for (int i = 0; i < 9; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        v.push_back(vertex(-0.85f + 1.7f * t, 0.6f * std::sin(t * 2.0f * kPi), static_cast<unsigned>(i)));
    }
    return v;
}

std::vector<Vertex2> tri_adjacency() {
    std::vector<Vertex2> v;
    const float cx[2] = {-0.45f, 0.45f};
    for (int t = 0; t < 2; ++t) {
        const float x = cx[t];
        v.push_back(vertex(x - 0.35f, -0.4f, 0));
        v.push_back(vertex(x - 0.5f, 0.1f, 3));
        v.push_back(vertex(x + 0.35f, -0.4f, 1));
        v.push_back(vertex(x + 0.6f, 0.0f, 4));
        v.push_back(vertex(x, 0.55f, 2));
        v.push_back(vertex(x - 0.1f, 0.8f, 5));
    }
    return v;
}

std::vector<Vertex2> strip_adjacency_tris() {
    std::vector<Vertex2> v;
    for (int i = 0; i < 5; ++i) {
        const float x = -0.8f + 0.4f * static_cast<float>(i);
        const float y = (i % 2 == 0) ? -0.4f : 0.4f;
        v.push_back(vertex(x, y, static_cast<unsigned>(i)));
        v.push_back(vertex(x + 0.1f, y * 1.5f, static_cast<unsigned>(i + 2)));
    }
    return v;
}

struct TopologyCase {
    const char* name;
    VkPrimitiveTopology topology;
    std::vector<Vertex2> (*geometry)();
    const char* geometry_shader;
    const char* feature;
};

void register_topologies() {
    const std::vector<TopologyCase> cases = {
        {"topo_point_list", VK_PRIMITIVE_TOPOLOGY_POINT_LIST, point_grid, nullptr, nullptr},
        {"topo_line_list", VK_PRIMITIVE_TOPOLOGY_LINE_LIST, spokes, nullptr, nullptr},
        {"topo_line_strip", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, [] { return zigzag(10); }, nullptr, nullptr},
        {"topo_triangle_list", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, tri_quadrants, nullptr, nullptr},
        {"topo_triangle_strip", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, [] { return strip_ribbon(9); }, nullptr, nullptr},
        {"topo_triangle_fan", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, fan_arc, nullptr, nullptr},
        {"topo_line_list_adjacency", VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY, line_adjacency, "line_adj.geom", "geometryShader"},
        {"topo_line_strip_adjacency", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY, strip_adjacency_lines, "line_adj.geom", "geometryShader"},
        {"topo_triangle_list_adjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY, tri_adjacency, "tri_adj.geom", "geometryShader"},
        {"topo_triangle_strip_adjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY, strip_adjacency_tris, "tri_adj.geom", "geometryShader"},
    };
    for (const TopologyCase& entry : cases) {
        CaseSpec spec("pipe", entry.name);
        spec.tolerance(Tolerance::tight(1));
        if (entry.feature != nullptr) {
            spec.feature(entry.feature);
        }
        Cases::add(std::move(spec), [entry](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            target.clear_color[0] = 0.08f;
            target.clear_color[1] = 0.08f;
            target.clear_color[2] = 0.12f;
            const std::vector<Vertex2> vertices = entry.geometry();
            const Buffer buffer = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.topology = entry.topology;
            if (entry.geometry_shader != nullptr) {
                pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh(entry.geometry_shader), nullptr});
            }
            const VkPipeline pipeline = rig.pipe(pipe);
            const auto count = static_cast<std::uint32_t>(vertices.size());
            out.add("", rig.draw(target, pipeline, &buffer, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0); }));
        });
    }
}

struct PatchCase {
    const char* name;
    unsigned points;
    const char* control;
    const char* evaluation;
};

std::vector<Vertex2> patch_vertices(unsigned points) {
    std::vector<Vertex2> v;
    switch (points) {
        case 1:
            v.push_back(vertex(-0.5f, -0.4f, 0));
            v.push_back(vertex(0.45f, -0.3f, 2));
            v.push_back(vertex(0.0f, 0.5f, 4));
            break;
        case 3:
            v = {vertex(-0.9f, -0.8f, 0), vertex(-0.05f, -0.7f, 1), vertex(-0.5f, 0.0f, 2),
                 vertex(0.1f, 0.0f, 3),   vertex(0.95f, 0.05f, 4),  vertex(0.5f, 0.85f, 5)};
            break;
        case 4:
            v = {vertex(-0.9f, -0.9f, 0), vertex(-0.05f, -0.8f, 1), vertex(-0.1f, -0.1f, 2), vertex(-0.85f, -0.05f, 3),
                 vertex(0.05f, 0.1f, 4),  vertex(0.9f, 0.05f, 5),   vertex(0.85f, 0.85f, 6), vertex(0.1f, 0.9f, 7)};
            break;
        default:
            for (unsigned j = 0; j < 4; ++j) {
                for (unsigned i = 0; i < 4; ++i) {
                    const float x = -0.85f + 0.567f * static_cast<float>(i);
                    const float y = -0.85f + 0.567f * static_cast<float>(j) + ((i == 1 || i == 2) ? 0.25f * (j % 2 == 0 ? 1.0f : -1.0f) : 0.0f);
                    v.push_back(vertex(x, y, i + j * 2));
                }
            }
            if (points == 32) {
                for (unsigned j = 0; j < 4; ++j) {
                    for (unsigned i = 0; i < 4; ++i) {
                        v.push_back(vertex(0.0f, 0.0f, i * 2 + j));
                    }
                }
            }
            break;
    }
    return v;
}

void register_patches() {
    const std::vector<PatchCase> cases = {
        {"topo_patch_1", 1, "patch1.tesc", "iso1.tese"},
        {"topo_patch_3", 3, "patch3.tesc", "tri3.tese"},
        {"topo_patch_4", 4, "patch4.tesc", "quad4.tese"},
        {"topo_patch_16", 16, "patch16.tesc", "bez16.tese"},
        {"topo_patch_32", 32, "patch32.tesc", "bez32.tese"},
    };
    for (const PatchCase& entry : cases) {
        CaseSpec spec("pipe", entry.name);
        spec.feature("tessellationShader").tolerance(Tolerance::tight(1));
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                target.clear_color[0] = 0.06f;
                target.clear_color[1] = 0.07f;
                target.clear_color[2] = 0.1f;
                const std::vector<Vertex2> vertices = patch_vertices(entry.points);
                const Buffer buffer = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                PipelineDesc pipe = rig.pos2c(target);
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
                pipe.patch_points = entry.points;
                pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, rig.sh(entry.control), nullptr});
                pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, rig.sh(entry.evaluation), nullptr});
                if (entry.points == 1) {
                    pipe.polygon = VK_POLYGON_MODE_FILL;
                }
                const VkPipeline pipeline = rig.pipe(pipe);
                const auto count = static_cast<std::uint32_t>(vertices.size());
                out.add("", rig.draw(target, pipeline, &buffer, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0); }));
            },
            [entry](Kit& kit) -> std::string {
                if (kit.properties.limits.maxTessellationPatchSize < entry.points) {
                    return "maxTessellationPatchSize below the patch size";
                }
                return {};
            });
    }
}

template <class Index>
std::vector<Index> restart_strips() {
    constexpr Index restart = static_cast<Index>(~Index(0));
    return {0, 1, 2, 3, 4, 5, restart, 6, 7, 8, 9, restart, 10, 11, 12, 13, 14, 15, 16, 17};
}

std::vector<Vertex2> restart_vertices() {
    std::vector<Vertex2> v;
    for (int s = 0; s < 3; ++s) {
        const float y0 = -0.85f + 0.6f * static_cast<float>(s);
        const int pairs = s == 0 ? 3 : (s == 1 ? 2 : 4);
        for (int i = 0; i < pairs; ++i) {
            const float x = -0.85f + 0.4f * static_cast<float>(i) + 0.2f * static_cast<float>(s);
            v.push_back(vertex(x, y0, static_cast<unsigned>(i + s)));
            v.push_back(vertex(x + 0.05f, y0 + 0.45f, static_cast<unsigned>(i + s + 3)));
        }
    }
    return v;
}

void register_restart() {
    Cases::add(CaseSpec("pipe", "restart_strip_16bit").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<Vertex2> vertices = restart_vertices();
        const std::vector<std::uint16_t> indices = restart_strips<std::uint16_t>();
        const Buffer vb = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer ib = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.primitive_restart = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(indices.size()), 1, 0, 0, 0);
        }));
    });
    Cases::add(CaseSpec("pipe", "restart_strip_32bit").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<Vertex2> vertices = restart_vertices();
        const std::vector<std::uint32_t> indices = restart_strips<std::uint32_t>();
        const Buffer vb = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer ib = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.primitive_restart = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(indices.size()), 1, 0, 0, 0);
        }));
    });
    Cases::add(CaseSpec("pipe", "restart_line_strip").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<Vertex2> vertices = zigzag(12);
        const std::vector<std::uint16_t> indices = {0, 1, 2, 3, 0xFFFF, 4, 5, 6, 7, 0xFFFF, 8, 9, 10, 11};
        const Buffer vb = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer ib = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        pipe.primitive_restart = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(indices.size()), 1, 0, 0, 0);
        }));
    });
    CaseSpec list_spec("pipe", "restart_list_ext");
    list_spec.extension("VK_EXT_primitive_topology_list_restart").tolerance(Tolerance::tight(1));
    Cases::add(
        std::move(list_spec),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            const std::vector<Vertex2> vertices = tri_quadrants();
            const std::vector<std::uint16_t> indices = {0, 1, 2, 0xFFFF, 3, 4, 0xFFFF, 5, 6, 7, 8, 9, 10, 0xFFFF, 11};
            const Buffer vb = rig.buffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer ib = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipe.primitive_restart = true;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT16);
                rig.kit.api.vkCmdDrawIndexed(cb, static_cast<std::uint32_t>(indices.size()), 1, 0, 0, 0);
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT);
            return f.primitiveTopologyListRestart != VK_FALSE ? std::string() : std::string("primitiveTopologyListRestart");
        });
}

const int registered = (register_topologies(), register_patches(), register_restart(), 0);

}  // namespace

}  // namespace lsw::render::vk
