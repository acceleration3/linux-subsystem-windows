#include "render/vk/rig.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

struct R64Entry {
    const char* name;
    VkFormat format;
    unsigned components;
    char kind;
};

std::vector<std::uint8_t> r64_bytes(const R64Entry& entry, std::uint32_t count) {
    std::vector<std::uint8_t> out;
    for (std::uint32_t p = 0; p < count; ++p) {
        if (entry.kind == 'f') {
            for (unsigned c = 0; c < entry.components; ++c) {
                const std::uint32_t k = (p * (1 + 2 * c) + c * 3 + (c == 0 ? 0 : 1)) % 16;
                const double value = 0.5 + static_cast<double>(k) / 1099511627776.0;
                std::uint8_t raw[8];
                std::memcpy(raw, &value, 8);
                out.insert(out.end(), raw, raw + 8);
            }
        } else {
            const std::uint64_t hi = p % 16;
            const std::uint64_t lo = (p * 3 + 1) % 16;
            const std::uint64_t mid = (p * 5) % 16;
            const std::uint64_t bit32 = p & 1;
            const std::uint64_t low = (bit32 << 32) | (1ull << 31) | (mid << 8) | lo;
            std::uint64_t value = 0;
            if (entry.kind == 'u') {
                value = (hi << 33) | low;
            } else {
                value = static_cast<std::uint64_t>(-static_cast<std::int64_t>((hi + 1) << 33)) + low;
            }
            std::uint8_t raw[8];
            std::memcpy(raw, &value, 8);
            out.insert(out.end(), raw, raw + 8);
        }
    }
    return out;
}

void register_r64() {
    const std::vector<R64Entry> entries = {
        {"r64_sfloat", VK_FORMAT_R64_SFLOAT, 1, 'f'},
        {"r64g64_sfloat", VK_FORMAT_R64G64_SFLOAT, 2, 'f'},
        {"r64g64b64_sfloat", VK_FORMAT_R64G64B64_SFLOAT, 3, 'f'},
        {"r64g64b64a64_sfloat", VK_FORMAT_R64G64B64A64_SFLOAT, 4, 'f'},
        {"r64_uint", VK_FORMAT_R64_UINT, 1, 'u'},
        {"r64_sint", VK_FORMAT_R64_SINT, 1, 'i'},
    };
    for (const R64Entry& entry : entries) {
        CaseSpec spec("pipe", std::string("vin_") + entry.name);
        spec.tolerance(Tolerance::exact()).feature(entry.kind == 'f' ? "shaderFloat64" : "shaderInt64");
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                Target& target = rig.make_target(TargetDesc{});
                target.clear_color[0] = 0.05f;
                target.clear_color[1] = 0.05f;
                target.clear_color[2] = 0.05f;
                std::vector<Vertex2> grid;
                for (int y = 0; y < 7; ++y) {
                    for (int x = 0; x < 7; ++x) {
                        grid.push_back(vertex(-0.84f + 0.28f * static_cast<float>(x), -0.84f + 0.28f * static_cast<float>(y), 0));
                    }
                }
                const auto count = static_cast<std::uint32_t>(grid.size());
                const std::vector<std::uint8_t> attrs = r64_bytes(entry, count);
                const Buffer positions = rig.buffer(grid, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                const Buffer values = rig.buffer(attrs.data(), attrs.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                const std::uint32_t stride = 8 * (entry.kind == 'f' ? entry.components : 1);
                PipelineDesc pipe = rig.pos2c(target);
                const char* shader = entry.kind == 'u' ? "vin_u64.vert" : entry.kind == 'i' ? "vin_i64.vert"
                                     : entry.components == 1 ? "vin_d1.vert" : entry.components == 2 ? "vin_d2.vert"
                                     : entry.components == 3 ? "vin_d3.vert" : "vin_d4.vert";
                pipe.vertex = rig.sh(shader);
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                pipe.bindings = {{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX}, {1, stride, VK_VERTEX_INPUT_RATE_VERTEX}};
                pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, entry.format, 0}};
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(target, pipeline, &positions, [&](VkCommandBuffer cb) {
                    rig.bind_vertex(cb, values, 0, 1);
                    rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0);
                }));
            },
            [entry](Kit& kit) { return Cases::need_buffer_format(kit, entry.format, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "vertex buffer format"); });
    }
}

const int registered = (register_r64(), 0);

}  // namespace

}  // namespace lsw::render::vk
