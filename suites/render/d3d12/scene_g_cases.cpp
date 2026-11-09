#include "render/d3d12/scene_util.hpp"
#include "render/d3d12/textures.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

constexpr UINT kSize = 256;

const char* const kTerrainPrelude =
    "struct CP { float3 pos : POSITION; };\n"
    "struct Patch { float edges[4] : SV_TessFactor; float inside[2] : SV_InsideTessFactor; };\n"
    "float height(float2 q) { return 0.55 * sin(q.x * 0.9 + 0.5) * cos(q.y * 0.8) + 0.2 * sin(q.x * 2.1 + q.y * 1.7) + 0.08 * sin(q.x * 5.3) * sin(q.y * 4.7); }\n";

std::string terrain_hull() {
    return std::string(Hlsl12::kRoot) + kTerrainPrelude +
           "float fac(float3 a, float3 b) { float3 m = (a + b) * 0.5; m.y = 0.0; float d = distance(m, p2.xyz); return clamp(p1.x * 36.0 / d, 1.0, 24.0); }\n"
           "Patch pc(InputPatch<CP, 4> p) { Patch o; o.edges[0] = fac(p[0].pos, p[3].pos); o.edges[1] = fac(p[0].pos, p[1].pos); o.edges[2] = fac(p[1].pos, p[2].pos); o.edges[3] = fac(p[3].pos, p[2].pos);\n"
           "  o.inside[0] = 0.5 * (o.edges[1] + o.edges[3]); o.inside[1] = 0.5 * (o.edges[0] + o.edges[2]); return o; }\n"
           "[domain(\"quad\")][partitioning(\"fractional_odd\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"pc\")]\n"
           "CP main(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
}

std::string terrain_domain() {
    return std::string(Hlsl12::kRoot) + kTerrainPrelude +
           "struct DsOut { float4 pos : SV_Position; float3 wp : WP; };\n"
           "[domain(\"quad\")] DsOut main(Patch c, float2 loc : SV_DomainLocation, const OutputPatch<CP, 4> p) {\n"
           "  float3 w = lerp(lerp(p[0].pos, p[1].pos, loc.x), lerp(p[3].pos, p[2].pos, loc.x), loc.y); w.y = height(w.xz);\n"
           "  DsOut o; o.wp = w; o.pos = mul(mvp, float4(w, 1)); return o; }\n";
}

std::string terrain_pixel() {
    return std::string(Hlsl12::kRoot) + kTerrainPrelude +
           "struct DsOut { float4 pos : SV_Position; float3 wp : WP; };\n"
           "float4 main(DsOut i) : SV_Target {\n"
           "  float e = 0.05; float hx = height(i.wp.xz + float2(e, 0)) - height(i.wp.xz - float2(e, 0)); float hz = height(i.wp.xz + float2(0, e)) - height(i.wp.xz - float2(0, e));\n"
           "  float3 n = normalize(float3(-hx / (2 * e), 1, -hz / (2 * e))); float l = saturate(dot(n, normalize(float3(0.5, 0.8, 0.3)))) * 0.8 + 0.2;\n"
           "  float h = height(i.wp.xz); float3 low = float3(0.25, 0.5, 0.2); float3 mid = float3(0.55, 0.45, 0.28); float3 high = float3(0.92, 0.93, 0.96);\n"
           "  float3 c = lerp(low, mid, smoothstep(-0.2, 0.3, h)); c = lerp(c, high, smoothstep(0.5, 0.75, h)); return float4(c * l, 1); }\n";
}

