#include "render/common/pattern.hpp"
#include "render/vk/formatdata.hpp"
#include "render/vk/rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

std::vector<FormatEntry> format_table() {
    using K = Kind;
    return {
        {"r8_unorm", VK_FORMAT_R8_UNORM, 'f', 1, K::Unorm, {8, 0, 0, 0}},
        {"r8_snorm", VK_FORMAT_R8_SNORM, 'f', 1, K::Snorm, {8, 0, 0, 0}},
        {"r8_uscaled", VK_FORMAT_R8_USCALED, 'f', 1, K::Uscaled, {8, 0, 0, 0}},
        {"r8_sscaled", VK_FORMAT_R8_SSCALED, 'f', 1, K::Sscaled, {8, 0, 0, 0}},
        {"r8_uint", VK_FORMAT_R8_UINT, 'u', 1, K::Uint, {8, 0, 0, 0}},
        {"r8_sint", VK_FORMAT_R8_SINT, 'i', 1, K::Sint, {8, 0, 0, 0}},
        {"r8g8_unorm", VK_FORMAT_R8G8_UNORM, 'f', 2, K::Unorm, {8, 8, 0, 0}},
        {"r8g8_snorm", VK_FORMAT_R8G8_SNORM, 'f', 2, K::Snorm, {8, 8, 0, 0}},
        {"r8g8_uint", VK_FORMAT_R8G8_UINT, 'u', 2, K::Uint, {8, 8, 0, 0}},
        {"r8g8b8a8_unorm", VK_FORMAT_R8G8B8A8_UNORM, 'f', 4, K::Unorm, {8, 8, 8, 8}},
        {"b8g8r8a8_unorm", VK_FORMAT_B8G8R8A8_UNORM, 'f', 4, K::Unorm, {8, 8, 8, 8}},
        {"r8g8b8a8_snorm", VK_FORMAT_R8G8B8A8_SNORM, 'f', 4, K::Snorm, {8, 8, 8, 8}},
        {"r8g8b8a8_uscaled", VK_FORMAT_R8G8B8A8_USCALED, 'f', 4, K::Uscaled, {8, 8, 8, 8}},
        {"r8g8b8a8_sscaled", VK_FORMAT_R8G8B8A8_SSCALED, 'f', 4, K::Sscaled, {8, 8, 8, 8}},
        {"r8g8b8a8_uint", VK_FORMAT_R8G8B8A8_UINT, 'u', 4, K::Uint, {8, 8, 8, 8}},
        {"r8g8b8a8_sint", VK_FORMAT_R8G8B8A8_SINT, 'i', 4, K::Sint, {8, 8, 8, 8}},
        {"a8b8g8r8_unorm_pack32", VK_FORMAT_A8B8G8R8_UNORM_PACK32, 'f', 4, K::Unorm, {8, 8, 8, 8}},
        {"a8b8g8r8_snorm_pack32", VK_FORMAT_A8B8G8R8_SNORM_PACK32, 'f', 4, K::Snorm, {8, 8, 8, 8}},
        {"a8b8g8r8_uint_pack32", VK_FORMAT_A8B8G8R8_UINT_PACK32, 'u', 4, K::Uint, {8, 8, 8, 8}},
        {"a8b8g8r8_sint_pack32", VK_FORMAT_A8B8G8R8_SINT_PACK32, 'i', 4, K::Sint, {8, 8, 8, 8}},
        {"r16_unorm", VK_FORMAT_R16_UNORM, 'f', 2, K::Unorm, {16, 0, 0, 0}},
        {"r16g16_unorm", VK_FORMAT_R16G16_UNORM, 'f', 4, K::Unorm, {16, 16, 0, 0}},
        {"r16g16b16a16_unorm", VK_FORMAT_R16G16B16A16_UNORM, 'f', 8, K::Unorm, {16, 16, 16, 16}},
        {"r16g16_snorm", VK_FORMAT_R16G16_SNORM, 'f', 4, K::Snorm, {16, 16, 0, 0}},
        {"r16g16b16a16_snorm", VK_FORMAT_R16G16B16A16_SNORM, 'f', 8, K::Snorm, {16, 16, 16, 16}},
        {"r16g16_uscaled", VK_FORMAT_R16G16_USCALED, 'f', 4, K::Uscaled, {16, 16, 0, 0}},
        {"r16g16b16a16_sscaled", VK_FORMAT_R16G16B16A16_SSCALED, 'f', 8, K::Sscaled, {16, 16, 16, 16}},
        {"r16g16b16a16_uint", VK_FORMAT_R16G16B16A16_UINT, 'u', 8, K::Uint, {16, 16, 16, 16}},
        {"r16g16_sint", VK_FORMAT_R16G16_SINT, 'i', 4, K::Sint, {16, 16, 0, 0}},
        {"r16g16b16a16_sint", VK_FORMAT_R16G16B16A16_SINT, 'i', 8, K::Sint, {16, 16, 16, 16}},
        {"r16_sfloat", VK_FORMAT_R16_SFLOAT, 'f', 2, K::Float, {16, 0, 0, 0}},
        {"r16g16_sfloat", VK_FORMAT_R16G16_SFLOAT, 'f', 4, K::Float, {16, 16, 0, 0}},
        {"r16g16b16a16_sfloat", VK_FORMAT_R16G16B16A16_SFLOAT, 'f', 8, K::Float, {16, 16, 16, 16}},
        {"r32_uint", VK_FORMAT_R32_UINT, 'u', 4, K::Uint, {32, 0, 0, 0}},
        {"r32g32_uint", VK_FORMAT_R32G32_UINT, 'u', 8, K::Uint, {32, 32, 0, 0}},
        {"r32g32b32_uint", VK_FORMAT_R32G32B32_UINT, 'u', 12, K::Uint, {32, 32, 32, 0}},
        {"r32g32b32a32_uint", VK_FORMAT_R32G32B32A32_UINT, 'u', 16, K::Uint, {32, 32, 32, 32}},
        {"r32_sint", VK_FORMAT_R32_SINT, 'i', 4, K::Sint, {32, 0, 0, 0}},
        {"r32g32b32_sint", VK_FORMAT_R32G32B32_SINT, 'i', 12, K::Sint, {32, 32, 32, 0}},
        {"r32g32b32a32_sint", VK_FORMAT_R32G32B32A32_SINT, 'i', 16, K::Sint, {32, 32, 32, 32}},
        {"r32_sfloat", VK_FORMAT_R32_SFLOAT, 'f', 4, K::Float, {32, 0, 0, 0}},
        {"r32g32_sfloat", VK_FORMAT_R32G32_SFLOAT, 'f', 8, K::Float, {32, 32, 0, 0}},
        {"r32g32b32_sfloat", VK_FORMAT_R32G32B32_SFLOAT, 'f', 12, K::Float, {32, 32, 32, 0}},
        {"r32g32b32a32_sfloat", VK_FORMAT_R32G32B32A32_SFLOAT, 'f', 16, K::Float, {32, 32, 32, 32}},
        {"a2b10g10r10_unorm", VK_FORMAT_A2B10G10R10_UNORM_PACK32, 'f', 4, K::Unorm, {10, 10, 10, 2}},
        {"a2b10g10r10_snorm", VK_FORMAT_A2B10G10R10_SNORM_PACK32, 'f', 4, K::Snorm, {10, 10, 10, 2}},
        {"a2b10g10r10_uscaled", VK_FORMAT_A2B10G10R10_USCALED_PACK32, 'f', 4, K::Uscaled, {10, 10, 10, 2}},
        {"a2b10g10r10_sscaled", VK_FORMAT_A2B10G10R10_SSCALED_PACK32, 'f', 4, K::Sscaled, {10, 10, 10, 2}},
        {"a2b10g10r10_uint", VK_FORMAT_A2B10G10R10_UINT_PACK32, 'u', 4, K::Uint, {10, 10, 10, 2}},
        {"a2b10g10r10_sint", VK_FORMAT_A2B10G10R10_SINT_PACK32, 'i', 4, K::Sint, {10, 10, 10, 2}},
        {"a2r10g10b10_unorm", VK_FORMAT_A2R10G10B10_UNORM_PACK32, 'f', 4, K::Unorm, {10, 10, 10, 2}},
        {"a2r10g10b10_uint", VK_FORMAT_A2R10G10B10_UINT_PACK32, 'u', 4, K::Uint, {10, 10, 10, 2}},
    };
}

