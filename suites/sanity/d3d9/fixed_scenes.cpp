#include "sanity/d3d9/fixed_scenes.hpp"

#include <cmath>
#include <cstring>
#include <memory>

namespace lsw::suites::sanity::d3d9 {

namespace {

struct LitVertex {
    float x, y, z, nx, ny, nz;
};

struct ColorVertex {
    float x, y, z;
    DWORD color;
};

struct TexturedVertex {
    float x, y, z;
    DWORD color;
    float u, v;
};

constexpr const char* kLitVs =
    "cbuffer C : register(b0) { float4 mat; float4 amb; float4 lpos; float4 ldir; float4 att; float4 cone; };\n"
    "struct I { float3 p : POSITION; float3 n : NORMAL; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "O main(I i) {\n"
    "  O o; float3 N = normalize(i.n); float3 L = -ldir.xyz; float a = 1; float s = 1;\n"
    "  if (ldir.w > 0.5) {\n"
    "    float3 d = lpos.xyz - i.p; float dist = length(d); L = d / dist;\n"
    "    a = dist > lpos.w ? 0 : 1.0 / (att.x + att.y * dist + att.z * dist * dist);\n"
    "    if (ldir.w > 1.5) { float rho = dot(-L, ldir.xyz);\n"
    "      if (rho > cone.x) s = 1; else if (rho <= cone.y) s = 0; else s = pow((rho - cone.y) / (cone.x - cone.y), att.w); }\n"
    "  }\n"
    "  float lam = max(0, dot(N, L));\n"
    "  o.c = saturate(amb + mat * (lam * a * s)); o.c.a = mat.a; o.p = float4(i.p, 1); return o;\n"
    "}\n";

constexpr const char* kPassPs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target { return c; }\n";

constexpr const char* kFogVs =
    "cbuffer C : register(b0) { float4 fog; float4 fogc; };\n"
    "struct I { float3 p : POSITION; float4 c : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float f : TEXCOORD0; };\n"
    "O main(I i) {\n"
    "  O o; o.p = float4(i.p, 1); o.c = i.c; float z = i.p.z;\n"
    "  if (fog.w < 0.5) o.f = (fog.y - z) / (fog.y - fog.x);\n"
    "  else if (fog.w < 1.5) o.f = exp(-fog.z * z);\n"
    "  else o.f = exp(-(fog.z * z) * (fog.z * z));\n"
    "  o.f = saturate(o.f); return o;\n"
    "}\n";

constexpr const char* kFogPs =
    "cbuffer C : register(b0) { float4 fog; float4 fogc; };\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR0, float f : TEXCOORD0) : SV_Target {\n"
    "  return float4(lerp(fogc.rgb, c.rgb, f), c.a); }\n";

constexpr const char* kStageVs =
    "struct I { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(I i) { O o; o.p = float4(i.p, 1); o.c = i.c; o.uv = i.uv; return o; }\n";

constexpr const char* kTciVs =
    "struct I { float3 p : POSITION; float4 c : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(I i) { O o; o.p = float4(i.p, 1); o.c = i.c; o.uv = float2(0.5 * i.p.x + 0.5, -0.5 * i.p.y + 0.5); return o; }\n";

constexpr const char* kTciPs =
    "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR0, float2 uv : TEXCOORD0) : SV_Target { return t0.Sample(s0, uv); }\n";

constexpr const char* kAlphaVs =
    "struct I { float3 p : POSITION; float4 c : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "O main(I i) { O o; o.p = float4(i.p, 1); o.c = i.c; return o; }\n";

constexpr const char* kAlphaPs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target { clip(round(c.a * 255.0) > 128.0 ? 1 : -1); return c; }\n";

constexpr const char* kClipVs =
    "struct I { float3 p : POSITION; float4 c : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR0; float3 w : TEXCOORD0; };\n"
    "O main(I i) { O o; o.p = float4(i.p, 1); o.c = i.c; o.w = i.p; return o; }\n";

constexpr const char* kClipPs =
    "cbuffer C : register(b0) { float4 plane0; float4 plane1; };\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR0, float3 w : TEXCOORD0) : SV_Target {\n"
    "  clip(dot(float4(w, 1), plane0)); clip(dot(float4(w, 1), plane1)); return c; }\n";

template <typename T>
std::vector<std::uint8_t> bytes_of(const std::vector<T>& values) {
    std::vector<std::uint8_t> out(values.size() * sizeof(T));
    std::memcpy(out.data(), values.data(), out.size());
    return out;
}

void grid(std::vector<LitVertex>& vertices, std::vector<std::uint16_t>& indices) {
    constexpr int n = 16;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float x = -0.9f + 1.8f * static_cast<float>(i) / n;
            const float y = -0.9f + 1.8f * static_cast<float>(j) / n;
            const float nx = 0.6f * x;
            const float ny = 0.6f * y;
            const float length = std::sqrt(nx * nx + ny * ny + 1.0f);
            vertices.push_back({x, y, 0.5f, nx / length, ny / length, -1.0f / length});
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const auto a = static_cast<std::uint16_t>(j * (n + 1) + i);
            const auto b = static_cast<std::uint16_t>(a + 1);
            const auto c = static_cast<std::uint16_t>(a + n + 1);
            const auto d = static_cast<std::uint16_t>(c + 1);
            for (const std::uint16_t v : {a, c, b, b, c, d}) {
                indices.push_back(v);
            }
        }
    }
}

void grid_fog(std::vector<ColorVertex>& vertices, std::vector<std::uint16_t>& indices) {
    constexpr int n = 8;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float x = -0.9f + 1.8f * static_cast<float>(i) / n;
            const float y = -0.9f + 1.8f * static_cast<float>(j) / n;
            vertices.push_back({x, y, 0.1f + 0.8f * (x + 0.9f) / 1.8f, 0xFFCC8040u});
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const auto a = static_cast<std::uint16_t>(j * (n + 1) + i);
            const auto b = static_cast<std::uint16_t>(a + 1);
            const auto c = static_cast<std::uint16_t>(a + n + 1);
            const auto d = static_cast<std::uint16_t>(c + 1);
            for (const std::uint16_t v : {a, c, b, b, c, d}) {
                indices.push_back(v);
            }
        }
    }
}