void register_terrain() {
    for (int dxil = 0; dxil < 2; ++dxil) {
        rr::CaseSpec spec("scene", dxil != 0 ? "terrain_tessellation_distance_lod_sm6" : "terrain_tessellation_distance_lod");
        spec.size(kSize, kSize).tolerance(rr::Tolerance::blend(8).with_edge(1).with_outliers(0.004)).tag("scene").tag("tessellation");
        if (dxil != 0) {
            spec.variant(1).tag("x64-only");
            spec.same_as({"scene.terrain_tessellation_distance_lod"}, "the DXIL hull and domain shaders are the same terrain as the DXBC twin; the shader compiler and model are the subject of the pair");
        } else {
            spec.same_as({"scene.terrain_tessellation_distance_lod_sm6"}, "the DXBC hull and domain shaders are the same terrain as the DXIL twin; the shader compiler and model are the subject of the pair");
        }
        Cases::single(spec, [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize)) {
                return false;
            }
            std::vector<Vertex> patches;
            for (int z = 0; z < 8; ++z) {
                for (int x = 0; x < 8; ++x) {
                    const float x0 = static_cast<float>(x) - 4.0f;
                    const float z0 = static_cast<float>(z) - 6.0f;
                    patches.push_back({{x0, 0, z0}, {0, 1, 0}, {0, 0}});
                    patches.push_back({{x0 + 1, 0, z0}, {0, 1, 0}, {0, 0}});
                    patches.push_back({{x0 + 1, 0, z0 + 1}, {0, 1, 0}, {0, 0}});
                    patches.push_back({{x0, 0, z0 + 1}, {0, 1, 0}, {0, 0}});
                }
            }
            Geometry geometry;
            if (!geometry.create_vertices(kit, patches.data(), static_cast<UINT>(patches.size()), sizeof(Vertex), error)) {
                return false;
            }
            const char* vs =
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct CP { float3 pos : POSITION; };\n"
                "CP main(VsIn i) { CP o; o.pos = i.pos; return o; }\n";
            Pso builder(kit, stage.canvas.binder.signature);
            builder.vs(vs).stage(terrain_hull(), "main", "hs").stage(terrain_domain(), "main", "ds").ps(terrain_pixel()).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            builder.desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            const float up[3] = {0, 1, 0};
            const float eye[3] = {0.0f, 3.0f, 5.5f};
            const float look[3] = {0.0f, 0.0f, -1.5f};
            const rr::Mat4 vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, look, up);
            const float color[4] = {1, 1, 1, 1};
            const float factors[4] = {kit.broken("terrain_tess") ? 0.12f : 1.0f, 0, 0, 0};
            const float camera_position[4] = {eye[0], eye[1], eye[2], 0};
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.45f, 0.62f, 0.85f, 1};
                    stage.canvas.begin(list, clear);
                    list->SetPipelineState(state);
                    set_cbv_root(list, vp, color, factors);
                    list->SetGraphicsRoot32BitConstants(0, 4, camera_position, 24);
                    geometry.bind(list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
                    geometry.draw_vertices(list);
                },
                error);
            return ok && stage.finish(out, error);
        });
    }
}

struct Font {
    static constexpr std::uint8_t kRows[16][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
                                                  {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4}};

    [[nodiscard]] static std::vector<std::uint8_t> atlas() {
        std::vector<std::uint8_t> sharp(64 * 64, 0);
        for (int g = 0; g < 16; ++g) {
            const int cx = (g % 4) * 16;
            const int cy = (g / 4) * 16;
            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if ((kRows[g][row] >> (2 - col)) & 1) {
                        for (int y = 0; y < 3; ++y) {
                            for (int x = 0; x < 3; ++x) {
                                sharp[static_cast<std::size_t>((cy + 1 + row * 3 + y) * 64 + cx + 3 + col * 3 + x)] = 255;
                            }
                        }
                    }
                }
            }
        }
        std::vector<std::uint8_t> soft(64 * 64, 0);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int sum = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int px = x + dx;
                        const int py = y + dy;
                        if (px >= 0 && px < 64 && py >= 0 && py < 64) {
                            sum += sharp[static_cast<std::size_t>(py * 64 + px)];
                        }
                    }
                }
                soft[static_cast<std::size_t>(y * 64 + x)] = static_cast<std::uint8_t>(sum / 9);
            }
        }
        return soft;
    }
};

struct TextVertex {
    float pos[2];
    float uv[2];
    float color[4];
};