void register_vertex_formats() {
    for (const FormatEntry& entry : format_table()) {
        CaseSpec spec("pipe", std::string("vin_") + entry.name);
        spec.tolerance(Tolerance::exact());
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
                const std::vector<std::uint8_t> attrs = make_format_bytes(entry, count);
                const Buffer positions = rig.buffer(grid, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                const Buffer values = rig.buffer(attrs.data(), attrs.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                struct Constants {
                    float scale[4];
                    float bias[4];
                } constants{};
                make_scale(entry, constants.scale, constants.bias);
                const VkPipelineLayout layout = rig.playout({}, sizeof constants, VK_SHADER_STAGE_VERTEX_BIT);
                PipelineDesc pipe = rig.pos2c(target, layout);
                const char shader[] = {'v', 'i', 'n', '_', entry.cls, '.', 'v', 'e', 'r', 't', 0};
                pipe.vertex = rig.sh(shader);
                pipe.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                pipe.bindings = {{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX}, {1, entry.bytes, VK_VERTEX_INPUT_RATE_VERTEX}};
                pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, entry.format, 0}};
                const VkPipeline pipeline = rig.pipe(pipe);
                out.add("", rig.draw(
                                target, pipeline, &positions,
                                [&](VkCommandBuffer cb) {
                                    rig.bind_vertex(cb, values, 0, 1);
                                    rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &constants, sizeof constants);
                                    rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0);
                                },
                                layout));
            },
            [entry](Kit& kit) { return Cases::need_buffer_format(kit, entry.format, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "vertex buffer format"); });
    }
}

