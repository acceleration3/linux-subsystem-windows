#include "sanity/shader/dxbc_checks.hpp"

#include "core/text.hpp"
#include "sanity/shader/kit.hpp"
#include "support/d3d10_rig.hpp"
#include "support/d3d10_tools.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/d3d9_twin.hpp"
#include "support/oracle_rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::shader {

namespace {

using Rgba = std::array<double, 4>;

constexpr UINT kSize = 64;

std::uint8_t byte_of(double value) {
    return static_cast<std::uint8_t>(std::lround(std::min(std::max(value, 0.0), 1.0) * 255.0));
}

double saturate(double value) { return std::min(std::max(value, 0.0), 1.0); }

double frac(double value) { return value - std::floor(value); }

struct NineCase {
    std::string name;
    const char* vs_profile;
    const char* ps_profile;
    DWORD vs_major;
    DWORD ps_major;
    std::string vs9;
    std::string ps9;
    std::string vs11;
    std::string ps11;
    bool constants;
    std::function<Rgba(double, double)> expected;
};

NineCase make_case(const std::string& name, const char* vs_profile, const char* ps_profile, DWORD vs_major, DWORD ps_major, const std::string& vs_body9,
                   const std::string& vs_body11, const std::string& shade, std::function<Rgba(double, double)> expected, bool constants = false) {
    NineCase c;
    c.name = name;
    c.vs_profile = vs_profile;
    c.ps_profile = ps_profile;
    c.vs_major = vs_major;
    c.ps_major = ps_major;
    c.vs9 = "struct O { float4 p : POSITION; float2 t : TEXCOORD0; };\n" + vs_body9;
    c.vs11 = "struct O { float4 p : SV_Position; float2 t : TEXCOORD0; };\n" + vs_body11;
    c.ps9 = shade + "float4 main(float2 t : TEXCOORD0) : COLOR { return shade(t); }\n";
    c.ps11 = shade + "float4 main(float4 p : SV_Position, float2 t : TEXCOORD0) : SV_Target { return shade(t); }\n";
    c.constants = constants;
    c.expected = std::move(expected);
    return c;
}

std::vector<NineCase> nine_cases() {
    const std::string plain9 =
        "O main(float3 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = float4(p.x - 1.0 / 64.0, p.y + 1.0 / 64.0, p.z, 1); o.t = t; return o; }\n";
    const std::string plain11 = "O main(float2 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = float4(p, 0, 1); o.t = t; return o; }\n";
    std::vector<NineCase> cases;
    cases.push_back(make_case("vs_2_0 and ps_2_0 arithmetic", "vs_2_0", "ps_2_0", 2, 2, plain9, plain11,
                              "float4 shade(float2 t) { float a = t.x * t.y; float b = lerp(t.x, t.y, 0.25); float c = saturate(t.x * 2.0 - t.y); float d = abs(t.x - t.y); return float4(a, b, c, d); }\n",
                              [](double u, double v) { return Rgba{u * v, u + (v - u) * 0.25, saturate(u * 2.0 - v), std::fabs(u - v)}; }));
    cases.push_back(make_case("vs_2_0 with constant registers", "vs_2_0", "ps_2_0", 2, 2,
                              "float4 k : register(c0);\nO main(float3 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = float4(p.x - 1.0 / 64.0, p.y + 1.0 / 64.0, p.z, 1); o.t = t * k.xy + k.zw; return o; }\n",
                              "O main(float2 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = float4(p, 0, 1); o.t = t * float2(0.5, 0.25) + float2(0.25, 0.5); return o; }\n",
                              "float4 shade(float2 t) { return float4(t.x, t.y, t.x * t.y, 1); }\n",
                              [](double u, double v) {
                                  const double x = u * 0.5 + 0.25;
                                  const double y = v * 0.25 + 0.5;
                                  return Rgba{x, y, x * y, 1};
                              },
                              true));
    cases.push_back(make_case("vs_2_a and ps_2_a transcendental functions", "vs_2_a", "ps_2_a", 2, 2, plain9, plain11,
                              "float4 shade(float2 t) { return float4(sin(t.x * 3.0) * 0.5 + 0.5, cos(t.y * 3.0) * 0.5 + 0.5, exp2(-t.x * 2.0), pow(t.y, 2.0)); }\n",
                              [](double u, double v) { return Rgba{std::sin(u * 3.0) * 0.5 + 0.5, std::cos(v * 3.0) * 0.5 + 0.5, std::exp2(-u * 2.0), v * v}; }));
    cases.push_back(make_case("vs_2_0 and ps_2_b chained arithmetic", "vs_2_0", "ps_2_b", 2, 2, plain9, plain11,
                              "float4 shade(float2 t) { float s = 0; float2 q = t; s += dot(q, float2(0.5, 0.25)); q = q.yx * 0.9 + 0.05; s += dot(q, float2(0.25, 0.5));\n"
                              "  q = frac(q * 3.0); s += q.x * q.y; return float4(s * 0.5, q.x, q.y, 1); }\n",
                              [](double u, double v) {
                                  double s = 0;
                                  double qx = u;
                                  double qy = v;
                                  s += qx * 0.5 + qy * 0.25;
                                  const double nx = qy * 0.9 + 0.05;
                                  const double ny = qx * 0.9 + 0.05;
                                  qx = nx;
                                  qy = ny;
                                  s += qx * 0.25 + qy * 0.5;
                                  qx = frac(qx * 3.0);
                                  qy = frac(qy * 3.0);
                                  s += qx * qy;
                                  return Rgba{s * 0.5, qx, qy, 1};
                              }));
    cases.push_back(make_case("vs_3_0 and ps_3_0 loop and branch", "vs_3_0", "ps_3_0", 3, 3, plain9, plain11,
                              "float4 shade(float2 t) { float acc = 0; for (int i = 0; i < 4; i++) { acc += frac(t.x * (i + 1)) * 0.25; } if (t.y > 0.5) { acc *= 0.5; } return float4(acc, t.y, 0.25, 1); }\n",
                              [](double u, double v) {
                                  double acc = 0;
                                  for (int i = 0; i < 4; ++i) {
                                      acc += frac(u * (i + 1)) * 0.25;
                                  }
                                  if (v > 0.5) {
                                      acc *= 0.5;
                                  }
                                  return Rgba{acc, v, 0.25, 1};
                              }));
    return cases;
}

SceneImage expected_image(const NineCase& c) {
    SceneImage image;
    image.width = kSize;
    image.height = kSize;
    image.rgba = false;
    image.data.resize(static_cast<std::size_t>(kSize) * kSize * 4);
    for (UINT y = 0; y < kSize; ++y) {
        for (UINT x = 0; x < kSize; ++x) {
            const Rgba value = c.expected((x + 0.5) / kSize, (y + 0.5) / kSize);
            std::uint8_t* out = image.data.data() + (static_cast<std::size_t>(y) * kSize + x) * 4;
            out[0] = byte_of(value[2]);
            out[1] = byte_of(value[1]);
            out[2] = byte_of(value[0]);
            out[3] = byte_of(value[3]);
        }
    }
    return image;
}

bool render_nine(IDirect3DDevice9* device, const NineCase& c, SceneImage& out, std::string& error) {
    Com<IDirect3DVertexShader9> vs = D3d9Tools::vs(device, c.vs9, c.vs_profile, error);
    Com<IDirect3DPixelShader9> ps = D3d9Tools::ps(device, c.ps9, c.ps_profile, error);
    if (!vs || !ps) {
        return false;
    }
    HRESULT result = S_OK;
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
    if (!target) {
        error = "the render target cannot be created: " + D3d9Tools::hr(result);
        return false;
    }
    Com<IDirect3DSurface9> saved_target;
    Com<IDirect3DSurface9> saved_depth;
    device->GetRenderTarget(0, saved_target.put());
    device->GetDepthStencilSurface(saved_depth.put());
    device->SetRenderTarget(0, target.get());
    device->SetDepthStencilSurface(nullptr);
    D3d9Tools::prepare(device, false);
    D3d9Tools::clear(device, D3DCOLOR_ARGB(0, 0, 0, 0));
    device->SetVertexShader(vs.get());
    device->SetPixelShader(ps.get());
    if (c.constants) {
        const float k[4] = {0.5f, 0.25f, 0.25f, 0.5f};
        device->SetVertexShaderConstantF(0, k, 1);
    }
    const float quad[20] = {-1, 1, 0, 0, 0, 1, 1, 0, 1, 0, -1, -1, 0, 0, 1, 1, -1, 0, 1, 1};
    device->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
    device->BeginScene();
    const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 20);
    device->EndScene();
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    const bool read = SUCCEEDED(drawn) && D3d9Tools::read_surface(device, target.get(), out, error);
    device->SetRenderTarget(0, saved_target.get());
    device->SetDepthStencilSurface(saved_depth.get());
    if (FAILED(drawn)) {
        error = "the draw failed with " + D3d9Tools::hr(drawn);
        return false;
    }
    return read;
}


