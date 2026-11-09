#include "render/d3d12/gfx.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct VertexFormat {
    const char* name;
    DXGI_FORMAT format;
    UINT size;
    bool integer;
    std::function<void(const float*, std::uint8_t*)> encode;
    std::function<float(std::uint32_t)> sample;
};

std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float bits_to_float(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

float sample_f32(std::uint32_t n) { return bits_to_float(0x3f000000u | (mix32(n) & 0x7fffffu)); }
float sample_f16(std::uint32_t n) { return rr::PixelInfo::half_to_float(static_cast<std::uint16_t>(0x3800u | (mix32(n) & 0x3ffu))); }
float sample_unorm8(std::uint32_t n) { return static_cast<float>(8u + mix32(n) % 240u) / 255.0f; }
float sample_unorm16(std::uint32_t n) { return static_cast<float>(300u + mix32(n) % 65000u) / 65535.0f; }
float sample_unorm10(std::uint32_t n) { return static_cast<float>(20u + mix32(n) % 980u) / 1023.0f; }
float sample_snorm8(std::uint32_t n) { return static_cast<float>(static_cast<int>(mix32(n) % 253u) - 126) / 127.0f; }
float sample_snorm16(std::uint32_t n) { return static_cast<float>(static_cast<int>(mix32(n) % 65000u) - 32500) / 32767.0f; }
float sample_uint8(std::uint32_t n) { return static_cast<float>(8u + mix32(n) % 240u); }
float sample_uint16(std::uint32_t n) { return static_cast<float>(256u + mix32(n) % 65000u); }
float sample_uint32(std::uint32_t n) { return static_cast<float>(65536u + mix32(n) % 16000000u); }

template <class T>
void put(std::uint8_t* out, std::size_t index, T value) {
    std::memcpy(out + index * sizeof(T), &value, sizeof(T));
}

std::vector<VertexFormat> vertex_formats() {
    using rr::PixelInfo;
    std::vector<VertexFormat> list;
    auto f32 = [](int comps) { return [comps](const float* c, std::uint8_t* o) { for (int i = 0; i < comps; ++i) put<float>(o, i, c[i]); }; };
    auto f16 = [](int comps) { return [comps](const float* c, std::uint8_t* o) { for (int i = 0; i < comps; ++i) put<std::uint16_t>(o, i, PixelInfo::float_to_half(c[i])); }; };
    auto u16 = [](int comps) { return [comps](const float* c, std::uint8_t* o) { for (int i = 0; i < comps; ++i) put<std::uint16_t>(o, i, static_cast<std::uint16_t>(std::lround(clampf(c[i], 0, 1) * 65535.0f))); }; };
    auto s16 = [](int comps) { return [comps](const float* c, std::uint8_t* o) { for (int i = 0; i < comps; ++i) put<std::int16_t>(o, i, static_cast<std::int16_t>(std::lround(clampf(c[i], -1, 1) * 32767.0f))); }; };
    auto u8 = [](int comps, bool swap) { return [comps, swap](const float* c, std::uint8_t* o) { const int order[4] = {2, 1, 0, 3}; for (int i = 0; i < comps; ++i) put<std::uint8_t>(o, i, static_cast<std::uint8_t>(std::lround(clampf(c[swap ? order[i] : i], 0, 1) * 255.0f))); }; };
    auto s8 = [](int comps) { return [comps](const float* c, std::uint8_t* o) { for (int i = 0; i < comps; ++i) put<std::int8_t>(o, i, static_cast<std::int8_t>(std::lround(clampf(c[i], -1, 1) * 127.0f))); }; };
    list.push_back({"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, 16, false, f32(4), sample_f32});
    list.push_back({"r32g32b32_float", DXGI_FORMAT_R32G32B32_FLOAT, 12, false, f32(3), sample_f32});
    list.push_back({"r32g32_float", DXGI_FORMAT_R32G32_FLOAT, 8, false, f32(2), sample_f32});
    list.push_back({"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, 8, false, f16(4), sample_f16});
    list.push_back({"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, 4, false, f16(2), sample_f16});
    list.push_back({"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, 8, false, u16(4), sample_unorm16});
    list.push_back({"r16g16_unorm", DXGI_FORMAT_R16G16_UNORM, 4, false, u16(2), sample_unorm16});
    list.push_back({"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM, 8, false, s16(4), sample_snorm16});
    list.push_back({"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, u8(4, false), sample_unorm8});
    list.push_back({"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, 4, false, u8(4, true), sample_unorm8});
    list.push_back({"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM, 4, false, s8(4), sample_snorm8});
    list.push_back({"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, 2, false, u8(2, false), sample_unorm8});
    list.push_back({"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, 4, false, [](const float* c, std::uint8_t* o) {
                        const std::uint32_t r = static_cast<std::uint32_t>(std::lround(clampf(c[0], 0, 1) * 1023.0f));
                        const std::uint32_t g = static_cast<std::uint32_t>(std::lround(clampf(c[1], 0, 1) * 1023.0f));
                        const std::uint32_t b = static_cast<std::uint32_t>(std::lround(clampf(c[2], 0, 1) * 1023.0f));
                        const std::uint32_t a = static_cast<std::uint32_t>(std::lround(clampf(c[3], 0, 1) * 3.0f));
                        put<std::uint32_t>(o, 0, r | (g << 10) | (b << 20) | (a << 30));
                    }, sample_unorm10});
    list.push_back({"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT, 4, true, [](const float* c, std::uint8_t* o) { for (int i = 0; i < 4; ++i) put<std::uint8_t>(o, i, static_cast<std::uint8_t>(c[i])); }, sample_uint8});
    list.push_back({"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT, 8, true, [](const float* c, std::uint8_t* o) { for (int i = 0; i < 4; ++i) put<std::uint16_t>(o, i, static_cast<std::uint16_t>(c[i])); }, sample_uint16});
    list.push_back({"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT, 16, true, [](const float* c, std::uint8_t* o) { for (int i = 0; i < 4; ++i) put<std::uint32_t>(o, i, static_cast<std::uint32_t>(c[i])); }, sample_uint32});
    return list;
}

DXGI_FORMAT sibling_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case DXGI_FORMAT_R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_UINT;
        case DXGI_FORMAT_R32G32_FLOAT: return DXGI_FORMAT_R32G32_UINT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case DXGI_FORMAT_R16G16_FLOAT: return DXGI_FORMAT_R16G16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8G8_SNORM;
        case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case DXGI_FORMAT_R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case DXGI_FORMAT_R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        default: return format;
    }
}

struct Bars {
    std::vector<float> positions;
    UINT count = 0;
    Bars() {
        for (int q = 0; q < 6; ++q) {
            const float x0 = -0.95f + 0.32f * static_cast<float>(q);
            const float x1 = x0 + 0.28f;
            const float y0 = -0.8f + 0.1f * static_cast<float>(q);
            const float y1 = 0.8f - 0.1f * static_cast<float>(q);
            const float quad[6][2] = {{x0, y0}, {x0, y1}, {x1, y1}, {x0, y0}, {x1, y1}, {x1, y0}};
            for (const auto& p : quad) {
                positions.push_back(p[0]);
                positions.push_back(p[1]);
            }
        }
        count = 36;
    }

    [[nodiscard]] static std::vector<float> colors_for(const VertexFormat& format, unsigned shift) {
        std::vector<float> colors;
        const std::uint32_t salt = static_cast<std::uint32_t>(format.format) * 7919u;
        for (unsigned v = 0; v < 36; ++v) {
            const unsigned quad = (v / 6 + shift) % 6;
            for (unsigned k = 0; k < 4; ++k) {
                colors.push_back(format.sample(salt + quad * 4 + k));
            }
        }
        return colors;
    }
};

const char* kAttrVs =
    "struct VsIn { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos, 0, 1); o.col = frac(i.col * 64.0); return o; }\n";
const char* kAttrUintVs =
    "struct VsIn { float2 pos : POSITION; uint4 col : COLOR; };\n"
    "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos, 0, 1); uint4 h = (i.col * 977u + (i.col >> 8)) & 255u; o.col = float4(h) / 255.0; return o; }\n";
const char* kAttrPs =
    "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "float4 main(VsOut i) : SV_Target { return i.col; }\n";

void register_formats() {
    for (const VertexFormat& format : vertex_formats()) {
        Cases::single(rr::CaseSpec("ia", std::string("vertex_format_") + format.name).tolerance(rr::Tolerance::blend(2)).tag("vertex"),
                      [format](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          const Bars bars;
                          const std::vector<float> colors = Bars::colors_for(format, 0);
                          std::vector<std::uint8_t> encoded(static_cast<std::size_t>(bars.count) * format.size);
                          for (UINT v = 0; v < bars.count; ++v) {
                              format.encode(&colors[v * 4], &encoded[static_cast<std::size_t>(v) * format.size]);
                          }
                          ID3D12Resource* positions = kit.make_upload(bars.positions.data(), bars.positions.size() * 4, error);
                          ID3D12Resource* attributes = kit.make_upload(encoded.data(), encoded.size(), error);
                          if (positions == nullptr || attributes == nullptr) {
                              return false;
                          }
                          const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {
                              {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                              {"COLOR", 0, kit.broken("vertex_format") ? sibling_format(format.format) : format.format, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(format.integer ? kAttrUintVs : kAttrVs)).ps(kAttrPs).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const D3D12_VERTEX_BUFFER_VIEW views[2] = {{positions->GetGPUVirtualAddress(), static_cast<UINT>(bars.positions.size() * 4), 8},
                                                                     {attributes->GetGPUVirtualAddress(), static_cast<UINT>(encoded.size()), format.size}};
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                  list->IASetVertexBuffers(0, 2, views);
                                  list->DrawInstanced(bars.count, 1, 0, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
}

void register_format_streams() {
    for (const VertexFormat& format : vertex_formats()) {
        const std::string format_name = format.name;
        if (format_name != "r8g8b8a8_unorm" && format_name != "r32g32b32a32_float") {
            continue;
        }
        Cases::frames(rr::CaseSpec("ia", std::string("vertex_format_") + format.name + "_streamed_ring").frames(4, {0, 3}).tolerance(rr::Tolerance::blend(2)).tag("vertex"), [format](Cases::Steps& steps) {
            struct State {
                Stage stage;
                ID3D12Resource* positions = nullptr;
                ID3D12Resource* ring = nullptr;
                std::uint8_t* mapped = nullptr;
                ID3D12PipelineState* state = nullptr;
                Bars bars;
            };
            auto s = std::make_shared<State>();
            steps.on_setup = [s, format](Kit& kit, std::string& error) {
                if (!s->stage.open(kit, error)) {
                    return false;
                }
                s->positions = kit.make_upload(s->bars.positions.data(), s->bars.positions.size() * 4, error);
                s->ring = kit.make_upload(nullptr, 3ull * s->bars.count * format.size, error);
                if (s->positions == nullptr || s->ring == nullptr) {
                    return false;
                }
                const D3D12_RANGE none{0, 0};
                if (FAILED(s->ring->Map(0, &none, reinterpret_cast<void**>(&s->mapped)))) {
                    return fail_with(error, "Map of the vertex ring failed");
                }
                const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, kit.broken("vertex_format") ? sibling_format(format.format) : format.format, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
                Pso builder(kit, s->stage.canvas.binder.signature);
                builder.vs(Hlsl12::with_root(format.integer ? kAttrUintVs : kAttrVs)).ps(kAttrPs).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                s->state = builder.build(error);
                return s->state != nullptr;
            };
            steps.on_frame = [s, format](Kit& kit, unsigned frame, std::string& error) {
                const std::size_t slot = static_cast<std::size_t>(frame % 3) * s->bars.count * format.size;
                const std::vector<float> colors = Bars::colors_for(format, frame + 2);
                for (UINT v = 0; v < s->bars.count; ++v) {
                    format.encode(&colors[v * 4], s->mapped + slot + static_cast<std::size_t>(v) * format.size);
                }
                const D3D12_VERTEX_BUFFER_VIEW views[2] = {{s->positions->GetGPUVirtualAddress(), static_cast<UINT>(s->bars.positions.size() * 4), 8},
                                                           {s->ring->GetGPUVirtualAddress() + slot, s->bars.count * format.size, format.size}};
                return kit.submit([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    s->stage.canvas.begin(list, clear);
                    list->SetPipelineState(s->state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->IASetVertexBuffers(0, 2, views);
                    list->DrawInstanced(s->bars.count, 1, 0, 0);
                }, error);
            };
            steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
        });
    }
}

const char* kInstVs =
    "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float4 inst : INSTANCE; float4 tint : TINT; uint id : SV_InstanceID; };\n"
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 tint : TINT; };\n"
    "VsOut main(VsIn i) { VsOut o; float3 p = i.pos * i.inst.w + i.inst.xyz; o.pos = mul(mvp, float4(p, 1)); o.nrm = i.nrm; o.tint = i.tint; return o; }\n";
const char* kInstPs =
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 tint : TINT; };\n"
    "float4 main(VsOut i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2; return float4(i.tint.rgb * l, 1); }\n";

struct InstanceData {
    float offset[4];
};

bool draw_instanced(Kit& kit, UINT step_rate, UINT start_instance, UINT instances, bool indexed, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
        return false;
    }
    std::vector<InstanceData> positions;
    std::vector<float> tints;
    const int total = 12;
    for (int k = 0; k < total; ++k) {
        positions.push_back({{-0.9f + 0.55f * static_cast<float>(k % 4), -0.7f + 0.65f * static_cast<float>(k / 4), 0.1f * static_cast<float>(k % 3), 0.17f + 0.015f * static_cast<float>(k)}});
        tints.push_back(0.3f + 0.06f * static_cast<float>(k));
        tints.push_back(0.9f - 0.07f * static_cast<float>(k));
        tints.push_back(0.4f + 0.04f * static_cast<float>((k * 5) % 7));
        tints.push_back(1.0f);
    }
    ID3D12Resource* instance_buffer = kit.make_upload(positions.data(), positions.size() * sizeof(InstanceData), error);
    ID3D12Resource* tint_buffer = kit.make_upload(tints.data(), tints.size() * 4, error);
    if (instance_buffer == nullptr || tint_buffer == nullptr) {
        return false;
    }
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
    layout.push_back({"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, step_rate});
    layout.push_back({"TINT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1});
    Pso builder(kit, stage.canvas.binder.signature);
    builder.vs(Hlsl12::with_root(kInstVs)).ps(kInstPs).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
    ID3D12PipelineState* state = builder.build(error);
    Geometry sphere;
    const MeshData sphere_mesh = Shapes::sphere(10, 14, 1.0f);
    if (state == nullptr || !sphere.create(kit, sphere_mesh, false, error)) {
        return false;
    }
    Geometry expanded_sphere;
    if (!indexed) {
        std::vector<Vertex> expanded;
        for (std::uint32_t index : sphere_mesh.indices) {
            expanded.push_back(sphere_mesh.vertices[index]);
        }
        if (!expanded_sphere.create_vertices(kit, expanded.data(), static_cast<UINT>(expanded.size()), sizeof(Vertex), error)) {
            return false;
        }
    }
    const D3D12_VERTEX_BUFFER_VIEW views[2] = {{instance_buffer->GetGPUVirtualAddress(), static_cast<UINT>(positions.size() * sizeof(InstanceData)), sizeof(InstanceData)},
                                               {tint_buffer->GetGPUVirtualAddress(), static_cast<UINT>(tints.size() * 4), 16}};
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            list->SetPipelineState(state);
            (indexed ? sphere : expanded_sphere).bind(list);
            list->IASetVertexBuffers(1, 2, views);
            const rr::Mat4 matrix = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.6f);
            list->SetGraphicsRoot32BitConstants(0, 16, matrix.m, 0);
            if (indexed) {
                list->DrawIndexedInstanced(sphere.index_count, instances, 0, 0, start_instance);
            } else {
                list->DrawInstanced(expanded_sphere.vertex_count, instances, 0, start_instance);
            }
        },
        error);
    return ok && stage.finish(out, error);
}

void register_instancing() {
    Cases::single(rr::CaseSpec("ia", "instancing_step1_indexed").tolerance(rr::Tolerance::blend(2)).tag("instance"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_instanced(kit, 1, 0, 12, true, out, error); });
    Cases::single(rr::CaseSpec("ia", "instancing_step2_indexed").tolerance(rr::Tolerance::blend(2)).tag("instance"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_instanced(kit, 2, 0, 12, true, out, error); });
    Cases::single(rr::CaseSpec("ia", "instancing_start_instance_location").tolerance(rr::Tolerance::blend(2)).tag("instance"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_instanced(kit, 1, 5, 7, true, out, error); });
    Cases::single(rr::CaseSpec("ia", "instancing_start_instance_nonindexed").tolerance(rr::Tolerance::blend(2)).tag("instance"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_instanced(kit, 1, 3, 6, false, out, error); });
    const char* ids_vs =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "VsOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
        "  VsOut o; uint corner = vid % 6; float2 c = float2((corner == 1 || corner == 4 || corner == 5) ? 1 : 0, (corner == 2 || corner == 3 || corner == 5) ? 1 : 0);\n"
        "  uint cell = vid / 6 + iid * 4; float2 org = float2(-0.9 + 0.45 * (cell % 4), -0.9 + 0.45 * ((cell / 4) % 4));\n"
        "  o.pos = float4(org + c * 0.4, 0, 1); o.col = float3((cell % 5) / 4.0, (cell % 3) / 2.0, iid / 3.0 + 0.2); return o; }\n";
    const char* ids_ps =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i) : SV_Target { return float4(i.col, 1); }\n";
    Cases::single(rr::CaseSpec("ia", "sv_vertexid_instanceid_no_buffers").tolerance(rr::Tolerance::blend(2)).tag("instance"),
                  [ids_vs, ids_ps](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error)) {
                          return false;
                      }
                      Pso builder(kit, stage.canvas.binder.signature);
                      builder.vs(Hlsl12::with_root(ids_vs)).ps(ids_ps).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              list->SetPipelineState(state);
                              list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                              list->DrawInstanced(24, 4, 0, 0);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
    Cases::single(rr::CaseSpec("ia", "multiple_vertex_slots_split_attributes").tolerance(rr::Tolerance::blend(2)).tag("vertex"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                          return false;
                      }
                      const MeshData mesh = Shapes::torus(20, 10, 0.8f, 0.3f);
                      std::vector<float> position_stream;
                      std::vector<float> normal_stream;
                      std::vector<float> uv_stream;
                      for (const Vertex& v : mesh.vertices) {
                          position_stream.insert(position_stream.end(), v.pos, v.pos + 3);
                          normal_stream.insert(normal_stream.end(), v.normal, v.normal + 3);
                          uv_stream.insert(uv_stream.end(), v.uv, v.uv + 2);
                      }
                      std::vector<std::uint16_t> indices(mesh.indices.begin(), mesh.indices.end());
                      ID3D12Resource* p = kit.make_upload(position_stream.data(), position_stream.size() * 4, error);
                      ID3D12Resource* n = kit.make_upload(normal_stream.data(), normal_stream.size() * 4, error);
                      ID3D12Resource* t = kit.make_upload(uv_stream.data(), uv_stream.size() * 4, error);
                      ID3D12Resource* ib = kit.make_upload(indices.data(), indices.size() * 2, error);
                      if (p == nullptr || n == nullptr || t == nullptr || ib == nullptr) {
                          return false;
                      }
                      const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {
                          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                          {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 5, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                          {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 7, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
                      Pso builder = stage.pso();
                      builder.layout(layout);
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      D3D12_VERTEX_BUFFER_VIEW views[3] = {{p->GetGPUVirtualAddress(), static_cast<UINT>(position_stream.size() * 4), 12},
                                                           {n->GetGPUVirtualAddress(), static_cast<UINT>(normal_stream.size() * 4), 12},
                                                           {t->GetGPUVirtualAddress(), static_cast<UINT>(uv_stream.size() * 4), 8}};
                      const D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), static_cast<UINT>(indices.size() * 2), DXGI_FORMAT_R16_UINT};
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              list->SetPipelineState(state);
                              list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                              list->IASetVertexBuffers(2, 1, &views[0]);
                              list->IASetVertexBuffers(5, 1, &views[1]);
                              list->IASetVertexBuffers(7, 1, &views[2]);
                              list->IASetIndexBuffer(&ibv);
                              const float color[4] = {0.9f, 0.6f, 0.2f, 1.0f};
                              set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(0.9f), color);
                              list->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
}

void register_indices() {
    struct Variant {
        const char* name;
        bool wide;
        UINT first_index;
        INT base_vertex;
    };
    const Variant variants[] = {{"index16", false, 0, 0}, {"index32", true, 0, 0}, {"first_index_offset", false, 300, 0}, {"base_vertex_offset", true, 0, 40}};
    static const float palette[4][4] = {{0.9f, 0.6f, 0.2f, 1}, {0.3f, 0.8f, 0.9f, 1}, {0.8f, 0.3f, 0.6f, 1}, {0.5f, 0.9f, 0.3f, 1}};
    int variant_index = -1;
    for (const Variant& variant : variants) {
        ++variant_index;
        Cases::single(rr::CaseSpec("ia", std::string("draw_indexed_") + variant.name).tolerance(rr::Tolerance::blend(2)).tag("index"),
                      [variant, variant_index](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                              return false;
                          }
                          MeshData mesh = Shapes::torus(24 - 2 * variant_index, 12, 0.85f - 0.05f * static_cast<float>(variant_index), 0.3f - 0.03f * static_cast<float>(variant_index));
                          if (variant.wide && variant.base_vertex == 0) {
                              const std::uint32_t pad = 70000;
                              MeshData big;
                              big.vertices.assign(pad, Vertex{{5, 5, 5}, {0, 0, 1}, {0, 0}});
                              big.vertices.insert(big.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
                              for (std::uint32_t index : mesh.indices) {
                                  big.indices.push_back(index + pad);
                              }
                              mesh = big;
                          }
                          MeshData padded;
                          if (variant.base_vertex != 0) {
                              for (int k = 0; k < variant.base_vertex; ++k) {
                                  padded.vertices.push_back({{5, 5, 5}, {0, 0, 1}, {0, 0}});
                              }
                              padded.vertices.insert(padded.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
                              padded.indices = mesh.indices;
                              mesh = padded;
                          }
                          Geometry geometry;
                          if (!geometry.create(kit, mesh, variant.wide, error)) {
                              return false;
                          }
                          Pso builder = stage.pso();
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  geometry.bind(list);
                                  set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(0.9f + 0.3f * static_cast<float>(variant_index)), palette[variant_index]);
                                  const UINT count = variant.first_index != 0 ? (geometry.index_count - variant.first_index) / 3 * 3 : geometry.index_count;
                                  list->DrawIndexedInstanced(count, 1, variant.first_index, variant.base_vertex, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
    for (int wide = 0; wide < 2; ++wide) {
        Cases::single(rr::CaseSpec("ia", wide != 0 ? "strip_cut_value_0xffffffff" : "strip_cut_value_0xffff").tolerance(rr::Tolerance::blend(2)).tag("index"),
                      [wide](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          std::vector<Vertex> vertices;
                          std::vector<std::uint32_t> indices;
                          const int strips = wide != 0 ? 3 : 4;
                          const int per_strip = wide != 0 ? 8 : 6;
                          for (int strip = 0; strip < strips; ++strip) {
                              for (int k = 0; k < per_strip; ++k) {
                                  const float x = -0.9f + (wide != 0 ? 0.6f : 0.45f) * static_cast<float>(strip) + (wide != 0 ? 0.3f : 0.2f) * static_cast<float>(k % 2);
                                  const float y = -0.8f + (wide != 0 ? 0.22f : 0.3f) * static_cast<float>(k / 2) * (1.0f + 0.1f * static_cast<float>(strip));
                                  indices.push_back(static_cast<std::uint32_t>(vertices.size()));
                                  vertices.push_back({{x, y, 0}, {0, 0, 1}, {0, 0}});
                              }
                              indices.push_back(wide != 0 ? 0xffffffffu : 0xffffu);
                          }
                          indices.pop_back();
                          MeshData mesh{vertices, indices};
                          Geometry geometry;
                          if (!geometry.create(kit, mesh, wide != 0, error)) {
                              return false;
                          }
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root("float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x / 128.0, 0.6, pos.y / 128.0, 1); }\n"))
                              .targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          builder.desc.IBStripCutValue = wide != 0 ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  geometry.bind(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                                  const float color[4] = {1, 1, 1, 1};
                                  set_cbv_root(list, rr::Mat4::identity(), color);
                                  list->DrawIndexedInstanced(geometry.index_count, 1, 0, 0, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
}

void register_topologies() {
    struct Topology {
        const char* name;
        D3D_PRIMITIVE_TOPOLOGY topology;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE type;
        UINT count;
    };
    const Topology topologies[] = {
        {"triangle_list", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 18},
        {"triangle_strip", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 10},
        {"line_list", D3D_PRIMITIVE_TOPOLOGY_LINELIST, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, 160},
        {"line_strip", D3D_PRIMITIVE_TOPOLOGY_LINESTRIP, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, 120},
        {"point_list", D3D_PRIMITIVE_TOPOLOGY_POINTLIST, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT, 400},
    };
    for (const Topology& topology : topologies) {
        Cases::single(rr::CaseSpec("ia", std::string("topology_") + topology.name).tolerance(rr::Tolerance::raster(4, 1)).tag("topology"),
                      [topology](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          std::vector<Vertex> vertices;
                          for (UINT k = 0; k < topology.count; ++k) {
                              const float t = static_cast<float>(k);
                              const float a = t * 0.9f;
                              const float r = 0.25f + 0.07f * static_cast<float>(k % 9);
                              vertices.push_back({{r * std::cos(a), r * std::sin(a) * 1.2f, 0}, {0, 0, 1}, {0, 0}});
                          }
                          Geometry geometry;
                          if (!geometry.create_vertices(kit, vertices.data(), static_cast<UINT>(vertices.size()), sizeof(Vertex), error)) {
                              return false;
                          }
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs))
                              .ps(Hlsl12::with_root("float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x / 128.0, 1 - pos.y / 128.0, 0.5, 1); }\n"))
                              .targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          builder.desc.PrimitiveTopologyType = topology.type;
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  geometry.bind(list, topology.topology);
                                  const float color[4] = {1, 1, 1, 1};
                                  set_cbv_root(list, rr::Mat4::identity(), color);
                                  list->DrawInstanced(topology.count, 1, 0, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
    const char* gs_source =
        "struct In { float4 pos : SV_Position; };\n"
        "[maxvertexcount(3)] void main(%s In v[%d], inout TriangleStream<In> stream) {\n"
        "  %s\n"
        "}\n";
    struct Adjacency {
        const char* name;
        D3D_PRIMITIVE_TOPOLOGY topology;
        const char* primitive;
        int vertices;
        const char* body;
        UINT count;
    };
    const Adjacency adjacencies[] = {
        {"trianglelist_adj", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ, "triangleadj", 6,
         "stream.Append(v[0]); stream.Append(v[2]); stream.Append(v[4]); stream.RestartStrip();", 12},
        {"trianglestrip_adj", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ, "triangleadj", 6,
         "stream.Append(v[0]); stream.Append(v[2]); stream.Append(v[4]); stream.RestartStrip();", 14},
        {"linelist_adj", D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ, "lineadj", 4,
         "In a = v[1]; In b = v[2]; In c = v[1]; c.pos.y += 0.25; stream.Append(a); stream.Append(b); stream.Append(c); stream.RestartStrip();", 16},
        {"linestrip_adj", D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ, "lineadj", 4,
         "In a = v[1]; In b = v[2]; In c = v[1]; c.pos.y += 0.25; stream.Append(a); stream.Append(b); stream.Append(c); stream.RestartStrip();", 10},
    };
    for (const Adjacency& adjacency : adjacencies) {
        Cases::single(rr::CaseSpec("ia", std::string("topology_") + adjacency.name).tolerance(rr::Tolerance::blend(2)).tag("topology"),
                      [adjacency, gs_source](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          char text[1024];
                          std::snprintf(text, sizeof text, gs_source, adjacency.primitive, adjacency.vertices, adjacency.body);
                          std::vector<Vertex> vertices;
                          for (UINT k = 0; k < adjacency.count; ++k) {
                              const float a = static_cast<float>(k) * 0.7f;
                              const float r = 0.35f + 0.08f * static_cast<float>(k % 6);
                              vertices.push_back({{r * std::cos(a), r * std::sin(a), 0}, {0, 0, 1}, {0, 0}});
                          }
                          Geometry geometry;
                          if (!geometry.create_vertices(kit, vertices.data(), static_cast<UINT>(vertices.size()), sizeof(Vertex), error)) {
                              return false;
                          }
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs))
                              .stage(text, "main", "gs")
                              .ps(Hlsl12::with_root("float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x / 128.0, 1 - pos.y / 128.0, 0.5, 1); }\n"))
                              .targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          builder.desc.PrimitiveTopologyType = adjacency.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ ||
                                                                       adjacency.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ
                                                                   ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE
                                                                   : D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  geometry.bind(list, adjacency.topology);
                                  const float color[4] = {1, 1, 1, 1};
                                  set_cbv_root(list, rr::Mat4::identity(), color);
                                  list->DrawInstanced(adjacency.count, 1, 0, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
}

struct IndirectConstantArgs {
    float color[4];
    D3D12_DRAW_INDEXED_ARGUMENTS draw;
};

void register_indirect() {
    for (int counted = 0; counted < 2; ++counted) {
        Cases::single(rr::CaseSpec("ia", counted != 0 ? "execute_indirect_draw_indexed_count_buffer" : "execute_indirect_draw_indexed_constants")
                          .tolerance(rr::Tolerance::blend(2))
                          .tag("indirect"),
                      [counted](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                              return false;
                          }
                          Geometry geometry;
                          if (!geometry.create(kit, Shapes::torus(32, 16, 0.85f, 0.3f), true, error)) {
                              return false;
                          }
                          const UINT parts = 4;
                          const UINT per = geometry.index_count / 3 / parts * 3;
                          IndirectConstantArgs args[parts];
                          static const float palette[4][4] = {{0.9f, 0.3f, 0.2f, 1}, {0.2f, 0.8f, 0.3f, 1}, {0.2f, 0.4f, 0.95f, 1}, {0.95f, 0.85f, 0.2f, 1}};
                          for (UINT k = 0; k < parts; ++k) {
                              std::memcpy(args[k].color, palette[k], sizeof args[k].color);
                              args[k].draw = {k + 1 == parts ? geometry.index_count - per * k : per, 1, per * k, 0, 0};
                          }
                          ID3D12Resource* arg_buffer = kit.make_upload(args, sizeof args, error);
                          const UINT count_value = 3;
                          ID3D12Resource* count_buffer = kit.make_upload(&count_value, sizeof count_value, error);
                          if (arg_buffer == nullptr || count_buffer == nullptr) {
                              return false;
                          }
                          D3D12_INDIRECT_ARGUMENT_DESC argument_descs[2]{};
                          argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
                          argument_descs[0].Constant = {0, 16, 4};
                          argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
                          D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
                          signature_desc.ByteStride = sizeof(IndirectConstantArgs);
                          signature_desc.NumArgumentDescs = 2;
                          signature_desc.pArgumentDescs = argument_descs;
                          Com<ID3D12CommandSignature> signature;
                          const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, stage.canvas.binder.signature, __uuidof(ID3D12CommandSignature), signature.put_void());
                          if (FAILED(created)) {
                              return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
                          }
                          ID3D12CommandSignature* held = kit.rig.hold(std::move(signature));
                          Pso builder = stage.pso();
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  geometry.bind(list);
                                  const rr::Mat4 matrix = stage.view_projection() * rr::Mat4::rotate_x(0.9f);
                                  list->SetGraphicsRoot32BitConstants(0, 16, matrix.m, 0);
                                  if (counted != 0) {
                                      list->ExecuteIndirect(held, 4, arg_buffer, 0, count_buffer, 0);
                                  } else {
                                      list->ExecuteIndirect(held, parts, arg_buffer, 0, nullptr, 0);
                                  }
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
    Cases::single(rr::CaseSpec("ia", "execute_indirect_draw_instanced").tolerance(rr::Tolerance::blend(2)).tag("indirect"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error)) {
                          return false;
                      }
                      D3D12_DRAW_ARGUMENTS args[3] = {{24, 4, 0, 0}, {12, 2, 24, 4}, {6, 3, 12, 1}};
                      ID3D12Resource* arg_buffer = kit.make_upload(args, sizeof args, error);
                      if (arg_buffer == nullptr) {
                          return false;
                      }
                      D3D12_INDIRECT_ARGUMENT_DESC argument{};
                      argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
                      D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
                      signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
                      signature_desc.NumArgumentDescs = 1;
                      signature_desc.pArgumentDescs = &argument;
                      Com<ID3D12CommandSignature> signature;
                      const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, nullptr, __uuidof(ID3D12CommandSignature), signature.put_void());
                      if (FAILED(created)) {
                          return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
                      }
                      ID3D12CommandSignature* held = kit.rig.hold(std::move(signature));
                      const char* vs =
                          "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
                          "VsOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
                          "  VsOut o; uint corner = vid % 6; float2 c = float2((corner == 1 || corner == 4 || corner == 5) ? 1 : 0, (corner == 2 || corner == 3 || corner == 5) ? 1 : 0);\n"
                          "  uint cell = vid / 6 + iid * 4; float2 org = float2(-0.9 + 0.45 * (cell % 4), -0.9 + 0.45 * ((cell / 4) % 4));\n"
                          "  o.pos = float4(org + c * 0.4, 0, 1); o.col = float3(iid / 3.0 + 0.2, (cell % 3) / 2.0, (cell % 5) / 4.0); return o; }\n";
                      Pso builder(kit, stage.canvas.binder.signature);
                      builder.vs(Hlsl12::with_root(vs)).ps("struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\nfloat4 main(VsOut i) : SV_Target { return float4(i.col, 1); }\n").layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              list->SetPipelineState(state);
                              list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                              list->ExecuteIndirect(held, 3, arg_buffer, 0, nullptr, 0);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
}

void register_stream_output() {
    for (int mode = 0; mode < 2; ++mode) {
        rr::CaseSpec so_spec("ia", mode == 0 ? "stream_output_vertex_shader_then_draw" : "stream_output_geometry_shader_then_draw");
        so_spec.tolerance(rr::Tolerance::blend(2)).tag("streamout");
        so_spec.same_as({mode == 0 ? "ia.stream_output_geometry_shader_then_draw" : "ia.stream_output_vertex_shader_then_draw"},
                        "the vertex shader pass and the passthrough geometry shader pass stream out the same torus, so the filled size capture is the same count by design; the redrawn colour image differs");
        Cases::single(so_spec,
                      [mode](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                              return false;
                          }
                          const MeshData mesh = Shapes::torus(18, 10, 0.85f, 0.3f);
                          std::vector<Vertex> expanded;
                          for (std::uint32_t index : mesh.indices) {
                              expanded.push_back(mesh.vertices[index]);
                          }
                          Geometry source;
                          if (!source.create_vertices(kit, expanded.data(), static_cast<UINT>(expanded.size()), sizeof(Vertex), error)) {
                              return false;
                          }
                          const char* so_vs =
                              "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                              "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                              "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.nrm = i.nrm; o.uv = i.uv; return o; }\n";
                          const char* so_gs =
                              "struct In { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                              "[maxvertexcount(3)] void main(triangle In v[3], inout TriangleStream<In> s) {\n"
                              "  float3 n = normalize(cross(v[1].pos.xyz - v[0].pos.xyz, v[2].pos.xyz - v[0].pos.xyz));\n"
                              "  for (int k = 0; k < 3; ++k) { In o = v[k]; o.nrm = (mode_flat) ? n : o.nrm; s.Append(o); } }\n";
                          std::string gs_text = std::string("static const bool mode_flat = true;\n") + so_gs;
                          const D3D12_SO_DECLARATION_ENTRY entries[3] = {{0, "SV_Position", 0, 0, 4, 0}, {0, "NORMAL", 0, 0, 3, 0}, {0, "TEXCOORD", 0, 0, 2, 0}};
                          const UINT strides[1] = {36};
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(so_vs));
                          if (mode == 1) {
                              builder.stage(gs_text, "main", "gs");
                          }
                          builder.targets({});
                          builder.desc.StreamOutput.pSODeclaration = entries;
                          builder.desc.StreamOutput.NumEntries = 3;
                          builder.desc.StreamOutput.pBufferStrides = strides;
                          builder.desc.StreamOutput.NumStrides = 1;
                          builder.desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
                          builder.desc.NumRenderTargets = 0;
                          builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
                          builder.desc.DepthStencilState.DepthEnable = FALSE;
                          builder.desc.PS = {};
                          ID3D12PipelineState* so_state = builder.build(error);
                          if (so_state == nullptr) {
                              return false;
                          }
                          const UINT64 bytes = static_cast<UINT64>(expanded.size()) * 36;
                          HRESULT result = S_OK;
                          ID3D12Resource* so_buffer = D3d12Tools::device_buffer(kit.rig, bytes, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_FLAG_NONE, &result);
                          ID3D12Resource* filled = D3d12Tools::device_buffer(kit.rig, 4096, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_FLAG_NONE, &result);
                          if (so_buffer == nullptr || filled == nullptr) {
                              return fail_with(error, "stream output buffers: " + D3d12Tools::hr(result));
                          }
                          const std::vector<std::uint32_t> zero_words(1024, 0);
                          if (!D3d12Tools::write_buffer(kit.rig, filled, D3D12_RESOURCE_STATE_STREAM_OUT, 0, zero_words.data(), 4096, error)) {
                              return false;
                          }
                          const char* draw_ps = Hlsl12::kLitPs;
                          const char* draw_vs =
                              "struct VsIn { float4 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                              "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                              "VsOut main(VsIn i) { VsOut o; o.pos = i.pos; o.nrm = i.nrm; o.uv = i.uv; return o; }\n";
                          std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
                          layout[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                          layout[1].AlignedByteOffset = 16;
                          layout[2].AlignedByteOffset = 28;
                          Pso draw(kit, stage.canvas.binder.signature);
                          draw.vs(Hlsl12::with_root(draw_vs)).ps(Hlsl12::with_root(draw_ps)).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
                          ID3D12PipelineState* draw_state = draw.build(error);
                          if (draw_state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
                                  list->SetPipelineState(so_state);
                                  const rr::Mat4 matrix = stage.view_projection() * rr::Mat4::rotate_x(0.9f);
                                  const float color[4] = {0.9f, 0.6f, 0.2f, 1.0f};
                                  set_cbv_root(list, matrix, color);
                                  D3D12_STREAM_OUTPUT_BUFFER_VIEW view{so_buffer->GetGPUVirtualAddress(), bytes, filled->GetGPUVirtualAddress()};
                                  list->SOSetTargets(0, 1, &view);
                                  source.bind(list);
                                  source.draw_vertices(list);
                                  list->SOSetTargets(0, 1, nullptr);
                                  D3d12Tools::move(list, so_buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                                  list->OMSetRenderTargets(1, &stage.canvas.color.view, FALSE, &stage.canvas.depth.view);
                                  list->SetPipelineState(draw_state);
                                  const D3D12_VERTEX_BUFFER_VIEW vbv{so_buffer->GetGPUVirtualAddress(), static_cast<UINT>(bytes), 36};
                                  list->IASetVertexBuffers(0, 1, &vbv);
                                  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                  list->DrawInstanced(static_cast<UINT>(expanded.size()), 1, 0, 0);
                              },
                              error);
                          if (!ok || !stage.finish(out, error)) {
                              return false;
                          }
                          std::vector<std::uint32_t> filled_words;
                          if (!D3d12Tools::read_words(kit.rig, filled, D3D12_RESOURCE_STATE_STREAM_OUT, 1, filled_words, error)) {
                              return false;
                          }
                          rr::Image size_image(2, 1, rr::PixelKind::Rgba32Uint);
                          const std::uint32_t values[8] = {filled_words[0], static_cast<std::uint32_t>(expanded.size()), 0, 0, static_cast<std::uint32_t>(expanded.size()), filled_words[0] / 36, 1, 7};
                          std::memcpy(size_image.bytes().data(), values, sizeof values);
                          out.add("filled_size", std::move(size_image));
                          return true;
                      });
    }
}

const struct Registrar {
    Registrar() {
        register_formats();
        register_format_streams();
        register_instancing();
        register_indices();
        register_topologies();
        register_indirect();
        register_stream_output();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