class TextBatch {
public:
    void run(const char* digits, float x, float y, float scale, float angle, const float top[4], const float bottom[4], bool shift) {
        const float cell = 16.0f * scale;
        const float advance = 0.72f * cell;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        for (int k = 0; digits[k] != 0; ++k) {
            int glyph = digits[k] <= '9' ? digits[k] - '0' : digits[k] - 'A' + 10;
            if (shift) {
                glyph = (glyph + 1) % 16;
            }
            const float u0 = static_cast<float>(glyph % 4) * 0.25f;
            const float v0 = static_cast<float>(glyph / 4) * 0.25f;
            const float ox = advance * static_cast<float>(k);
            const float corners[4][2] = {{0, 0}, {cell, 0}, {cell, cell}, {0, cell}};
            const float uvs[4][2] = {{u0, v0}, {u0 + 0.25f, v0}, {u0 + 0.25f, v0 + 0.25f}, {u0, v0 + 0.25f}};
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            for (int q = 0; q < 4; ++q) {
                const float lx = ox + corners[q][0];
                const float ly = corners[q][1];
                TextVertex v{};
                v.pos[0] = x + lx * c - ly * s;
                v.pos[1] = y + lx * s + ly * c;
                v.uv[0] = uvs[q][0];
                v.uv[1] = uvs[q][1];
                const float t = corners[q][1] / cell;
                for (int i = 0; i < 4; ++i) {
                    v.color[i] = top[i] * (1.0f - t) + bottom[i] * t;
                }
                vertices.push_back(v);
            }
            const std::uint32_t quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
            indices.insert(indices.end(), quad, quad + 6);
        }
    }

    std::vector<TextVertex> vertices;
    std::vector<std::uint32_t> indices;
};

void register_text() {
    Cases::single(rr::CaseSpec("scene", "text_glyph_atlas_alpha_runs_scaled_rotated").size(kSize, kSize).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, kSize)) {
            return false;
        }
        TextureSpec spec;
        spec.format = DXGI_FORMAT_R8_UNORM;
        spec.width = 64;
        spec.height = 64;
        ID3D12Resource* atlas = TextureFactory::create(kit, spec, false, error);
        if (atlas == nullptr) {
            return false;
        }
        const std::vector<std::uint8_t> bytes = Font::atlas();
        if (!D3d12Tools::write_texture(kit.rig, atlas, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, bytes.data(), error)) {
            return false;
        }
        Binder& binder = stage.canvas.binder;
        binder.srv_texture(0, atlas, DXGI_FORMAT_R8_UNORM);
        binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
        const float white[4] = {1, 1, 1, 1};
        const float cream[4] = {1.0f, 0.95f, 0.7f, 1};
        const float orange[4] = {1.0f, 0.5f, 0.15f, 1};
        const float cyan[4] = {0.2f, 0.9f, 1.0f, 1};
        const float shadow[4] = {0, 0, 0, 0.55f};
        const float blue[4] = {0.3f, 0.4f, 1.0f, 0.9f};
        TextBatch shadows;
        TextBatch text;
        const bool shifted = kit.broken("text_atlas");
        struct Line {
            const char* digits;
            float x, y, scale, angle;
            const float* top;
            const float* bottom;
        };
        const Line lines[] = {{"0123456789ABCDEF", 10, 12, 1.0f, 0.0f, white, white}, {"FEDCBA9876543210", 12, 44, 1.75f, 0.0f, cream, orange},
                              {"1A2B3C4D5E6F", 16, 100, 0.5f, 0.0f, cyan, cyan},      {"C0FFEE", 14, 128, 2.6f, 0.0f, orange, blue},
                              {"8BADF00D", 90, 214, 1.4f, -0.35f, white, cyan}};
        for (const Line& line : lines) {
            shadows.run(line.digits, line.x + 2.0f, line.y + 2.0f, line.scale, line.angle, shadow, shadow, shifted);
            text.run(line.digits, line.x, line.y, line.scale, line.angle, line.top, line.bottom, shifted);
        }
        Geometry shadow_geometry;
        Geometry text_geometry;
        if (!shadow_geometry.create_vertices(kit, shadows.vertices.data(), static_cast<UINT>(shadows.vertices.size()), sizeof(TextVertex), error) ||
            !text_geometry.create_vertices(kit, text.vertices.data(), static_cast<UINT>(text.vertices.size()), sizeof(TextVertex), error)) {
            return false;
        }
        ID3D12Resource* shadow_indices = kit.make_upload(shadows.indices.data(), shadows.indices.size() * 4, error);
        ID3D12Resource* text_indices = kit.make_upload(text.indices.data(), text.indices.size() * 4, error);
        if (shadow_indices == nullptr || text_indices == nullptr) {
            return false;
        }
        const char* vs =
            "struct VsIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };\n"
            "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };\n"
            "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos.x / 128.0 - 1.0, 1.0 - i.pos.y / 128.0, 0, 1); o.uv = i.uv; o.col = i.col; return o; }\n";
        const char* ps =
            "Texture2D<float> atlas : register(t0); SamplerState smp : register(s0);\n"
            "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };\n"
            "float4 main(VsOut i) : SV_Target { float a = atlas.Sample(smp, i.uv) * i.col.a; return float4(i.col.rgb, a); }\n";
        const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                              {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                              {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        Pso builder(kit, binder.signature);
        builder.vs(vs).ps(Hlsl12::with_root(ps)).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const D3D12_INDEX_BUFFER_VIEW shadow_ibv{shadow_indices->GetGPUVirtualAddress(), static_cast<UINT>(shadows.indices.size() * 4), DXGI_FORMAT_R32_UINT};
        const D3D12_INDEX_BUFFER_VIEW text_ibv{text_indices->GetGPUVirtualAddress(), static_cast<UINT>(text.indices.size() * 4), DXGI_FORMAT_R32_UINT};
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.1f, 0.1f, 0.14f, 0.5f};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(state);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &shadow_geometry.vbv);
                list->IASetIndexBuffer(&shadow_ibv);
                list->DrawIndexedInstanced(static_cast<UINT>(shadows.indices.size()), 1, 0, 0, 0);
                list->IASetVertexBuffers(0, 1, &text_geometry.vbv);
                list->IASetIndexBuffer(&text_ibv);
                list->DrawIndexedInstanced(static_cast<UINT>(text.indices.size()), 1, 0, 0, 0);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