const char* kPointVs = "struct V { uint id : ID; };\nV main(uint id : SV_VertexID) { V v; v.id = id; return v; }\n";

const char* kQuadGs =
    "struct V { uint id : ID; };\nstruct O { float4 p : SV_Position; nointerpolation uint id : ID; };\n"
    "[maxvertexcount(4)] void main(point V p[1], inout TriangleStream<O> s) {\n"
    "  uint id = p[0].id; float cx = float((id & 3u) * 16u + 8u); float cy = float((id >> 2) * 16u + 8u);\n"
    "  O o; o.id = id;\n"
    "  float2 c[4] = {float2(-6, -6), float2(6, -6), float2(-6, 6), float2(6, 6)};\n"
    "  [unroll] for (int i = 0; i < 4; ++i) { float2 px = float2(cx, cy) + c[i]; o.p = float4(px.x / 32.0 - 1.0, 1.0 - px.y / 32.0, 0, 1); s.Append(o); }\n"
    "}\n";

const char* kHashPs =
    "struct O { float4 p : SV_Position; nointerpolation uint id : ID; };\n"
    "float4 main(O i) : SV_Target { uint v = (i.id * 37u) ^ (uint(i.p.x) + 3u * uint(i.p.y)); "
    "return float4(float(v & 255u) / 255.0, float((v >> 3) & 255u) / 255.0, float(i.id) / 15.0, 1); }\n";