struct Instanced {
    float offset[2];
    float scale;
    float color[3];
};

void register_vertex_layouts() {
    Cases::add(CaseSpec("pipe", "vin_multi_binding").tolerance(Tolerance::exact()), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<float> positions = {-0.6f, -0.6f, 0.6f, -0.6f, 0.0f, 0.6f, -0.2f, 0.1f, 0.5f, 0.2f, 0.1f, 0.7f};
        const std::vector<float> colors = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1};
        const std::vector<std::uint8_t> shade = {255, 128, 255, 64, 255, 192};
        const Buffer b0 = rig.buffer(positions, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer b1 = rig.buffer(colors, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer b2 = rig.buffer(shade.data(), shade.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const char* name = "multi.vert";
        PipelineDesc pipe = rig.pos2c(target);
        pipe.vertex = rig.sh(name);
        pipe.bindings = {{0, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 12, VK_VERTEX_INPUT_RATE_VERTEX}, {2, 1, VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 2, VK_FORMAT_R8_UNORM, 0}};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &b0, [&](VkCommandBuffer cb) {
            rig.bind_vertex(cb, b1, 0, 1);
            rig.bind_vertex(cb, b2, 0, 2);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            rig.bind_vertex(cb, b0, 24, 0);
            rig.bind_vertex(cb, b1, 36, 1);
            rig.bind_vertex(cb, b2, 3, 2);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }));
    });
    Cases::add(CaseSpec("pipe", "vin_instance_rate").tolerance(Tolerance::exact()), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<float> tri = {0.0f, 0.12f, -0.1f, -0.1f, 0.1f, -0.1f};
        std::vector<Instanced> inst;
        for (int i = 0; i < 25; ++i) {
            const float a = static_cast<float>(i);
            inst.push_back({{-0.8f + 0.4f * static_cast<float>(i % 5), -0.8f + 0.4f * static_cast<float>(i / 5)},
                            0.5f + 0.15f * static_cast<float>((i * 7) % 5),
                            {0.2f + 0.8f * std::fabs(std::sin(a)), 0.2f + 0.8f * std::fabs(std::cos(a * 0.7f)), 0.5f}});
        }
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer ib = rig.buffer(inst, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.vertex = rig.sh("inst.vert");
        pipe.bindings = {{0, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(Instanced), VK_VERTEX_INPUT_RATE_INSTANCE}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                           {1, 1, VK_FORMAT_R32G32_SFLOAT, 0},
                           {2, 1, VK_FORMAT_R32_SFLOAT, 8},
                           {3, 1, VK_FORMAT_R32G32B32_SFLOAT, 12}};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.bind_vertex(cb, ib, 0, 1);
            rig.kit.api.vkCmdDraw(cb, 3, 25, 0, 0);
        }));
    });
    CaseSpec divisor("pipe", "vin_instance_divisor");
    divisor.extension("VK_EXT_vertex_attribute_divisor").tolerance(Tolerance::exact());
    Cases::add(
        std::move(divisor),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            const std::vector<float> tri = {0.0f, 0.3f, -0.25f, -0.25f, 0.25f, -0.25f};
            std::vector<Instanced> inst;
            for (int i = 0; i < 9; ++i) {
                inst.push_back({{-0.7f + 0.7f * static_cast<float>(i % 3), -0.7f + 0.7f * static_cast<float>(i / 3)},
                                0.8f + 0.1f * static_cast<float>(i % 3),
                                {0.3f + 0.1f * static_cast<float>(i), 0.9f - 0.08f * static_cast<float>(i), 0.4f}});
            }
            const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer ib = rig.buffer(inst, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            VkVertexInputBindingDivisorDescriptionEXT div{1, 3};
            VkPipelineVertexInputDivisorStateCreateInfoEXT div_info{};
            div_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
            div_info.vertexBindingDivisorCount = 1;
            div_info.pVertexBindingDivisors = &div;
            PipelineDesc pipe = rig.pos2c(target);
            pipe.vertex = rig.sh("inst.vert");
            pipe.bindings = {{0, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(Instanced), VK_VERTEX_INPUT_RATE_INSTANCE}};
            pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                               {1, 1, VK_FORMAT_R32G32_SFLOAT, 0},
                               {2, 1, VK_FORMAT_R32_SFLOAT, 8},
                               {3, 1, VK_FORMAT_R32G32B32_SFLOAT, 12}};
            pipe.vertex_next = &div_info;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                rig.bind_vertex(cb, ib, 0, 1);
                rig.kit.api.vkCmdDraw(cb, 3, 9, 0, 0);
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT);
            return f.vertexAttributeInstanceRateDivisor != VK_FALSE ? std::string() : std::string("vertexAttributeInstanceRateDivisor");
        });
    CaseSpec dynamic("pipe", "vin_dynamic_state");
    dynamic.extension("VK_EXT_vertex_input_dynamic_state").tolerance(Tolerance::exact());
    Cases::add(
        std::move(dynamic),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            const std::vector<Vertex2> a = {vertex(-0.8f, -0.8f, 0), vertex(-0.1f, -0.8f, 1), vertex(-0.45f, -0.1f, 2)};
            const std::vector<float> b_pos = {0.1f, 0.1f, 0.9f, 0.1f, 0.5f, 0.8f};
            const std::vector<std::uint8_t> b_col = {255, 64, 64, 255, 64, 255, 64, 255, 64, 64, 255, 255};
            const Buffer va = rig.buffer(a, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer vb = rig.buffer(b_pos, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer vc = rig.buffer(b_col.data(), b_col.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.dynamic = {VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                VkVertexInputBindingDescription2EXT bind0{};
                bind0.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
                bind0.binding = 0;
                bind0.stride = sizeof(Vertex2);
                bind0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                bind0.divisor = 1;
                VkVertexInputAttributeDescription2EXT attr0[2]{};
                attr0[0] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                attr0[1] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8};
                rig.kit.api.vkCmdSetVertexInputEXT(cb, 1, &bind0, 2, attr0);
                rig.bind_vertex(cb, va);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                VkVertexInputBindingDescription2EXT bind1[2]{};
                for (int i = 0; i < 2; ++i) {
                    bind1[i].sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
                    bind1[i].binding = static_cast<std::uint32_t>(i);
                    bind1[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                    bind1[i].divisor = 1;
                }
                bind1[0].stride = 8;
                bind1[1].stride = 4;
                VkVertexInputAttributeDescription2EXT attr1[2]{};
                attr1[0] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                attr1[1] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, 0};
                rig.kit.api.vkCmdSetVertexInputEXT(cb, 2, bind1, 2, attr1);
                rig.bind_vertex(cb, vb, 0, 0);
                rig.bind_vertex(cb, vc, 0, 1);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT);
            return f.vertexInputDynamicState != VK_FALSE ? std::string() : std::string("vertexInputDynamicState");
        });
    Cases::add(CaseSpec("pipe", "vin_stride_zero").tolerance(Tolerance::exact()), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::vector<float> pos = {-0.7f, -0.7f, 0.0f, -0.7f, 0.7f, -0.7f, -0.7f, 0.1f, 0.0f, 0.1f, 0.7f, 0.1f, -0.2f, 0.4f, 0.2f, 0.9f, 0.6f, 0.4f};
        const std::vector<float> color = {0.2f, 0.9f, 0.5f, 1.0f};
        const Buffer vb = rig.buffer(pos, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const Buffer cb_buf = rig.buffer(color, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.bindings = {{0, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 0, VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.bind_vertex(cb, cb_buf, 0, 1);
            rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
        }));
    });
}

const int registered = (register_vertex_formats(), register_vertex_layouts(), 0);

}  // namespace

}  // namespace lsw::render::vk