const char* const kUiVs =
    "struct VsOut { float4 pos : SV_Position; float2 local : LOCAL; };\n"
    "VsOut main(uint id : SV_VertexID) {\n"
    "  static const float2 q[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1), float2(0, 1)};\n"
    "  float2 c = q[id]; float m = p2.z; float2 px = p1.xy - m + c * (p1.zw + 2.0 * m); VsOut o;\n"
    "  o.pos = float4(px.x / 128.0 - 1.0, 1.0 - px.y / 128.0, 0, 1); o.local = px - (p1.xy + p1.zw * 0.5); return o; }\n";

const char* const kUiPs =
    "struct VsOut { float4 pos : SV_Position; float2 local : LOCAL; };\n"
    "float box(float2 p, float2 h, float r) { float2 d = abs(p) - h + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }\n"
    "float4 main(VsOut i) : SV_Target {\n"
    "  float d = box(i.local, p1.zw * 0.5, p2.x); float cover = saturate(0.5 - d); float4 col = p0;\n"
    "  if (p2.w > 1.5) col = lerp(p0, p3, saturate(i.local.y / p1.w + 0.5));\n"
    "  if (p2.w > 0.5 && p2.w < 1.5) { cover = saturate(0.5 - d / (2.0 * max(p2.y, 0.01))); }\n"
    "  float a = col.a * cover; float3 rgb = col.rgb;\n"
    "  if (p2.w < 0.5 && p2.y > 0.0) { float inner = saturate(0.5 - d - p2.y); rgb = lerp(p3.rgb, col.rgb, inner); a = lerp(p3.a, col.a, inner) * cover; }\n"
    "  return float4(rgb * a, a); }\n";