bool geometry_scene11(D3d11Device& device, SceneImage& out, std::string& error) {
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, kPointVs, error);
    Com<ID3D11GeometryShader> gs = D3d11Tools::gs(device, kQuadGs, error);
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, kHashPs, error);
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    if (!vs || !gs || !ps || !target || !view) {
        if (error.empty()) {
            error = "creating the geometry scene failed";
        }
        return false;
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    Com<ID3D11RasterizerState> state;
    device.device->CreateRasterizerState(&raster, state.put());
    D3d11Tools::clear(device, view.get(), 0, 0, 0, 0);
    ID3D11RenderTargetView* views[1] = {view.get()};
    device.context->OMSetRenderTargets(1, views, nullptr);
    D3d11Tools::set_viewport(device, kSize, kSize);
    device.context->RSSetState(state.get());
    device.context->IASetInputLayout(nullptr);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    device.context->VSSetShader(vs.get(), nullptr, 0);
    device.context->GSSetShader(gs.get(), nullptr, 0);
    device.context->PSSetShader(ps.get(), nullptr, 0);
    device.context->Draw(16, 0);
    TexelImage image;
    const bool read = D3d11Tools::read_texture(device, target.get(), 0, image, error);
    D3d11Tools::reset_state(device);
    if (!read) {
        return false;
    }
    out.width = kSize;
    out.height = kSize;
    out.rgba = true;
    out.data = std::move(image.data);
    return true;
}

bool geometry_scene10(D3d10Device& device, SceneImage& out, std::string& error) {
    Com<ID3D10VertexShader> vs = D3d10Tools::vs(device, kPointVs, error);
    Com<ID3D10GeometryShader> gs = D3d10Tools::gs(device, kQuadGs, error);
    Com<ID3D10PixelShader> ps = D3d10Tools::ps(device, kHashPs, error);
    Com<ID3D10Texture2D> target = D3d10Tools::target(device, kSize, kSize);
    Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(device, target.get());
    if (!vs || !gs || !ps || !target || !view) {
        if (error.empty()) {
            error = "creating the geometry scene failed";
        }
        return false;
    }
    D3D10_RASTERIZER_DESC raster{};
    raster.FillMode = D3D10_FILL_SOLID;
    raster.CullMode = D3D10_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    Com<ID3D10RasterizerState> state;
    device.device->CreateRasterizerState(&raster, state.put());
    D3d10Tools::clear(device, view.get(), 0, 0, 0, 0);
    ID3D10RenderTargetView* views[1] = {view.get()};
    device.device->OMSetRenderTargets(1, views, nullptr);
    D3d10Tools::set_viewport(device, kSize, kSize);
    device.device->RSSetState(state.get());
    device.device->IASetInputLayout(nullptr);
    device.device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
    device.device->VSSetShader(vs.get());
    device.device->GSSetShader(gs.get());
    device.device->PSSetShader(ps.get());
    device.device->Draw(16, 0);
    TexelImage image;
    const bool read = D3d10Tools::read_texture(device, target.get(), image, error);
    device.device->ClearState();
    device.device->Flush();
    if (!read) {
        return false;
    }
    out.width = kSize;
    out.height = kSize;
    out.rgba = true;
    out.data = std::move(image.data);
    return true;
}

std::uint32_t hash_word(std::uint32_t id, bool sm5) {
    std::uint32_t v = id * 2654435761u;
    v ^= v >> 13;
    v *= 1597334677u;
    if (sm5) {
        std::uint32_t bits = 0;
        for (std::uint32_t x = v; x != 0; x >>= 1) {
            bits += x & 1u;
        }
        v ^= bits * 16777619u;
        std::uint32_t reversed = 0;
        for (int i = 0; i < 32; ++i) {
            reversed |= ((id >> i) & 1u) << (31 - i);
        }
        v ^= reversed;
    } else {
        std::uint32_t top = 0;
        for (std::uint32_t x = v | 1u; x > 1; x >>= 1) {
            ++top;
        }
        v ^= top * 16777619u;
    }
    return v;
}

const char* kHashCs =
    "RWByteAddressBuffer o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint v = id.x * 2654435761u; v ^= v >> 13; v *= 1597334677u;\n"
    "#ifdef SM5\n"
    "  v ^= countbits(v) * 16777619u; v ^= reversebits(id.x);\n"
    "#else\n"
    "  v ^= firstbithigh(v | 1u) * 16777619u;\n"
    "#endif\n"
    "  o.Store(id.x * 4, v);\n"
    "  uint previous; o.InterlockedAdd(1024 + (id.x & 15u) * 4, 1u, previous);\n"
    "}\n";