DWORD bits(float value) {
    DWORD out = 0;
    std::memcpy(&out, &value, 4);
    return out;
}

void apply_projection(IDirect3DDevice9* device) {
    const D3DMATRIX projection = D3d9Tools::half_pixel_projection(FixedScenes::kSize, FixedScenes::kSize);
    device->SetTransform(D3DTS_PROJECTION, &projection);
}

TwinScene twin_base() {
    TwinScene scene;
    scene.width = FixedScenes::kSize;
    scene.height = FixedScenes::kSize;
    return scene;
}


}  // namespace

std::vector<FixedScene> FixedScenes::build(IDirect3DDevice9* device, const D3DCAPS9& caps) {
    std::vector<FixedScene> scenes;
    const Tolerance lit_tolerance = Tolerance::bytes(4, 0.002);
    {
        auto vertices = std::make_shared<std::vector<LitVertex>>();
        auto indices = std::make_shared<std::vector<std::uint16_t>>();
        grid(*vertices, *indices);
        struct LightCase {
            const char* name;
            D3DLIGHTTYPE type;
        };
        for (const LightCase& lc : {LightCase{"lighting directional", D3DLIGHT_DIRECTIONAL}, LightCase{"lighting point", D3DLIGHT_POINT}, LightCase{"lighting spot", D3DLIGHT_SPOT}}) {
            D3DLIGHT9 light{};
            light.Type = lc.type;
            light.Diffuse = {1, 1, 1, 1};
            float direction[3] = {0.3f, -0.4f, 0.8f};
            float position[3] = {0.2f, 0.3f, -0.5f};
            float range = 3.0f;
            float attenuation[4] = {1.0f, 0.0f, 0.5f, 1.0f};
            float cones[2] = {0, 0};
            if (lc.type == D3DLIGHT_SPOT) {
                direction[0] = 0.1f;
                direction[1] = 0.05f;
                direction[2] = 1.0f;
                position[0] = 0.0f;
                position[1] = 0.0f;
                position[2] = -1.0f;
                range = 5.0f;
                attenuation[2] = 0.0f;
                light.Theta = 0.6f;
                light.Phi = 1.2f;
                light.Falloff = 1.0f;
                cones[0] = std::cos(light.Theta / 2);
                cones[1] = std::cos(light.Phi / 2);
            }
            const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
            for (float& value : direction) {
                value /= length;
            }
            light.Direction = {direction[0], direction[1], direction[2]};
            light.Position = {position[0], position[1], position[2]};
            light.Range = range;
            light.Attenuation0 = attenuation[0];
            light.Attenuation1 = attenuation[1];
            light.Attenuation2 = attenuation[2];
            FixedScene scene;
            scene.name = lc.name;
            scene.tolerance = lit_tolerance;
            scene.draw = [=](IDirect3DDevice9* d) {
                D3DMATERIAL9 material{};
                material.Diffuse = {0.9f, 0.6f, 0.3f, 1.0f};
                material.Ambient = material.Diffuse;
                apply_projection(d);
                d->SetRenderState(D3DRS_LIGHTING, TRUE);
                d->SetRenderState(D3DRS_AMBIENT, 0x00202020u);
                d->SetRenderState(D3DRS_COLORVERTEX, FALSE);
                d->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
                d->SetMaterial(&material);
                d->SetLight(0, &light);
                d->LightEnable(0, TRUE);
                d->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL);
                if (FAILED(d->BeginScene())) {
                    return false;
                }
                const HRESULT result = d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 17 * 17, static_cast<UINT>(indices->size() / 3), indices->data(), D3DFMT_INDEX16, vertices->data(), sizeof(LitVertex));
                d->EndScene();
                d->LightEnable(0, FALSE);
                d->SetRenderState(D3DRS_LIGHTING, FALSE);
                return SUCCEEDED(result);
            };
            scene.twin = twin_base();
            scene.twin.vertices = bytes_of(*vertices);
            scene.twin.stride = sizeof(LitVertex);
            scene.twin.vertex_count = static_cast<UINT>(vertices->size());
            scene.twin.indices = *indices;
            scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"NORMAL", DXGI_FORMAT_R32G32B32_FLOAT, 12}};
            scene.twin.vs = kLitVs;
            scene.twin.ps = kPassPs;
            const float ambient = 32.0f / 255.0f;
            const float type = lc.type == D3DLIGHT_DIRECTIONAL ? 0.0f : lc.type == D3DLIGHT_POINT ? 1.0f : 2.0f;
            scene.twin.constants = {0.9f, 0.6f, 0.3f, 1.0f,
                                    0.9f * ambient, 0.6f * ambient, 0.3f * ambient, 0.0f,
                                    position[0], position[1], position[2], range,
                                    direction[0], direction[1], direction[2], type,
                                    attenuation[0], attenuation[1], attenuation[2], attenuation[3],
                                    cones[0], cones[1], 0.0f, 0.0f};
            if ((caps.VertexProcessingCaps & D3DVTXPCAPS_DIRECTIONALLIGHTS) == 0 && lc.type == D3DLIGHT_DIRECTIONAL) {
                scene.unsupported = "no directional lights in the caps";
            }
            if ((caps.VertexProcessingCaps & D3DVTXPCAPS_POSITIONALLIGHTS) == 0 && lc.type != D3DLIGHT_DIRECTIONAL) {
                scene.unsupported = "no positional lights in the caps";
            }
            scenes.push_back(std::move(scene));
        }
    }
    {
        auto vertices = std::make_shared<std::vector<ColorVertex>>();
        auto indices = std::make_shared<std::vector<std::uint16_t>>();
        grid_fog(*vertices, *indices);
        struct FogCase {
            const char* name;
            D3DFOGMODE mode;
            float density;
        };
        for (const FogCase& fc : {FogCase{"fog linear", D3DFOG_LINEAR, 0.0f}, FogCase{"fog exp", D3DFOG_EXP, 1.5f}, FogCase{"fog exp2", D3DFOG_EXP2, 1.5f}}) {
            FixedScene scene;
            scene.name = fc.name;
            scene.tolerance = Tolerance::bytes(4, 0.002);
            scene.draw = [=](IDirect3DDevice9* d) {
                apply_projection(d);
                d->SetRenderState(D3DRS_FOGENABLE, TRUE);
                d->SetRenderState(D3DRS_FOGCOLOR, 0xFF3060C0u);
                d->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
                d->SetRenderState(D3DRS_FOGVERTEXMODE, fc.mode);
                d->SetRenderState(D3DRS_FOGSTART, bits(0.2f));
                d->SetRenderState(D3DRS_FOGEND, bits(0.9f));
                d->SetRenderState(D3DRS_FOGDENSITY, bits(fc.density));
                d->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
                d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
                if (FAILED(d->BeginScene())) {
                    return false;
                }
                const HRESULT result = d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 81, static_cast<UINT>(indices->size() / 3), indices->data(), D3DFMT_INDEX16, vertices->data(), sizeof(ColorVertex));
                d->EndScene();
                d->SetRenderState(D3DRS_FOGENABLE, FALSE);
                return SUCCEEDED(result);
            };
            scene.twin = twin_base();
            scene.twin.vertices = bytes_of(*vertices);
            scene.twin.stride = sizeof(ColorVertex);
            scene.twin.vertex_count = static_cast<UINT>(vertices->size());
            scene.twin.indices = *indices;
            scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"COLOR", DXGI_FORMAT_B8G8R8A8_UNORM, 12}};
            scene.twin.vs = kFogVs;
            scene.twin.ps = kFogPs;
            const float mode = fc.mode == D3DFOG_LINEAR ? 0.0f : fc.mode == D3DFOG_EXP ? 1.0f : 2.0f;
            scene.twin.constants = {0.2f, 0.9f, fc.density, mode, 0x30 / 255.0f, 0x60 / 255.0f, 0xC0 / 255.0f, 1.0f};
            if ((caps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX) == 0) {
                scene.unsupported = "no vertex fog in the caps";
            }
            scenes.push_back(std::move(scene));
        }
    }
    {
        auto tex0 = std::make_shared<Com<IDirect3DTexture9>>(D3d9Tools::texture(device, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED));
        auto tex1 = std::make_shared<Com<IDirect3DTexture9>>(D3d9Tools::texture(device, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED));
        std::string ignored;
        if (*tex0) {
            static_cast<void>(D3d9Tools::write_texture_level(tex0->get(), 0, 61, ignored));
        }
        if (*tex1) {
            static_cast<void>(D3d9Tools::write_texture_level(tex1->get(), 0, 62, ignored));
        }
        const std::vector<std::uint32_t> t0 = D3d9Tools::pattern(61, 32, 32);
        const std::vector<std::uint32_t> t1 = D3d9Tools::pattern(62, 16, 16);
        auto quad = std::make_shared<std::vector<TexturedVertex>>(std::vector<TexturedVertex>{{-0.75f, 0.75f, 0.5f, 0xC0FF8040u, 0, 0},
                                                                                              {0.75f, 0.75f, 0.5f, 0xC040FF80u, 1, 0},
                                                                                              {-0.75f, -0.75f, 0.5f, 0xC08040FFu, 0, 1},
                                                                                              {0.75f, -0.75f, 0.5f, 0xC0FFFF40u, 1, 1}});
        struct StageCase {
            const char* name;
            D3DTEXTUREOP op;
            const char* expression;
            DWORD cap;
        };
        const StageCase stages[] = {
            {"stage MODULATE", D3DTOP_MODULATE, "cur * t1.rgb", D3DTEXOPCAPS_MODULATE},
            {"stage ADD", D3DTOP_ADD, "cur + t1.rgb", D3DTEXOPCAPS_ADD},
            {"stage SUBTRACT", D3DTOP_SUBTRACT, "cur - t1.rgb", D3DTEXOPCAPS_SUBTRACT},
            {"stage MODULATE2X", D3DTOP_MODULATE2X, "cur * t1.rgb * 2", D3DTEXOPCAPS_MODULATE2X},
            {"stage ADDSIGNED", D3DTOP_ADDSIGNED, "cur + t1.rgb - 0.5", D3DTEXOPCAPS_ADDSIGNED},
            {"stage BLENDDIFFUSEALPHA", D3DTOP_BLENDDIFFUSEALPHA, "t1.rgb * c.a + cur * (1 - c.a)", D3DTEXOPCAPS_BLENDDIFFUSEALPHA},
            {"stage BLENDTEXTUREALPHA", D3DTOP_BLENDTEXTUREALPHA, "t1.rgb * t1.a + cur * (1 - t1.a)", D3DTEXOPCAPS_BLENDTEXTUREALPHA},
        };
        for (const StageCase& sc : stages) {
            FixedScene scene;
            scene.name = sc.name;
            scene.tolerance = Tolerance::bytes(3, 0.002);
            scene.draw = [=](IDirect3DDevice9* d) {
                apply_projection(d);
                D3d9Tools::sampler(d, 0, false);
                D3d9Tools::sampler(d, 1, false);
                d->SetTexture(0, tex0->get());
                d->SetTexture(1, tex1->get());
                d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
                d->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
                d->SetTextureStageState(1, D3DTSS_COLOROP, sc.op);
                d->SetTextureStageState(1, D3DTSS_COLORARG1, sc.op == D3DTOP_BLENDDIFFUSEALPHA || sc.op == D3DTOP_BLENDTEXTUREALPHA ? D3DTA_TEXTURE : D3DTA_CURRENT);
                d->SetTextureStageState(1, D3DTSS_COLORARG2, sc.op == D3DTOP_BLENDDIFFUSEALPHA || sc.op == D3DTOP_BLENDTEXTUREALPHA ? D3DTA_CURRENT : D3DTA_TEXTURE);
                d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                d->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
                d->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
                d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                if (FAILED(d->BeginScene())) {
                    return false;
                }
                const HRESULT result = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad->data(), sizeof(TexturedVertex));
                d->EndScene();
                d->SetTexture(0, nullptr);
                d->SetTexture(1, nullptr);
                d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
                d->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
                return SUCCEEDED(result);
            };
            scene.twin = twin_base();
            scene.twin.vertices = bytes_of(*quad);
            scene.twin.stride = sizeof(TexturedVertex);
            scene.twin.vertex_count = 4;
            scene.twin.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"COLOR", DXGI_FORMAT_B8G8R8A8_UNORM, 12}, {"TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, 16}};
            scene.twin.vs = kStageVs;
            scene.twin.ps = std::string("Texture2D t0 : register(t0); Texture2D t1x : register(t1); SamplerState s0 : register(s0);\n"
                                        "float4 main(float4 p : SV_Position, float4 c : COLOR0, float2 uv : TEXCOORD0) : SV_Target {\n"
                                        "  float3 cur = t0.Sample(s0, uv).rgb * c.rgb; float4 t1 = t1x.Sample(s0, uv);\n"
                                        "  return float4(saturate(") + sc.expression + "), c.a); }\n";
            TwinTexture a;
            a.width = 32;
            a.height = 32;
            a.bgra.resize(t0.size() * 4);
            std::memcpy(a.bgra.data(), t0.data(), a.bgra.size());
            TwinTexture b;
            b.width = 16;
            b.height = 16;
            b.bgra.resize(t1.size() * 4);
            std::memcpy(b.bgra.data(), t1.data(), b.bgra.size());
            scene.twin.textures.push_back(std::move(a));
            scene.twin.textures.push_back(std::move(b));
            if (!*tex0 || !*tex1) {
                scene.unsupported = "the stage textures cannot be created";
            } else if (caps.MaxTextureBlendStages < 2 || caps.MaxSimultaneousTextures < 2) {
                scene.unsupported = "fewer than two texture stages in the caps";
            } else if ((caps.TextureOpCaps & sc.cap) == 0) {
                scene.unsupported = "the operation is not in TextureOpCaps";
            }
            scenes.push_back(std::move(scene));
        }
        {
            auto colored = std::make_shared<std::vector<ColorVertex>>(std::vector<ColorVertex>{{-0.75f, 0.75f, 0.5f, 0xFFFFFFFFu}, {0.75f, 0.75f, 0.5f, 0xFFFFFFFFu}, {-0.75f, -0.75f, 0.5f, 0xFFFFFFFFu}, {0.75f, -0.75f, 0.5f, 0xFFFFFFFFu}});
            FixedScene scene;
            scene.name = "texture coordinates from camera space position";
            scene.tolerance = Tolerance::bytes(3, 0.002);
            scene.draw = [=](IDirect3DDevice9* d) {
                apply_projection(d);
                D3d9Tools::sampler(d, 0, false);
                d->SetTexture(0, tex0->get());
                D3DMATRIX transform{};
                transform._11 = 0.5f;
                transform._22 = -0.5f;
                transform._41 = 0.5f;
                transform._42 = 0.5f;
                transform._33 = 1.0f;
                transform._44 = 1.0f;
                d->SetTransform(D3DTS_TEXTURE0, &transform);
                d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
                d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
                d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
                d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
                if (FAILED(d->BeginScene())) {
                    return false;
                }
                const HRESULT result = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, colored->data(), sizeof(ColorVertex));
                d->EndScene();
                d->SetTexture(0, nullptr);
                d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
                d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
                return SUCCEEDED(result);
            };
            scene.twin = twin_base();
            scene.twin.vertices = bytes_of(*colored);
            scene.twin.stride = sizeof(ColorVertex);
            scene.twin.vertex_count = 4;
            scene.twin.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"COLOR", DXGI_FORMAT_B8G8R8A8_UNORM, 12}};
            scene.twin.vs = kTciVs;
            scene.twin.ps = kTciPs;
            TwinTexture a;
            a.width = 32;
            a.height = 32;
            a.bgra.resize(t0.size() * 4);
            std::memcpy(a.bgra.data(), t0.data(), a.bgra.size());
            scene.twin.textures.push_back(std::move(a));
            if (!*tex0) {
                scene.unsupported = "the texture cannot be created";
            } else if ((caps.VertexProcessingCaps & D3DVTXPCAPS_TEXGEN) == 0) {
                scene.unsupported = "no texture coordinate generation in the caps";
            }
            scenes.push_back(std::move(scene));
        }
    }
    {
        auto quad = std::make_shared<std::vector<ColorVertex>>(std::vector<ColorVertex>{{-0.75f, 0.75f, 0.5f, 0x00FF8000u}, {0.75f, 0.75f, 0.5f, 0xFFFF8000u}, {-0.75f, -0.75f, 0.5f, 0x00FF8000u}, {0.75f, -0.75f, 0.5f, 0xFFFF8000u}});
        FixedScene scene;
        scene.name = "alpha test GREATER 0x80";
        scene.tolerance = Tolerance::bytes(3, 0.02);
        scene.draw = [=](IDirect3DDevice9* d) {
            apply_projection(d);
            d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            d->SetRenderState(D3DRS_ALPHAREF, 0x80);
            d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            if (FAILED(d->BeginScene())) {
                return false;
            }
            const HRESULT result = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad->data(), sizeof(ColorVertex));
            d->EndScene();
            d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            return SUCCEEDED(result);
        };
        scene.twin = twin_base();
        scene.twin.vertices = bytes_of(*quad);
        scene.twin.stride = sizeof(ColorVertex);
        scene.twin.vertex_count = 4;
        scene.twin.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"COLOR", DXGI_FORMAT_B8G8R8A8_UNORM, 12}};
        scene.twin.vs = kAlphaVs;
        scene.twin.ps = kAlphaPs;
        if ((caps.AlphaCmpCaps & D3DPCMPCAPS_GREATER) == 0) {
            scene.unsupported = "no GREATER alpha comparison in the caps";
        }
        scenes.push_back(std::move(scene));
    }
    {
        auto quad = std::make_shared<std::vector<ColorVertex>>(std::vector<ColorVertex>{{-1.0f, 1.0f, 0.5f, 0xFFFFFF00u}, {1.0f, 1.0f, 0.5f, 0xFFFFFF00u}, {-1.0f, -1.0f, 0.5f, 0xFFFFFF00u}, {1.0f, -1.0f, 0.5f, 0xFFFFFF00u}});
        const float plane0[4] = {1.0f, 0.5f, 0.0f, 0.1f};
        const float plane1[4] = {-0.2f, 1.0f, 0.0f, 0.3f};
        FixedScene scene;
        scene.name = "user clip planes";
        scene.tolerance = Tolerance::bytes(0, 0.03);
        scene.draw = [=](IDirect3DDevice9* d) {
            apply_projection(d);
            d->SetClipPlane(0, plane0);
            d->SetClipPlane(1, plane1);
            d->SetRenderState(D3DRS_CLIPPLANEENABLE, 3);
            d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            if (FAILED(d->BeginScene())) {
                return false;
            }
            const HRESULT result = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad->data(), sizeof(ColorVertex));
            d->EndScene();
            d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
            return SUCCEEDED(result);
        };
        scene.twin = twin_base();
        scene.twin.vertices = bytes_of(*quad);
        scene.twin.stride = sizeof(ColorVertex);
        scene.twin.vertex_count = 4;
        scene.twin.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        scene.twin.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}, {"COLOR", DXGI_FORMAT_B8G8R8A8_UNORM, 12}};
        scene.twin.vs = kClipVs;
        scene.twin.ps = kClipPs;
        scene.twin.constants = {plane0[0], plane0[1], plane0[2], plane0[3], plane1[0], plane1[1], plane1[2], plane1[3]};
        if (caps.MaxUserClipPlanes < 2) {
            scene.unsupported = "fewer than two user clip planes in the caps";
        }
        scenes.push_back(std::move(scene));
    }
    return scenes;
}

}  // namespace lsw::suites::sanity::d3d9