void register_ui() {
    Cases::single(rr::CaseSpec("scene", "ui_compositing_rounded_panels_scissor_list").size(kSize, kSize).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize)) {
            return false;
        }
        Pso ui(kit, stage.canvas.binder.signature);
        ui.vs(Hlsl12::with_root(kUiVs)).ps(Hlsl12::with_root(kUiPs)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        ui.desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        D3D12_RENDER_TARGET_BLEND_DESC& blend = ui.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ID3D12PipelineState* ui_state = ui.build(error);
        Pso scene = stage.pso();
        ID3D12PipelineState* scene_state = scene.build(error);
        if (ui_state == nullptr || scene_state == nullptr) {
            return false;
        }
        const bool clip = !kit.broken("ui_scissor");
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.08f, 0.1f, 0.16f, 1};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(scene_state);
                stage.mesh(list, stage.torus, rr::Mat4::translate(-0.3f, 0, 0) * rr::Mat4::rotate_x(0.9f), 0.9f, 0.5f, 0.25f);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.5f, -0.2f, 0.3f), 0.3f, 0.7f, 0.9f);
                list->SetPipelineState(ui_state);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                const D3D12_RECT full = rect_of(kSize, kSize);
                auto widget = [&](float x, float y, float w, float h, float radius, float aux, float kind, const float color[4], const float other[4]) {
                    const float p1[4] = {x, y, w, h};
                    const float p2[4] = {radius, aux, kind == 1.0f ? aux * 2.0f + 2.0f : 1.0f, kind};
                    list->SetGraphicsRoot32BitConstants(0, 4, color, 16);
                    list->SetGraphicsRoot32BitConstants(0, 4, p1, 20);
                    list->SetGraphicsRoot32BitConstants(0, 4, p2, 24);
                    list->SetGraphicsRoot32BitConstants(0, 4, other, 28);
                    list->DrawInstanced(6, 1, 0, 0);
                };
                const float none[4] = {0, 0, 0, 0};
                const float shadow[4] = {0, 0, 0, 0.6f};
                const float panel[4] = {0.94f, 0.96f, 0.99f, 0.82f};
                const float edge[4] = {1, 1, 1, 0.95f};
                const float title_top[4] = {0.25f, 0.45f, 0.95f, 0.95f};
                const float title_bottom[4] = {0.1f, 0.22f, 0.7f, 0.95f};
                widget(28, 26, 200, 170, 14, 9, 1, shadow, none);
                widget(24, 20, 208, 176, 14, 2, 0, panel, edge);
                widget(24, 20, 208, 28, 14, 0, 2, title_top, title_bottom);
                const float well[4] = {0.82f, 0.86f, 0.92f, 0.9f};
                widget(36, 56, 118, 104, 6, 1, 0, well, edge);
                const D3D12_RECT list_rect{38, 58, 152, 158};
                if (clip) {
                    list->RSSetScissorRects(1, &list_rect);
                }
                for (int row = 0; row < 8; ++row) {
                    const float y = 46.0f + 24.0f * static_cast<float>(row);
                    const float a = (row & 1) != 0 ? 0.9f : 0.55f;
                    const float item[4] = {0.2f + 0.1f * static_cast<float>(row), 0.5f, 0.9f - 0.08f * static_cast<float>(row), a};
                    const float item_bottom[4] = {0.1f, 0.25f, 0.5f, a};
                    widget(40, y, 110, 20, 5, 0, 2, item, item_bottom);
                }
                list->RSSetScissorRects(1, &full);
                const float green[4] = {0.3f, 0.8f, 0.4f, 0.95f};
                const float green_dark[4] = {0.1f, 0.5f, 0.2f, 0.95f};
                const float red[4] = {0.9f, 0.3f, 0.3f, 0.95f};
                const float red_dark[4] = {0.6f, 0.1f, 0.15f, 0.95f};
                widget(164, 60, 58, 26, 8, 0, 2, green, green_dark);
                widget(164, 94, 58, 26, 8, 0, 2, red, red_dark);
                const float amber[4] = {1.0f, 0.75f, 0.2f, 0.6f};
                widget(164, 128, 58, 26, 13, 1, 0, amber, edge);
                const float track[4] = {0.55f, 0.58f, 0.65f, 0.9f};
                widget(36, 172, 186, 12, 6, 0, 0, track, none);
                widget(36, 172, 112, 12, 6, 0, 2, green, green_dark);
                const float bubble[4] = {1.0f, 0.4f, 0.2f, 0.55f};
                widget(196, 150, 56, 56, 28, 0, 0, bubble, none);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() {
        register_terrain();
        register_text();
        register_ui();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