void compute_level(D3d11Device& device, bool sm5, Problems& problems, const std::string& level) {
    std::string error;
    const D3D_SHADER_MACRO macros[] = {{"SM5", "1"}, {nullptr, nullptr}};
    Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device, kHashCs, error, "main", sm5 ? macros : nullptr);
    HRESULT result = S_OK;
    const std::vector<std::uint8_t> zeros(1024 + 64, 0);
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 1024 + 64, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, zeros.data(), &result);
    Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
    if (!shader || !buffer || !uav) {
        problems.add(level + " compute scene: " + (error.empty() ? "creating the buffer failed: " + D3d11Tools::hr(result) : error));
        return;
    }
    ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
    device.context->CSSetShader(shader.get(), nullptr, 0);
    device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device.context->Dispatch(4, 1, 1);
    D3d11Tools::reset_state(device);
    const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
    if (words.size() != (1024 + 64) / 4) {
        problems.add(level + " compute scene: " + error);
        return;
    }
    for (std::uint32_t i = 0; i < 256; ++i) {
        if (words[i] != hash_word(i, sm5)) {
            problems.add(level + " compute scene: word " + std::to_string(i) + " is 0x" + Text::hex(words[i], 8) + ", expected 0x" + Text::hex(hash_word(i, sm5), 8));
            break;
        }
    }
    for (std::uint32_t bucket = 0; bucket < 16; ++bucket) {
        if (words[256 + bucket] != 16) {
            problems.add(level + " compute scene: atomic bucket " + std::to_string(bucket) + " holds " + std::to_string(words[256 + bucket]) + ", expected 16");
            break;
        }
    }
}

void resource_array_scene(D3d11Device& device, Problems& problems) {
    const char* source =
        "Texture2D<float4> tex[3] : register(t0);\nRWByteAddressBuffer o : register(u0);\n[numthreads(4, 4, 1)]\n"
        "void main(uint3 id : SV_DispatchThreadID) { o.Store4((id.y * 4 + id.x) * 16, asuint(tex[0].Load(int3(id.xy, 0)) + tex[1].Load(int3(id.xy, 0)) * 2.0 + tex[2].Load(int3(id.xy, 0)) * 3.0)); }\n";
    std::string error;
    Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device, source, error);
    if (!shader) {
        problems.add("resource array scene: " + error);
        return;
    }
    Com<ID3D11ShaderResourceView> srvs[3];
    Com<ID3D11Texture2D> textures[3];
    for (int k = 0; k < 3; ++k) {
        std::vector<float> texels(4 * 4 * 4);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                float* texel = texels.data() + (y * 4 + x) * 4;
                texel[0] = static_cast<float>(x + 1);
                texel[1] = static_cast<float>(y + 1);
                texel[2] = static_cast<float>(k + 1);
                texel[3] = 0.5f;
            }
        }
        D3D11_SUBRESOURCE_DATA initial{texels.data(), 4 * 16, 0};
        textures[k] = D3d11Tools::texture(device, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE), &initial);
        srvs[k] = textures[k] ? D3d11Tools::srv(device, textures[k].get()) : Com<ID3D11ShaderResourceView>();
        if (!srvs[k]) {
            problems.add("resource array scene: creating texture " + std::to_string(k) + " failed");
            return;
        }
    }
    HRESULT result = S_OK;
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 16 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, &result);
    Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
    if (!uav) {
        problems.add("resource array scene: creating the output failed: " + D3d11Tools::hr(result));
        return;
    }
    ID3D11ShaderResourceView* views[3] = {srvs[0].get(), srvs[1].get(), srvs[2].get()};
    ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
    device.context->CSSetShader(shader.get(), nullptr, 0);
    device.context->CSSetShaderResources(0, 3, views);
    device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device.context->Dispatch(1, 1, 1);
    D3d11Tools::reset_state(device);
    const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
    if (words.size() != 16 * 4) {
        problems.add("resource array scene: " + error);
        return;
    }
    for (int t = 0; t < 16; ++t) {
        const float x = static_cast<float>(t % 4 + 1);
        const float y = static_cast<float>(t / 4 + 1);
        const float want[4] = {x * 6.0f, y * 6.0f, 1.0f + 2.0f * 2.0f + 3.0f * 3.0f, 0.5f * 6.0f};
        for (int c = 0; c < 4; ++c) {
            float got = 0.0f;
            std::memcpy(&got, &words[static_cast<std::size_t>(t) * 4 + static_cast<std::size_t>(c)], 4);
            if (got != want[c]) {
                problems.add("resource array scene: texel " + std::to_string(t) + " channel " + std::to_string(c) + " is " + std::to_string(got) + ", expected " + std::to_string(want[c]));
                return;
            }
        }
    }
}

void tessellation_scene(D3d11Device& device, Problems& problems) {
    const char* vs = "struct VO { float2 p : POSITION; };\n"
                     "VO main(uint id : SV_VertexID) { float2 c[4] = {float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1)}; VO o; o.p = c[id]; return o; }\n";
    const char* hs =
        "struct VO { float2 p : POSITION; };\nstruct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
        "PC pc() { PC o; o.e[0] = 4; o.e[1] = 4; o.e[2] = 4; o.e[3] = 4; o.i[0] = 4; o.i[1] = 4; return o; }\n"
        "[domain(\"quad\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const char* ds =
        "struct VO { float2 p : POSITION; };\nstruct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\nstruct DO { float4 pos : SV_Position; };\n"
        "[domain(\"quad\")] DO main(PC pc, float2 uv : SV_DomainLocation, const OutputPatch<VO, 4> p) {\n"
        "  float2 a = lerp(p[0].p, p[1].p, uv.x); float2 b = lerp(p[2].p, p[3].p, uv.x); DO o; o.pos = float4(lerp(a, b, uv.y), 0, 1); return o; }\n";
    const char* ps = "float4 main() : SV_Target { return float4(0.2, 0.6, 1.0, 1.0); }\n";
    std::string error;
    Com<ID3D11VertexShader> vertex = D3d11Tools::vs(device, vs, error);
    Com<ID3D11HullShader> hull = D3d11Tools::hs(device, hs, error);
    Com<ID3D11DomainShader> domain = D3d11Tools::ds(device, ds, error);
    Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device, ps, error);
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    if (!vertex || !hull || !domain || !pixel || !target || !view) {
        problems.add("tessellation scene: " + (error.empty() ? std::string("creating the resources failed") : error));
        return;
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    Com<ID3D11RasterizerState> state;
    device.device->CreateRasterizerState(&raster, state.put());
    D3d11Tools::clear(device, view.get(), 0, 0, 0, 0);
    ID3D11RenderTargetView* views[1] = {view.get()};
    device.context->OMSetRenderTargets(1, views, nullptr);
    D3d11Tools::set_viewport(device, kSize, kSize);
    device.context->RSSetState(state.get());
    device.context->IASetInputLayout(nullptr);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    device.context->VSSetShader(vertex.get(), nullptr, 0);
    device.context->HSSetShader(hull.get(), nullptr, 0);
    device.context->DSSetShader(domain.get(), nullptr, 0);
    device.context->PSSetShader(pixel.get(), nullptr, 0);
    device.context->Draw(4, 0);
    TexelImage image;
    const bool read = D3d11Tools::read_texture(device, target.get(), 0, image, error);
    D3d11Tools::reset_state(device);
    if (!read) {
        problems.add("tessellation scene: " + error);
        return;
    }
    const std::uint32_t want = 51u | (153u << 8) | (255u << 16) | (255u << 24);
    for (UINT y = 2; y < kSize - 2; ++y) {
        for (UINT x = 2; x < kSize - 2; ++x) {
            if (image.pixel(x, y) != want) {
                problems.add("tessellation scene: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) +
                             " inside a full-target quad patch, expected 0x" + Text::hex(want, 8));
                return;
            }
        }
    }
}

void shader_model_51(D3d12Rig& rig, Problems& problems) {
    const char* source =
        "Texture2D<uint> tex[4] : register(t0, space1);\nRWByteAddressBuffer o : register(u0, space2);\ncbuffer C : register(b0) { uint base; };\n[numthreads(4, 4, 1)]\n"
        "void main(uint3 id : SV_DispatchThreadID) { for (uint w = 0; w < 4; ++w) { o.Store((w * 16 + id.y * 4 + id.x) * 4, tex[w].Load(int3(id.xy, 0)) + base); } }\n";
    std::string error;
    D3d12Root root;
    root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 1);
    root.uav(0, 2);
    root.constants(1, 0);
    ID3D12RootSignature* signature = root.build(rig, error);
    ID3D12PipelineState* state = signature != nullptr ? D3d12Tools::compute_from(rig, signature, source, error) : nullptr;
    ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
    ID3D12Resource* output = D3d12Tools::device_buffer(rig, 64 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (state == nullptr || heap == nullptr || output == nullptr) {
        problems.add("shader model 5.1 scene: " + (error.empty() ? std::string("creating the resources failed") : error));
        return;
    }
    const UINT increment = rig.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Resource* textures[4];
    for (UINT k = 0; k < 4; ++k) {
        textures[k] = D3d12Tools::texture(rig, D3d12Tools::texture_desc(4, 4, DXGI_FORMAT_R32_UINT), D3D12_RESOURCE_STATE_COPY_DEST);
        std::vector<std::uint32_t> texels(16);
        for (UINT t = 0; t < 16; ++t) {
            texels[t] = k * 1000 + t;
        }
        if (textures[k] == nullptr || !D3d12Tools::write_texture(rig, textures[k], D3D12_RESOURCE_STATE_COPY_DEST, 0, texels.data(), error)) {
            problems.add("shader model 5.1 scene: texture " + std::to_string(k) + ": " + error);
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        rig.device()->CreateShaderResourceView(textures[k], &view, D3d12Tools::cpu_at(heap, k, increment));
    }
    const bool ran = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            for (ID3D12Resource* texture : textures) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            ID3D12DescriptorHeap* heaps[] = {heap};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(state);
            list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
            const UINT base = 7;
            list->SetComputeRoot32BitConstants(2, 1, &base, 0);
            list->Dispatch(1, 1, 1);
        },
        error);
    std::vector<std::uint32_t> words;
    if (!ran || !D3d12Tools::read_words(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 64, words, error)) {
        problems.add("shader model 5.1 scene: " + error);
        return;
    }
    for (UINT k = 0; k < 4; ++k) {
        for (UINT t = 0; t < 16; ++t) {
            if (words[k * 16 + t] != k * 1000 + t + 7) {
                problems.add("shader model 5.1 scene: element " + std::to_string(k * 16 + t) + " is " + std::to_string(words[k * 16 + t]) + ", expected " + std::to_string(k * 1000 + t + 7));
                return;
            }
        }
    }
}

}  // namespace

Outcome DxbcChecks::sm2_sm3(Context& context) {
    if (!Kit::enabled("d3d9")) {
        return Context::skip("D3D9 excluded by LSW_SUITES_SKIP_APIS");
    }
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(false, error) || !rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    D3DCAPS9 caps{};
    rig.dev()->GetDeviceCaps(&caps);
    D3d9Rig ex;
    const bool have_ex = D3d9Rig::create_ex_available() && ex.open(true, error) && ex.create(settings, error);
    if (!have_ex) {
        context.note("the D3D9Ex reference device is unavailable: " + error);
    }
    D3d9Twin twin;
    const bool have_twin = Kit::enabled("d3d11") && twin.open(error);
    if (!have_twin) {
        context.note("the WARP twin is unavailable or D3D11 is excluded: " + error);
    }
    Problems problems;
    int rendered = 0;
    for (const NineCase& c : nine_cases()) {
        if (caps.VertexShaderVersion < D3DVS_VERSION(c.vs_major, 0) || caps.PixelShaderVersion < D3DPS_VERSION(c.ps_major, 0)) {
            context.note(c.name + ": skipped, the caps report vertex shader version 0x" + Text::hex(caps.VertexShaderVersion, 8) + " and pixel shader version 0x" +
                         Text::hex(caps.PixelShaderVersion, 8));
            continue;
        }
        SceneImage image;
        if (!render_nine(rig.dev(), c, image, error)) {
            problems.add(c.name + ": " + error);
            continue;
        }
        ++rendered;
        std::string detail;
        if (!OracleRig::judge(context, "shader " + c.name + " reference", expected_image(c), image, Tolerance::bytes(4, 0.004), detail)) {
            problems.add(detail + " (against the CPU reference)");
        }
        if (have_ex) {
            SceneImage other;
            if (!render_nine(ex.dev(), c, other, error)) {
                problems.add(c.name + " on D3D9Ex: " + error);
            } else if (!OracleRig::judge(context, "shader " + c.name + " D3D9Ex", other, image, Tolerance::bytes(2, 0.002), detail)) {
                problems.add(detail + " (against the D3D9Ex device)");
            }
        }
        if (have_twin) {
            TwinScene scene;
            scene.width = kSize;
            scene.height = kSize;
            const float quad[16] = {-1, 1, 0, 0, 1, 1, 1, 0, -1, -1, 0, 1, 1, -1, 1, 1};
            scene.vertices.resize(sizeof(quad));
            std::memcpy(scene.vertices.data(), quad, sizeof(quad));
            scene.stride = 16;
            scene.vertex_count = 4;
            scene.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            scene.elements = {{"POSITION", DXGI_FORMAT_R32G32_FLOAT, 0}, {"TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, 8}};
            const std::string& vs11 = c.vs11;
            scene.vs = vs11;
            scene.ps = c.ps11;
            twin.compare(context, problems, "shader " + c.name, image, scene, Tolerance::bytes(4, 0.004));
        }
    }
    context.measure("shader.sm2_sm3.rendered", rendered);
    if (rendered == 0) {
        return Context::skip("the caps report no vertex and pixel shader version of the tested set");
    }
    return problems.outcome();
}

Outcome DxbcChecks::sm4_sm5(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("d3d11")) {
        AvAdapter adapter;
        std::string error;
        if (!TargetAdapter::pick(adapter, error)) {
            problems.add("D3D11: " + error);
        } else {
            const struct {
                D3D_FEATURE_LEVEL level;
                const char* name;
                bool sm5;
            } levels[] = {{D3D_FEATURE_LEVEL_10_0, "shader model 4.0", false}, {D3D_FEATURE_LEVEL_10_1, "shader model 4.1", false}, {D3D_FEATURE_LEVEL_11_0, "shader model 5.0", true}};
            for (const auto& item : levels) {
                D3d11Device av;
                D3d11Device warp;
                if (!av.create_ex(adapter.luid, &item.level, 1, 0, error) || av.level != item.level) {
                    context.note(std::string(item.name) + ": no device at this feature level on the adapter: " + error);
                    continue;
                }
                const D3D_FEATURE_LEVEL level = item.level;
                if (!warp.create_warp(&level, 1, 0, error)) {
                    problems.add(std::string(item.name) + ": the WARP oracle is unavailable: " + error);
                    continue;
                }
                exercised = true;
                SceneImage actual;
                SceneImage expected;
                std::string detail;
                if (!geometry_scene11(av, actual, error)) {
                    problems.add(std::string(item.name) + " geometry scene: " + error);
                } else if (!geometry_scene11(warp, expected, error)) {
                    problems.add(std::string(item.name) + " WARP geometry scene: " + error);
                } else if (!OracleRig::judge(context, std::string("geometry ") + item.name, expected, actual, Tolerance::bytes(2, 0.002), detail)) {
                    problems.add(detail + " (against WARP)");
                }
                D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS options{};
                av.device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &options, sizeof(options));
                if (item.sm5 || options.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x) {
                    compute_level(av, item.sm5, problems, item.name);
                } else {
                    context.note(std::string(item.name) + ": compute shaders are not reported at this feature level");
                }
                if (item.sm5) {
                    resource_array_scene(av, problems);
                    tessellation_scene(av, problems);
                }
            }
        }
        D3d10Rig rig10;
        if (rig10.open(false, error)) {
            rig10.compare(context, problems, "shader model 4.0 geometry on D3D10", geometry_scene10, Tolerance::bytes(2, 0.002));
        } else {
            context.note("D3D10 is unavailable: " + error);
        }
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        AvAdapter adapter;
        std::string error;
        if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("D3D12: " + error);
        } else {
            shader_model_51(rig, problems);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

Outcome DxbcChecks::invalid_diagnostics(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    const char* broken = "float4 main(float4 p : POSITION) : SV_Position { return p +; }\n";
    const char* unknown = "float4 main(float4 p : POSITION) : SV_Position { return undeclared_function(p); }\n";
    const char* wrong_type = "float4 main(float4 p : POSITION) : SV_Position { float3x3 m = p; return p; }\n";
    const char* valid_vs = "float4 main(float4 p : POSITION) : SV_Position { return p; }\n";
    const char* valid_ps = "float4 main() : SV_Target { return float4(0.2, 0.4, 0.6, 1.0); }\n";
    for (const char* source : {broken, unknown, wrong_type}) {
        std::vector<std::uint8_t> code;
        std::string error;
        if (D3d11Tools::compile(source, "main", "vs_4_0", code, error)) {
            problems.add(std::string("D3DCompile accepted invalid HLSL: ") + source);
        } else if (error.empty()) {
            problems.add(std::string("D3DCompile failed without a diagnostic for: ") + source);
        }
    }
    {
        std::vector<std::uint8_t> code;
        std::string error;
        if (D3d11Tools::compile(valid_vs, "no_such_entry", "vs_4_0", code, error) || error.empty()) {
            problems.add("D3DCompile with a missing entry point did not fail with a diagnostic");
        }
    }
    std::vector<std::uint8_t> good_vs;
    std::vector<std::uint8_t> good_ps;
    std::string error;
    if (!D3d11Tools::compile(valid_vs, "main", "vs_4_0", good_vs, error) || !D3d11Tools::compile(valid_ps, "main", "ps_4_0", good_ps, error)) {
        return Outcome::fail("the valid reference shaders do not compile: " + error);
    }
    const std::vector<std::uint8_t> garbage = [] {
        std::vector<std::uint8_t> bytes(256);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::uint8_t>(i * 37 + 11);
        }
        return bytes;
    }();
    const std::vector<std::uint8_t> truncated(good_vs.begin(), good_vs.begin() + static_cast<std::ptrdiff_t>(good_vs.size() / 2));
    if (Kit::enabled("d3d11")) {
        exercised = true;
        AvAdapter adapter;
        D3d11Device device;
        if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
            problems.add("D3D11: " + error);
        } else {
            const struct {
                const char* name;
                const std::vector<std::uint8_t>* bytes;
            } cases[] = {{"garbage bytes", &garbage}, {"truncated bytecode", &truncated}};
            for (const auto& item : cases) {
                Com<ID3D11VertexShader> vs;
                Com<ID3D11PixelShader> ps;
                const HRESULT vs_result = device.device->CreateVertexShader(item.bytes->data(), item.bytes->size(), nullptr, vs.put());
                const HRESULT ps_result = device.device->CreatePixelShader(item.bytes->data(), item.bytes->size(), nullptr, ps.put());
                if (vs_result != E_INVALIDARG) {
                    problems.add(std::string("D3D11 CreateVertexShader with ") + item.name + " returned " + D3d11Tools::hr(vs_result) + ", expected E_INVALIDARG");
                }
                if (ps_result != E_INVALIDARG) {
                    problems.add(std::string("D3D11 CreatePixelShader with ") + item.name + " returned " + D3d11Tools::hr(ps_result) + ", expected E_INVALIDARG");
                }
            }
            Com<ID3D11PixelShader> wrong_stage;
            const HRESULT stage_result = device.device->CreatePixelShader(good_vs.data(), good_vs.size(), nullptr, wrong_stage.put());
            if (stage_result != E_INVALIDARG) {
                problems.add("D3D11 CreatePixelShader with vertex shader bytecode returned " + D3d11Tools::hr(stage_result) + ", expected E_INVALIDARG");
            }
            Com<ID3D11VertexShader> empty;
            const HRESULT empty_result = device.device->CreateVertexShader(good_vs.data(), 0, nullptr, empty.put());
            if (empty_result != E_INVALIDARG) {
                problems.add("D3D11 CreateVertexShader with a size of zero returned " + D3d11Tools::hr(empty_result) + ", expected E_INVALIDARG");
            }
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
            Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device, valid_ps, error);
            std::vector<std::uint8_t> bytes;
            if (!target || !view || !pixel || !D3d11Tools::draw_fullscreen(device, pixel.get(), view.get(), 8, 8, error) ||
                !D3d11Tools::read_pixels(device, target.get(), bytes, error)) {
                problems.add("the D3D11 device does not render after the invalid shaders: " + error);
            } else if (bytes[0] != 51 || bytes[1] != 102 || bytes[2] != 153 || bytes[3] != 255) {
                problems.add("the D3D11 device renders a wrong colour after the invalid shaders");
            }
            D3d11Tools::reset_state(device);
        }
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d9")) {
        exercised = true;
        D3d9Rig rig;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig.open(false, error) || !rig.create(settings, error)) {
            problems.add("D3D9: " + error);
        } else {
            const DWORD bad_vertex[] = {0xDEADBEEFu, 0x12345678u, 0x0000FFFFu};
            const DWORD bad_pixel[] = {0xDEADBEEFu, 0x12345678u, 0x0000FFFFu};
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps;
            if (SUCCEEDED(rig.dev()->CreateVertexShader(bad_vertex, vs.put()))) {
                problems.add("D3D9 CreateVertexShader accepted bytecode without a version token");
            }
            if (SUCCEEDED(rig.dev()->CreatePixelShader(bad_pixel, ps.put()))) {
                problems.add("D3D9 CreatePixelShader accepted bytecode without a version token");
            }
            std::vector<std::uint8_t> code;
            if (D3d9Tools::compile("float4 main(float4 c : COLOR0) : COLOR { return c +; }\n", "ps_2_0", code, error) || error.empty()) {
                problems.add("D3DCompile for ps_2_0 accepted invalid HLSL or gave no diagnostic");
            }
            Com<IDirect3DSurface9> target = D3d9Tools::target(rig.dev(), 8, 8);
            if (!target) {
                problems.add("the D3D9 device cannot create a render target after the invalid shaders");
            }
        }
    } else {
        skips.push_back("D3D9 excluded");
    }
    if (Kit::enabled("d3d12")) {
        exercised = true;
        D3d12Rig rig;
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("D3D12: " + error);
        } else {
            D3d12Root root;
            ID3D12RootSignature* signature = root.build(rig, error);
            D3d12Tools::Bytes vs;
            D3d12Tools::Bytes ps;
            if (signature == nullptr || !D3d12Tools::compile(D3d11Tools::kFullscreenVs, "main", "vs_5_0", vs, error) || !D3d12Tools::compile(valid_ps, "main", "ps_5_0", ps, error)) {
                problems.add("D3D12 reference shaders: " + error);
            } else {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(signature, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
                desc.PS = {garbage.data(), garbage.size()};
                Com<ID3D12PipelineState> state;
                const HRESULT result = rig.device()->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
                if (result != E_INVALIDARG) {
                    problems.add("D3D12 CreateGraphicsPipelineState with garbage pixel shader bytecode returned " + D3d12Tools::hr(result) + ", expected E_INVALIDARG");
                }
                desc.PS = {truncated.data(), truncated.size()};
                state.reset();
                const HRESULT truncated_result = rig.device()->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
                if (truncated_result != E_INVALIDARG) {
                    problems.add("D3D12 CreateGraphicsPipelineState with truncated pixel shader bytecode returned " + D3d12Tools::hr(truncated_result) + ", expected E_INVALIDARG");
                }
                desc.PS = D3d12Tools::bytecode(ps);
                ID3D12PipelineState* good = D3d12Tools::graphics_pso(rig, desc, error);
                if (good == nullptr) {
                    problems.add("D3D12 cannot create a valid pipeline after the invalid shaders: " + error);
                }
                if (rig.removed_reason() != S_OK) {
                    problems.add("the D3D12 device was removed after the invalid shaders: " + D3d12Tools::hr(rig.removed_reason()));
                }
            }
        }
    } else {
        skips.push_back("D3D12 excluded");
    }
    static_cast<void>(context);
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::shader
