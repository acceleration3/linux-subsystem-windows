#include "sanity/d3d9/render_checks.hpp"

#include "sanity/d3d9/fixed_scenes.hpp"
#include "sanity/d3d9/kit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::d3d9 {

namespace {

using Rgba = std::array<float, 4>;

DWORD bits(float value) {
    DWORD out = 0;
    std::memcpy(&out, &value, 4);
    return out;
}

std::uint8_t byte_of(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

SceneImage image_from(UINT width, UINT height, const std::function<Rgba(UINT, UINT)>& pixel_at) {
    SceneImage image;
    image.width = width;
    image.height = height;
    image.data.resize(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const Rgba c = pixel_at(x, y);
            std::uint8_t* out = image.data.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            out[0] = byte_of(c[2]);
            out[1] = byte_of(c[1]);
            out[2] = byte_of(c[0]);
            out[3] = byte_of(c[3]);
        }
    }
    return image;
}

struct TargetScope {
    TargetScope(IDirect3DDevice9* d, IDirect3DSurface9* target, IDirect3DSurface9* depth) : device(d) {
        device->GetRenderTarget(0, saved_target.put());
        device->GetDepthStencilSurface(saved_depth.put());
        device->SetRenderTarget(0, target);
        device->SetDepthStencilSurface(depth);
    }

    ~TargetScope() {
        device->SetRenderTarget(0, saved_target.get());
        device->SetDepthStencilSurface(saved_depth.get());
    }

    TargetScope(const TargetScope&) = delete;
    TargetScope& operator=(const TargetScope&) = delete;

    IDirect3DDevice9* device;
    Com<IDirect3DSurface9> saved_target;
    Com<IDirect3DSurface9> saved_depth;
};

struct ShaderCase {
    std::string name;
    const char* vs_profile;
    const char* ps_profile;
    std::string vs9;
    std::string ps9;
    std::string vs11;
    std::string ps11;
    std::function<Rgba(UINT, UINT)> expected;
    float clear_level = 0.0f;
};

const char* const kFullscreenVs9 = "float4 main(float3 p : POSITION) : POSITION { return float4(p.x - 1.0 / 128.0, p.y + 1.0 / 128.0, p.z, 1); }\n";
const char* const kFullscreenVs11 = "float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n";

ShaderCase shade_case(const std::string& name, const std::string& body, std::function<Rgba(UINT, UINT)> expected, float clear_level = 0.0f) {
    ShaderCase c;
    c.name = name;
    c.vs_profile = "vs_3_0";
    c.ps_profile = "ps_3_0";
    c.vs9 = kFullscreenVs9;
    c.vs11 = kFullscreenVs11;
    c.ps9 = body + "float4 main(float2 vp : VPOS) : COLOR { return shade(floor(vp)); }\n";
    c.ps11 = body + "float4 main(float4 sv : SV_Position) : SV_Target { return shade(floor(sv.xy)); }\n";
    c.expected = std::move(expected);
    c.clear_level = clear_level;
    return c;
}

struct BlendState {
    Rgba s;
    Rgba d;
    Rgba bf;
};

float blend_factor(D3DBLEND f, int channel, const BlendState& b, bool alpha) {
    const float sc = alpha ? b.s[3] : b.s[static_cast<std::size_t>(channel)];
    const float dc = alpha ? b.d[3] : b.d[static_cast<std::size_t>(channel)];
    const float bc = alpha ? b.bf[3] : b.bf[static_cast<std::size_t>(channel)];
    switch (f) {
    case D3DBLEND_ZERO: return 0;
    case D3DBLEND_ONE: return 1;
    case D3DBLEND_SRCCOLOR: return sc;
    case D3DBLEND_INVSRCCOLOR: return 1 - sc;
    case D3DBLEND_SRCALPHA: return b.s[3];
    case D3DBLEND_INVSRCALPHA: return 1 - b.s[3];
    case D3DBLEND_DESTALPHA: return b.d[3];
    case D3DBLEND_INVDESTALPHA: return 1 - b.d[3];
    case D3DBLEND_DESTCOLOR: return dc;
    case D3DBLEND_INVDESTCOLOR: return 1 - dc;
    case D3DBLEND_SRCALPHASAT: return alpha ? 1.0f : std::min(b.s[3], 1 - b.d[3]);
    case D3DBLEND_BLENDFACTOR: return bc;
    case D3DBLEND_INVBLENDFACTOR: return 1 - bc;
    default: return 0;
    }
}

DWORD blend_cap(D3DBLEND f) {
    switch (f) {
    case D3DBLEND_ZERO: return D3DPBLENDCAPS_ZERO;
    case D3DBLEND_ONE: return D3DPBLENDCAPS_ONE;
    case D3DBLEND_SRCCOLOR: return D3DPBLENDCAPS_SRCCOLOR;
    case D3DBLEND_INVSRCCOLOR: return D3DPBLENDCAPS_INVSRCCOLOR;
    case D3DBLEND_SRCALPHA: return D3DPBLENDCAPS_SRCALPHA;
    case D3DBLEND_INVSRCALPHA: return D3DPBLENDCAPS_INVSRCALPHA;
    case D3DBLEND_DESTALPHA: return D3DPBLENDCAPS_DESTALPHA;
    case D3DBLEND_INVDESTALPHA: return D3DPBLENDCAPS_INVDESTALPHA;
    case D3DBLEND_DESTCOLOR: return D3DPBLENDCAPS_DESTCOLOR;
    case D3DBLEND_INVDESTCOLOR: return D3DPBLENDCAPS_INVDESTCOLOR;
    case D3DBLEND_SRCALPHASAT: return D3DPBLENDCAPS_SRCALPHASAT;
    case D3DBLEND_BLENDFACTOR: return D3DPBLENDCAPS_BLENDFACTOR;
    case D3DBLEND_INVBLENDFACTOR: return D3DPBLENDCAPS_BLENDFACTOR;
    default: return 0;
    }
}

float srgb_encode(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

}  // namespace

Outcome RenderChecks::fixed_function(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    D3d9Twin twin;
    if (!twin.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int rendered = 0;
    {
        Com<IDirect3DSurface9> target = D3d9Tools::target(device, FixedScenes::kSize, FixedScenes::kSize);
        if (!target) {
            return Outcome::fail("the render target cannot be created");
        }
        TargetScope scope(device, target.get(), nullptr);
        std::vector<FixedScene> scenes = FixedScenes::build(device, caps);
        for (FixedScene& scene : scenes) {
            if (!scene.unsupported.empty()) {
                context.note(scene.name + ": skipped, " + scene.unsupported);
                continue;
            }
            D3d9Tools::prepare(device, false);
            D3d9Tools::clear(device, 0xFF000000u);
            if (!scene.draw(device)) {
                problems.add(scene.name + ": the draw failed");
                continue;
            }
            SceneImage image;
            if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
                problems.add(scene.name + ": " + error);
                continue;
            }
            twin.compare(context, problems, "fixed function " + scene.name, image, scene.twin, scene.tolerance);
            ++rendered;
        }
        device->SetTexture(0, nullptr);
        device->SetTexture(1, nullptr);
    }
    context.measure("scenes_rendered", rendered);
    if (!Kit::has_depth(rig, D3DFMT_D24S8)) {
        context.note("stencil operations: skipped, no D24S8 depth format");
    } else {
        constexpr UINT kStencilSize = 64;
        Com<IDirect3DSurface9> target = D3d9Tools::target(device, kStencilSize, kStencilSize);
        Com<IDirect3DSurface9> depth;
        device->CreateDepthStencilSurface(kStencilSize, kStencilSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, depth.put(), nullptr);
        if (!target || !depth) {
            problems.add("the stencil target cannot be created");
        } else {
            TargetScope scope(device, target.get(), depth.get());
            D3d9Tools::prepare(device, false);
            device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_STENCIL | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
            std::vector<int> model(kStencilSize * kStencilSize, 0);
            struct Pass {
                float l, t, r, b;
                D3DCMPFUNC func;
                DWORD ref;
                D3DSTENCILOP op;
            };
            const Pass passes[] = {{0, 0, 40, 40, D3DCMP_ALWAYS, 0, D3DSTENCILOP_INCRSAT},
                                   {20, 20, 60, 60, D3DCMP_ALWAYS, 0, D3DSTENCILOP_INCR},
                                   {10, 10, 50, 50, D3DCMP_EQUAL, 1, D3DSTENCILOP_REPLACE},
                                   {0, 30, 64, 64, D3DCMP_EQUAL, 2, D3DSTENCILOP_ZERO},
                                   {5, 5, 25, 25, D3DCMP_NOTEQUAL, 0, D3DSTENCILOP_INVERT},
                                   {30, 0, 64, 20, D3DCMP_ALWAYS, 0, D3DSTENCILOP_DECRSAT}};
            device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
            device->SetRenderState(D3DRS_STENCILMASK, 0xFF);
            device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
            for (const Pass& pass : passes) {
                device->SetRenderState(D3DRS_STENCILFUNC, pass.func);
                device->SetRenderState(D3DRS_STENCILREF, pass.ref);
                device->SetRenderState(D3DRS_STENCILPASS, pass.op);
                if (!D3d9Tools::draw_quad(device, pass.l, pass.t, pass.r, pass.b, 0xFFFFFFFFu)) {
                    problems.add("a stencil pass failed to draw");
                }
                for (UINT y = static_cast<UINT>(pass.t); y < static_cast<UINT>(pass.b); ++y) {
                    for (UINT x = static_cast<UINT>(pass.l); x < static_cast<UINT>(pass.r); ++x) {
                        int& value = model[y * kStencilSize + x];
                        const int ref = static_cast<int>(pass.ref);
                        bool ok = true;
                        switch (pass.func) {
                        case D3DCMP_ALWAYS: ok = true; break;
                        case D3DCMP_EQUAL: ok = ref == value; break;
                        case D3DCMP_NOTEQUAL: ok = ref != value; break;
                        default: break;
                        }
                        if (!ok) {
                            continue;
                        }
                        switch (pass.op) {
                        case D3DSTENCILOP_INCRSAT: value = std::min(255, value + 1); break;
                        case D3DSTENCILOP_INCR: value = (value + 1) & 255; break;
                        case D3DSTENCILOP_DECRSAT: value = std::max(0, value - 1); break;
                        case D3DSTENCILOP_REPLACE: value = ref; break;
                        case D3DSTENCILOP_ZERO: value = 0; break;
                        case D3DSTENCILOP_INVERT: value = (~value) & 255; break;
                        default: break;
                        }
                    }
                }
            }
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
            device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
            std::vector<int> present;
            for (const int v : model) {
                if (std::find(present.begin(), present.end(), v) == present.end()) {
                    present.push_back(v);
                }
            }
            std::sort(present.begin(), present.end());
            auto colour_of = [](int v) { return Kit::rgb(40 + (v * 53) % 200, 30 + (v * 97) % 220, 20 + (v * 29) % 230); };
            for (const int v : present) {
                device->SetRenderState(D3DRS_STENCILREF, static_cast<DWORD>(v));
                if (!D3d9Tools::draw_quad(device, 0, 0, kStencilSize, kStencilSize, colour_of(v))) {
                    problems.add("a stencil read-out pass failed to draw");
                }
            }
            SceneImage image;
            if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
                problems.add(error);
            } else {
                int wrong = 0;
                std::string first;
                for (UINT y = 0; y < kStencilSize; ++y) {
                    for (UINT x = 0; x < kStencilSize; ++x) {
                        const std::uint32_t expected = colour_of(model[y * kStencilSize + x]);
                        if (D3d9Tools::pixel(image, x, y) != expected && wrong++ == 0) {
                            first = "(" + std::to_string(x) + "," + std::to_string(y) + ") shows " + D3d9Tools::color_text(D3d9Tools::pixel(image, x, y)) + ", expected the colour of stencil value " + std::to_string(model[y * kStencilSize + x]);
                        }
                    }
                }
                if (wrong != 0) {
                    problems.add("the stencil buffer differs from the reference model in " + std::to_string(wrong) + " pixels, first " + first);
                }
            }
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            context.measure("stencil_values", present.size());
        }
    }
    Kit::check_usable(problems, "after the scenes", device);
    return problems.outcome();
}

Outcome RenderChecks::shaders(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    D3d9Twin twin;
    if (!twin.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::vector<ShaderCase> cases;
    {
        ShaderCase c;
        c.name = "vs_2_0 and ps_2_0 gradient";
        c.vs_profile = "vs_2_0";
        c.ps_profile = "ps_2_0";
        c.vs9 = "struct O { float4 p : POSITION; float4 c : COLOR0; };\n"
                "O main(float3 p : POSITION) { O o; o.p = float4(p.x - 1.0 / 128.0, p.y + 1.0 / 128.0, p.z, 1); o.c = float4(p.x * 0.5 + 0.5, p.y * 0.5 + 0.5, 0.25, 1); return o; }\n";
        c.ps9 = "float4 main(float4 c : COLOR0) : COLOR { return c; }\n";
        c.vs11 = "struct O { float4 p : SV_Position; float4 c : COLOR0; };\n"
                 "O main(float3 p : POSITION) { O o; o.p = float4(p, 1); o.c = float4(p.x * 0.5 + 0.5, p.y * 0.5 + 0.5, 0.25, 1); return o; }\n";
        c.ps11 = "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target { return c; }\n";
        c.expected = [](UINT x, UINT y) { return Rgba{(static_cast<float>(x) + 0.5f) / 128.0f, 1.0f - (static_cast<float>(y) + 0.5f) / 128.0f, 0.25f, 1.0f}; };
        cases.push_back(std::move(c));
    }
    cases.push_back(shade_case("ps_3_0 loop",
                               "float4 shade(float2 p) { float s = 0; [loop] for (int i = 0; i < 8; i++) { s += p.x / 1024.0; } return float4(s, p.y / 128.0, 0, 1); }\n",
                               [](UINT x, UINT y) { return Rgba{static_cast<float>(x) / 128.0f, static_cast<float>(y) / 128.0f, 0, 1}; }));
    cases.push_back(shade_case("ps_3_0 dynamic branch",
                               "float4 shade(float2 p) { float t = floor(p.x / 8.0) + floor(p.y / 8.0); float4 c; [branch] if (fmod(t, 2.0) >= 1.0) c = float4(1, 0, 0, 1); else c = float4(0, 0, 1, 1); return c; }\n",
                               [](UINT x, UINT y) { return ((x / 8 + y / 8) % 2) == 1 ? Rgba{1, 0, 0, 1} : Rgba{0, 0, 1, 1}; }));
    cases.push_back(shade_case("ps_3_0 texkill",
                               "float4 shade(float2 p) { clip(frac((p.x + 0.5) / 8.0) - 0.5); return float4(0, 1, 0, 1); }\n",
                               [](UINT x, UINT) { return (x % 8) >= 4 ? Rgba{0, 1, 0, 1} : Rgba{0.2f, 0.2f, 0.2f, 1}; }, 0.2f));
    cases.push_back(shade_case("ps_3_0 derivatives",
                               "float4 shade(float2 p) { return float4(ddx(p.x) * 0.5, ddy(p.y) * 0.25, 0.1, 1); }\n",
                               [](UINT, UINT) { return Rgba{0.5f, 0.25f, 0.1f, 1}; }));
    int rendered = 0;
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 128, 128);
    if (!target) {
        return Outcome::fail("the render target cannot be created");
    }
    {
        TargetScope scope(device, target.get(), nullptr);
        const float quad[12] = {-1, 1, 0, 1, 1, 0, -1, -1, 0, 1, -1, 0};
        for (const ShaderCase& c : cases) {
            if ((std::string(c.vs_profile) == "vs_3_0" && caps.VertexShaderVersion < D3DVS_VERSION(3, 0)) || (std::string(c.ps_profile) == "ps_3_0" && caps.PixelShaderVersion < D3DPS_VERSION(3, 0))) {
                context.note(c.name + ": skipped, the shader model is not in the caps");
                continue;
            }
            Com<IDirect3DVertexShader9> vs = D3d9Tools::vs(device, c.vs9, c.vs_profile, error);
            Com<IDirect3DPixelShader9> ps = D3d9Tools::ps(device, c.ps9, c.ps_profile, error);
            if (!vs || !ps) {
                problems.add(c.name + ": " + error);
                continue;
            }
            D3d9Tools::prepare(device, false);
            const std::uint8_t level = byte_of(c.clear_level);
            D3d9Tools::clear(device, Kit::rgb(level, level, level));
            device->SetVertexShader(vs.get());
            device->SetPixelShader(ps.get());
            device->SetFVF(D3DFVF_XYZ);
            device->BeginScene();
            const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 12);
            device->EndScene();
            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
            if (FAILED(drawn)) {
                problems.add(c.name + ": the draw failed with " + Kit::hr(drawn));
                continue;
            }
            SceneImage image;
            if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
                problems.add(c.name + ": " + error);
                continue;
            }
            std::string detail;
            if (!OracleRig::judge(context, "shader " + c.name + " reference", image_from(128, 128, c.expected), image, Tolerance::bytes(2, 0.002), detail)) {
                problems.add(detail + " (against the CPU reference)");
            }
            TwinScene scene;
            scene.width = 128;
            scene.height = 128;
            scene.vertices.resize(sizeof(quad));
            std::memcpy(scene.vertices.data(), quad, sizeof(quad));
            scene.stride = 12;
            scene.vertex_count = 4;
            scene.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            scene.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}};
            scene.vs = c.vs11;
            scene.ps = c.ps11;
            scene.clear[0] = scene.clear[1] = scene.clear[2] = c.clear_level;
            twin.compare(context, problems, "shader " + c.name, image, scene, Tolerance::bytes(2, 0.002));
            ++rendered;
        }
        if (caps.NumSimultaneousRTs >= 2 && caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) && caps.VertexShaderVersion >= D3DVS_VERSION(3, 0)) {
            Com<IDirect3DSurface9> second = D3d9Tools::target(device, 128, 128);
            const std::string vs9 = kFullscreenVs9;
            const std::string ps9 = "struct O { float4 a : COLOR0; float4 b : COLOR1; };\n"
                                    "O main(float2 vp : VPOS) { O o; float2 p = floor(vp); o.a = float4(p.x / 128.0, 0, 0, 1); o.b = float4(0, p.y / 128.0, 0, 1); return o; }\n";
            Com<IDirect3DVertexShader9> vs = D3d9Tools::vs(device, vs9, "vs_3_0", error);
            Com<IDirect3DPixelShader9> ps = D3d9Tools::ps(device, ps9, "ps_3_0", error);
            if (!vs || !ps || !second) {
                problems.add("MRT: " + error);
            } else {
                device->SetRenderTarget(1, second.get());
                D3d9Tools::prepare(device, false);
                device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
                device->SetVertexShader(vs.get());
                device->SetPixelShader(ps.get());
                device->SetFVF(D3DFVF_XYZ);
                device->BeginScene();
                const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 12);
                device->EndScene();
                device->SetVertexShader(nullptr);
                device->SetPixelShader(nullptr);
                SceneImage first_image;
                SceneImage second_image;
                if (FAILED(drawn)) {
                    problems.add("MRT: the draw failed with " + Kit::hr(drawn));
                } else if (D3d9Tools::read_surface(device, target.get(), first_image, error) && D3d9Tools::read_surface(device, second.get(), second_image, error)) {
                    std::string detail;
                    if (!OracleRig::judge(context, "shader MRT target 0 reference", image_from(128, 128, [](UINT x, UINT) { return Rgba{static_cast<float>(x) / 128.0f, 0, 0, 1}; }), first_image, Tolerance::bytes(2, 0.002), detail)) {
                        problems.add(detail);
                    }
                    if (!OracleRig::judge(context, "shader MRT target 1 reference", image_from(128, 128, [](UINT, UINT y) { return Rgba{0, static_cast<float>(y) / 128.0f, 0, 1}; }), second_image, Tolerance::bytes(2, 0.002), detail)) {
                        problems.add(detail);
                    }
                    TwinScene scene;
                    scene.width = 128;
                    scene.height = 128;
                    scene.targets = 2;
                    scene.vertices.resize(sizeof(quad));
                    std::memcpy(scene.vertices.data(), quad, sizeof(quad));
                    scene.stride = 12;
                    scene.vertex_count = 4;
                    scene.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                    scene.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}};
                    scene.vs = kFullscreenVs11;
                    scene.ps = "struct O { float4 a : SV_Target0; float4 b : SV_Target1; };\n"
                               "O main(float4 sv : SV_Position) { O o; float2 p = floor(sv.xy); o.a = float4(p.x / 128.0, 0, 0, 1); o.b = float4(0, p.y / 128.0, 0, 1); return o; }\n";
                    twin.compare(context, problems, "shader MRT target 0", first_image, scene, Tolerance::bytes(2, 0.002), 0);
                    twin.compare(context, problems, "shader MRT target 1", second_image, scene, Tolerance::bytes(2, 0.002), 1);
                    ++rendered;
                } else {
                    problems.add("MRT: " + error);
                }
                device->SetRenderTarget(1, nullptr);
            }
        } else {
            context.note("MRT: skipped, fewer than two render targets or no shader model 3 in the caps");
        }
        if (caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) && caps.VertexShaderVersion >= D3DVS_VERSION(3, 0)) {
            const float faces[36] = {-1, -1, 0, -1, 1, 0, 0, 1, 0, -1, -1, 0, 0, 1, 0, 0, -1, 0, 0, -1, 0, 1, -1, 0, 1, 1, 0, 0, -1, 0, 1, 1, 0, 0, 1, 0};
            Com<IDirect3DVertexShader9> vs = D3d9Tools::vs(device, kFullscreenVs9, "vs_3_0", error);
            Com<IDirect3DPixelShader9> ps = D3d9Tools::ps(device, "float4 main(float f : VFACE) : COLOR { return f > 0 ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1); }\n", "ps_3_0", error);
            if (!vs || !ps) {
                problems.add("VFACE: " + error);
            } else {
                D3d9Tools::prepare(device, false);
                D3d9Tools::clear(device, 0xFF000000u);
                device->SetVertexShader(vs.get());
                device->SetPixelShader(ps.get());
                device->SetFVF(D3DFVF_XYZ);
                device->BeginScene();
                const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 4, faces, 12);
                device->EndScene();
                device->SetVertexShader(nullptr);
                device->SetPixelShader(nullptr);
                SceneImage image;
                if (FAILED(drawn)) {
                    problems.add("VFACE: the draw failed with " + Kit::hr(drawn));
                } else if (D3d9Tools::read_surface(device, target.get(), image, error)) {
                    std::string detail;
                    if (!OracleRig::judge(context, "shader VFACE reference", image_from(128, 128, [](UINT x, UINT) { return x < 64 ? Rgba{0, 1, 0, 1} : Rgba{1, 0, 0, 1}; }), image, Tolerance::bytes(2), detail)) {
                        problems.add(detail + " (clockwise triangles must be front facing)");
                    }
                    TwinScene scene;
                    scene.width = 128;
                    scene.height = 128;
                    scene.vertices.resize(sizeof(faces));
                    std::memcpy(scene.vertices.data(), faces, sizeof(faces));
                    scene.stride = 12;
                    scene.vertex_count = 12;
                    scene.elements = {{"POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0}};
                    scene.vs = kFullscreenVs11;
                    scene.ps = "float4 main(float4 p : SV_Position, bool front : SV_IsFrontFace) : SV_Target { return front ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1); }\n";
                    twin.compare(context, problems, "shader VFACE", image, scene, Tolerance::bytes(2));
                    ++rendered;
                } else {
                    problems.add("VFACE: " + error);
                }
            }
        }
    }
    context.measure("shader_cases_rendered", rendered);
    Kit::check_usable(problems, "after the shaders", device);
    return problems.outcome();
}

Outcome RenderChecks::states_and_blend(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    Problems problems;
    {
        Com<IDirect3DSurface9> target = D3d9Tools::target(device, 128, 128);
        TargetScope scope(device, target.get(), nullptr);
        BlendState state{{204 / 255.0f, 77 / 255.0f, 153 / 255.0f, 102 / 255.0f}, {64 / 255.0f, 128 / 255.0f, 191 / 255.0f, 128 / 255.0f}, {0x33 / 255.0f, 0x66 / 255.0f, 0xCC / 255.0f, 0x99 / 255.0f}};
        const D3DBLEND all[] = {D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLEND_SRCCOLOR, D3DBLEND_INVSRCCOLOR, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLEND_DESTALPHA,
                                D3DBLEND_INVDESTALPHA, D3DBLEND_DESTCOLOR, D3DBLEND_INVDESTCOLOR, D3DBLEND_SRCALPHASAT, D3DBLEND_BLENDFACTOR, D3DBLEND_INVBLENDFACTOR};
        struct Tile {
            D3DBLENDOP op;
            D3DBLEND source;
            D3DBLEND destination;
        };
        std::vector<Tile> tiles;
        for (const D3DBLEND s : all) {
            for (const D3DBLEND d : all) {
                if (d != D3DBLEND_SRCALPHASAT) {
                    tiles.push_back({D3DBLENDOP_ADD, s, d});
                }
            }
        }
        const D3DBLEND some_source[] = {D3DBLEND_ONE, D3DBLEND_SRCALPHA, D3DBLEND_DESTCOLOR, D3DBLEND_INVDESTALPHA, D3DBLEND_BLENDFACTOR};
        const D3DBLEND some_destination[] = {D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLEND_SRCCOLOR, D3DBLEND_INVSRCALPHA, D3DBLEND_DESTALPHA};
        for (const D3DBLENDOP op : {D3DBLENDOP_SUBTRACT, D3DBLENDOP_REVSUBTRACT, D3DBLENDOP_MIN, D3DBLENDOP_MAX}) {
            for (const D3DBLEND s : some_source) {
                for (const D3DBLEND d : some_destination) {
                    tiles.push_back({op, s, d});
                }
            }
        }
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, D3DCOLOR_ARGB(128, 64, 128, 191));
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0x99, 0x33, 0x66, 0xCC));
        const bool ops = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) != 0;
        std::vector<bool> drawn(tiles.size(), false);
        for (std::size_t i = 0; i < tiles.size() && i < 256; ++i) {
            const Tile& t = tiles[i];
            if (((caps.SrcBlendCaps & blend_cap(t.source)) == 0) || ((caps.DestBlendCaps & blend_cap(t.destination)) == 0) || (t.op != D3DBLENDOP_ADD && !ops)) {
                continue;
            }
            device->SetRenderState(D3DRS_BLENDOP, t.op);
            device->SetRenderState(D3DRS_SRCBLEND, t.source);
            device->SetRenderState(D3DRS_DESTBLEND, t.destination);
            const float x = static_cast<float>((i % 16) * 8);
            const float y = static_cast<float>((i / 16) * 8);
            if (!D3d9Tools::draw_quad(device, x, y, x + 8, y + 8, D3DCOLOR_ARGB(102, 204, 77, 153))) {
                problems.add("a blend tile failed to draw");
            }
            drawn[i] = true;
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
            return Outcome::fail(error);
        }
        int checked = 0;
        int wrong = 0;
        std::string first;
        for (std::size_t i = 0; i < tiles.size() && i < 256; ++i) {
            if (!drawn[i]) {
                continue;
            }
            const Tile& t = tiles[i];
            const std::uint32_t got = D3d9Tools::pixel(image, static_cast<UINT>(i % 16) * 8 + 4, static_cast<UINT>(i / 16) * 8 + 4);
            ++checked;
            for (int channel = 0; channel < 4; ++channel) {
                const bool alpha = channel == 3;
                const float s = state.s[static_cast<std::size_t>(channel)];
                const float d = state.d[static_cast<std::size_t>(channel)];
                const float sf = blend_factor(t.source, channel, state, alpha);
                const float df = blend_factor(t.destination, channel, state, alpha);
                float v = 0;
                switch (t.op) {
                case D3DBLENDOP_ADD: v = s * sf + d * df; break;
                case D3DBLENDOP_SUBTRACT: v = s * sf - d * df; break;
                case D3DBLENDOP_REVSUBTRACT: v = d * df - s * sf; break;
                case D3DBLENDOP_MIN: v = std::min(s, d); break;
                default: v = std::max(s, d); break;
                }
                const int expected = byte_of(v);
                const int actual = static_cast<int>((got >> (channel == 0 ? 16 : channel == 1 ? 8 : channel == 2 ? 0 : 24)) & 255u);
                if (std::abs(expected - actual) > 2) {
                    if (wrong++ == 0) {
                        first = "op " + std::to_string(t.op) + " source " + std::to_string(t.source) + " destination " + std::to_string(t.destination) + " channel " + std::to_string(channel) + " is " + std::to_string(actual) + ", expected " + std::to_string(expected);
                    }
                    break;
                }
            }
        }
        context.measure("blend_tiles_checked", checked);
        if (wrong != 0) {
            problems.add(std::to_string(wrong) + " of " + std::to_string(checked) + " blend combinations differ from the CPU reference, first " + first);
        }
        if ((caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) != 0) {
            D3d9Tools::clear(device, D3DCOLOR_ARGB(128, 64, 128, 191));
            device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
            device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
            device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
            static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 16, 16, D3DCOLOR_ARGB(102, 204, 77, 153)));
            device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
            device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
            device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_MAX);
            static_cast<void>(D3d9Tools::draw_quad(device, 16, 0, 32, 16, D3DCOLOR_ARGB(102, 204, 77, 153)));
            device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            SceneImage separate;
            if (D3d9Tools::read_surface(device, target.get(), separate, error)) {
                const float a = state.s[3];
                const std::array<std::uint8_t, 4> want = {byte_of(state.s[0] * a + state.d[0] * (1 - a)), byte_of(state.s[1] * a + state.d[1] * (1 - a)), byte_of(state.s[2] * a + state.d[2] * (1 - a)), byte_of(a)};
                const std::array<std::uint8_t, 4> want_max = {want[0], want[1], want[2], byte_of(std::max(state.s[3], state.d[3]))};
                for (int pass = 0; pass < 2; ++pass) {
                    const std::uint32_t got = D3d9Tools::pixel(separate, 8 + static_cast<UINT>(pass) * 16, 8);
                    const std::array<std::uint8_t, 4>& expected = pass == 0 ? want : want_max;
                    const int actual[4] = {static_cast<int>((got >> 16) & 255u), static_cast<int>((got >> 8) & 255u), static_cast<int>(got & 255u), static_cast<int>(got >> 24)};
                    for (int c = 0; c < 4; ++c) {
                        if (std::abs(actual[c] - expected[static_cast<std::size_t>(c)]) > 2) {
                            problems.add(std::string("separate alpha blend ") + (pass == 0 ? "ONE/ZERO" : "MAX") + " channel " + std::to_string(c) + " is " + std::to_string(actual[c]) + ", expected " + std::to_string(expected[static_cast<std::size_t>(c)]));
                            break;
                        }
                    }
                }
            }
        } else {
            context.note("separate alpha blend: skipped, not in the caps");
        }
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        if (SUCCEEDED(rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_SURFACE, D3DFMT_A8R8G8B8))) {
            D3d9Tools::clear(device, 0xFF000000u);
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE);
            static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 16, 16, Kit::rgb(128, 64, 32)));
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            SceneImage encoded;
            if (D3d9Tools::read_surface(device, target.get(), encoded, error)) {
                const std::uint32_t got = D3d9Tools::pixel(encoded, 8, 8);
                const int want[3] = {byte_of(srgb_encode(128 / 255.0f)), byte_of(srgb_encode(64 / 255.0f)), byte_of(srgb_encode(32 / 255.0f))};
                const int have[3] = {static_cast<int>((got >> 16) & 255u), static_cast<int>((got >> 8) & 255u), static_cast<int>(got & 255u)};
                for (int c = 0; c < 3; ++c) {
                    if (std::abs(want[c] - have[c]) > 2) {
                        problems.add("sRGB write stores " + D3d9Tools::color_text(got) + " for the linear colour 0xFF804020");
                        break;
                    }
                }
            }
        } else {
            context.note("sRGB write: skipped, not supported for A8R8G8B8");
        }
    }
    {
        Com<IDirect3DSurface9> target = D3d9Tools::target(device, 64, 64);
        D3DFORMAT depth_format = D3DFMT_D24S8;
        if (!Kit::has_depth(rig, depth_format)) {
            depth_format = D3DFMT_D16;
        }
        Com<IDirect3DSurface9> depth;
        device->CreateDepthStencilSurface(64, 64, depth_format, D3DMULTISAMPLE_NONE, 0, FALSE, depth.put(), nullptr);
        if (!target || !depth) {
            problems.add("the 64x64 state targets cannot be created");
        } else {
            TargetScope scope(device, target.get(), depth.get());
            if ((caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0) {
                D3d9Tools::prepare(device, false);
                device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
                device->SetRenderState(D3DRS_ZENABLE, TRUE);
                device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 64, 64, 0xFFFF0000u, 0.5f));
                device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
                const float biases[3] = {0.0f, 0.01f, -0.01f};
                for (int band = 0; band < 3; ++band) {
                    device->SetRenderState(D3DRS_DEPTHBIAS, bits(biases[band]));
                    static_cast<void>(D3d9Tools::draw_quad(device, 0, static_cast<float>(band) * 20.0f, 64, static_cast<float>(band + 1) * 20.0f, 0xFF00FF00u, 0.5f));
                }
                device->SetRenderState(D3DRS_DEPTHBIAS, 0);
                SceneImage image;
                if (D3d9Tools::read_surface(device, target.get(), image, error)) {
                    const bool zero_passes = D3d9Tools::pixel(image, 32, 10) == 0xFF00FF00u;
                    const bool positive_passes = D3d9Tools::pixel(image, 32, 30) == 0xFF00FF00u;
                    const bool negative_passes = D3d9Tools::pixel(image, 32, 50) == 0xFF00FF00u;
                    if (zero_passes) {
                        problems.add("a coplanar triangle passes the LESS depth test without a bias");
                    }
                    if (positive_passes == negative_passes) {
                        problems.add(std::string("depth biases of +0.01 and -0.01 do not move coplanar geometry in opposite directions: ") + (positive_passes ? "both pass" : "neither passes"));
                    }
                    context.measure("depth_bias_toward_viewer", negative_passes ? "negative" : "positive");
                }
            } else {
                context.note("depth bias: skipped, not in the caps");
            }
            D3d9Tools::prepare(device, false);
            D3d9Tools::clear(device, 0xFF000000u);
            if ((caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0) {
                const RECT scissor = {10, 20, 50, 44};
                Kit::expect_success(problems, "SetScissorRect", device->SetScissorRect(&scissor));
                device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
                static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 64, 64, 0xFFFFFF00u));
                device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
                SceneImage image;
                std::string where;
                if (D3d9Tools::read_surface(device, target.get(), image, error)) {
                    int wrong = 0;
                    for (UINT y = 0; y < 64; ++y) {
                        for (UINT x = 0; x < 64; ++x) {
                            const bool inside = x >= 10 && x < 50 && y >= 20 && y < 44;
                            if (D3d9Tools::pixel(image, x, y) != (inside ? 0xFFFFFF00u : 0xFF000000u)) {
                                ++wrong;
                            }
                        }
                    }
                    if (wrong != 0) {
                        problems.add("the scissor rectangle misses the expected pixels in " + std::to_string(wrong) + " places");
                    }
                }
            } else {
                context.note("scissor: skipped, not in the caps");
            }
            const DWORD write_masks[] = {D3DCOLORWRITEENABLE_RED, D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_ALPHA, 0, D3DCOLORWRITEENABLE_BLUE};
            for (const DWORD mask : write_masks) {
                device->Clear(0, nullptr, D3DCLEAR_TARGET, 0x280A141Eu, 1.0f, 0);
                device->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
                static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 64, 64, 0xFFFFFFFFu));
                device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
                SceneImage image;
                if (D3d9Tools::read_surface(device, target.get(), image, error)) {
                    std::uint32_t expected = 0x280A141Eu;
                    if ((mask & D3DCOLORWRITEENABLE_RED) != 0) {
                        expected = (expected & 0xFF00FFFFu) | 0x00FF0000u;
                    }
                    if ((mask & D3DCOLORWRITEENABLE_GREEN) != 0) {
                        expected = (expected & 0xFFFF00FFu) | 0x0000FF00u;
                    }
                    if ((mask & D3DCOLORWRITEENABLE_BLUE) != 0) {
                        expected = (expected & 0xFFFFFF00u) | 0x000000FFu;
                    }
                    if ((mask & D3DCOLORWRITEENABLE_ALPHA) != 0) {
                        expected = (expected & 0x00FFFFFFu) | 0xFF000000u;
                    }
                    if (D3d9Tools::pixel(image, 32, 32) != expected) {
                        problems.add("colour write mask 0x" + Text::hex(mask, 1) + " leaves " + D3d9Tools::color_text(D3d9Tools::pixel(image, 32, 32)) + ", expected " + D3d9Tools::color_text(expected));
                    }
                }
            }
        }
    }
    {
        DWORD levels = 0;
        if (SUCCEEDED(rig.d3d->CheckDeviceMultiSampleType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &levels))) {
            Com<IDirect3DSurface9> ms = D3d9Tools::target(device, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES);
            Com<IDirect3DSurface9> resolved = D3d9Tools::target(device, 64, 64);
            if (ms && resolved) {
                TargetScope scope(device, ms.get(), nullptr);
                const struct {
                    DWORD mask;
                    int expected;
                } masks[] = {{0xFFFFFFFFu, 255}, {0x0, 0}, {0x3, 128}, {0x1, 64}};
                for (const auto& m : masks) {
                    D3d9Tools::prepare(device, false);
                    D3d9Tools::clear(device, 0xFF000000u);
                    device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
                    device->SetRenderState(D3DRS_MULTISAMPLEMASK, m.mask);
                    static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 64, 64, 0xFFFFFFFFu));
                    device->SetRenderState(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFFu);
                    if (SUCCEEDED(device->StretchRect(ms.get(), nullptr, resolved.get(), nullptr, D3DTEXF_NONE))) {
                        SceneImage image;
                        if (D3d9Tools::read_surface(device, resolved.get(), image, error)) {
                            const int actual = static_cast<int>((D3d9Tools::pixel(image, 32, 32) >> 8) & 255u);
                            if (std::abs(actual - m.expected) > 24) {
                                problems.add("multisample mask 0x" + Text::hex(m.mask, 8) + " resolves to " + std::to_string(actual) + ", expected about " + std::to_string(m.expected));
                            }
                        }
                    } else {
                        problems.add("resolving the multisampled target failed");
                    }
                }
            }
        } else {
            context.note("multisample mask: skipped, 4x multisampling is not supported");
        }
    }
    Kit::check_usable(problems, "after the state groups", device);
    return problems.outcome();
}

Outcome RenderChecks::multisample(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3d9Twin twin;
    if (!twin.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int tested = 0;
    for (const int count : {2, 4, 8}) {
        const auto type = static_cast<D3DMULTISAMPLE_TYPE>(count);
        DWORD levels = 0;
        const HRESULT support = rig.d3d->CheckDeviceMultiSampleType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, type, &levels);
        if (support != D3D_OK) {
            if (support != D3DERR_NOTAVAILABLE) {
                problems.add(std::to_string(count) + "x: CheckDeviceMultiSampleType returned " + Kit::hr(support));
            }
            context.note(std::to_string(count) + "x multisampling is not supported");
            continue;
        }
        HRESULT created = S_OK;
        Com<IDirect3DSurface9> ms = D3d9Tools::target(device, 128, 128, D3DFMT_A8R8G8B8, type, false, &created, 0);
        Com<IDirect3DSurface9> resolved = D3d9Tools::target(device, 128, 128);
        if (!ms || !resolved) {
            problems.add(std::to_string(count) + "x: the reported multisample type cannot be created: " + Kit::hr(created));
            continue;
        }
        {
            HRESULT top_result = S_OK;
            Com<IDirect3DSurface9> top = D3d9Tools::target(device, 128, 128, D3DFMT_A8R8G8B8, type, false, &top_result, levels - 1);
            if (!top) {
                problems.add(std::to_string(count) + "x: quality level " + std::to_string(levels - 1) + " cannot be created: " + Kit::hr(top_result));
            }
        }
        TargetScope scope(device, ms.get(), nullptr);
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, 0xFF000000u);
        device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
        const float points[6] = {10, 10, 118, 30, 30, 118};
        TlVertex triangle[3];
        for (int i = 0; i < 3; ++i) {
            triangle[i] = {points[i * 2] - 0.5f, points[i * 2 + 1] - 0.5f, 0.5f, 1.0f, 0xFFFFFFFFu, 0, 0};
        }
        device->SetFVF(D3d9Tools::kTlFvf);
        device->BeginScene();
        const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(TlVertex));
        device->EndScene();
        if (FAILED(drawn)) {
            problems.add(std::to_string(count) + "x: the draw failed with " + Kit::hr(drawn));
            continue;
        }
        const HRESULT resolve = device->StretchRect(ms.get(), nullptr, resolved.get(), nullptr, D3DTEXF_NONE);
        if (FAILED(resolve)) {
            problems.add(std::to_string(count) + "x: the resolving StretchRect failed with " + Kit::hr(resolve));
            continue;
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, resolved.get(), image, error)) {
            problems.add(error);
            continue;
        }
        if ((D3d9Tools::pixel(image, 40, 40) & 0xFFFFFFu) != 0xFFFFFFu || (D3d9Tools::pixel(image, 110, 110) & 0xFFFFFFu) != 0) {
            problems.add(std::to_string(count) + "x: the interior of the triangle or the empty corner is wrong");
        }
        int partial = 0;
        int off_grid = 0;
        for (UINT y = 0; y < 128; ++y) {
            for (UINT x = 0; x < 128; ++x) {
                const int value = static_cast<int>(D3d9Tools::pixel(image, x, y) & 255u);
                if (value > 3 && value < 252) {
                    ++partial;
                    bool on_grid = false;
                    for (int k = 0; k <= count; ++k) {
                        on_grid = on_grid || std::abs(value - byte_of(static_cast<float>(k) / static_cast<float>(count))) <= 3;
                    }
                    off_grid += on_grid ? 0 : 1;
                }
            }
        }
        context.measure(std::to_string(count) + "x.edge_pixels", partial);
        context.measure(std::to_string(count) + "x.quality_levels", levels);
        if (partial == 0) {
            problems.add(std::to_string(count) + "x: the resolved edge has no coverage-weighted pixels");
        }
        if (off_grid != 0) {
            problems.add(std::to_string(count) + "x: " + std::to_string(off_grid) + " edge pixels are not a multiple of 1/" + std::to_string(count) + " coverage");
        }
        TwinScene scene;
        scene.width = 128;
        scene.height = 128;
        scene.samples = static_cast<UINT>(count);
        const float ndc[6] = {10 / 64.0f - 1, 1 - 10 / 64.0f, 118 / 64.0f - 1, 1 - 30 / 64.0f, 30 / 64.0f - 1, 1 - 118 / 64.0f};
        scene.vertices.resize(sizeof(ndc));
        std::memcpy(scene.vertices.data(), ndc, sizeof(ndc));
        scene.stride = 8;
        scene.vertex_count = 3;
        scene.elements = {{"POSITION", DXGI_FORMAT_R32G32_FLOAT, 0}};
        scene.vs = "float4 main(float2 p : POSITION) : SV_Position { return float4(p, 0, 1); }\n";
        scene.ps = "float4 main(float4 p : SV_Position) : SV_Target { return float4(1, 1, 1, 1); }\n";
        twin.compare(context, problems, std::to_string(count) + "x multisampled triangle", image, scene, Tolerance::bytes(255 / count + 4));
        ++tested;
    }
    if (tested == 0) {
        return Context::skip("no multisample type is supported for A8R8G8B8 render targets");
    }
    Kit::check_usable(problems, "after the multisampled draws", device);
    return problems.outcome();
}

Outcome RenderChecks::draw_variants(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    Problems problems;
    struct Quad {
        float l, t, r, b;
        DWORD colour;
    };
    const Quad quads[4] = {{8, 8, 40, 40, 0xFFFF0000u}, {50, 8, 120, 40, 0xFF00FF00u}, {8, 50, 60, 100, 0xFF0000FFu}, {70, 60, 120, 120, 0xFFFFFF00u}};
    std::vector<TlVertex> vertices;
    std::vector<std::uint16_t> indices16;
    std::vector<std::uint32_t> indices32;
    std::vector<TlVertex> expanded;
    for (const Quad& q : quads) {
        TlVertex strip[4];
        D3d9Tools::tl_quad(strip, q.l, q.t, q.r, q.b, q.colour);
        const auto base = static_cast<std::uint16_t>(vertices.size());
        for (const TlVertex& v : strip) {
            vertices.push_back(v);
        }
        for (const int i : {0, 1, 2, 2, 1, 3}) {
            indices16.push_back(static_cast<std::uint16_t>(base + i));
            indices32.push_back(base + static_cast<std::uint32_t>(i));
            expanded.push_back(strip[i]);
        }
    }
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 128, 128);
    if (!target) {
        return Outcome::fail("the render target cannot be created");
    }
    TargetScope scope(device, target.get(), nullptr);
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DVertexBuffer9> offset_vb;
    Com<IDirect3DVertexBuffer9> expanded_vb;
    Com<IDirect3DIndexBuffer9> ib16;
    Com<IDirect3DIndexBuffer9> ib32;
    auto fill = [&](IDirect3DVertexBuffer9* buffer, const void* data, std::size_t size, std::size_t offset) {
        void* memory = nullptr;
        if (FAILED(buffer->Lock(static_cast<UINT>(offset), static_cast<UINT>(size), &memory, 0))) {
            return false;
        }
        std::memcpy(memory, data, size);
        buffer->Unlock();
        return true;
    };
    const UINT vertex_bytes = static_cast<UINT>(vertices.size() * sizeof(TlVertex));
    device->CreateVertexBuffer(vertex_bytes, 0, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, vb.put(), nullptr);
    device->CreateVertexBuffer(vertex_bytes + 8 * sizeof(TlVertex), 0, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, offset_vb.put(), nullptr);
    device->CreateVertexBuffer(static_cast<UINT>(expanded.size() * sizeof(TlVertex)), 0, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, expanded_vb.put(), nullptr);
    device->CreateIndexBuffer(static_cast<UINT>(indices16.size() * 2), 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, ib16.put(), nullptr);
    if (!vb || !offset_vb || !expanded_vb || !ib16 || !fill(vb.get(), vertices.data(), vertex_bytes, 0) || !fill(offset_vb.get(), vertices.data(), vertex_bytes, 8 * sizeof(TlVertex)) ||
        !fill(expanded_vb.get(), expanded.data(), expanded.size() * sizeof(TlVertex), 0)) {
        return Outcome::fail("the geometry buffers cannot be created");
    }
    {
        void* memory = nullptr;
        ib16->Lock(0, 0, &memory, 0);
        std::memcpy(memory, indices16.data(), indices16.size() * 2);
        ib16->Unlock();
    }
    const bool wide = caps.MaxVertexIndex > 0xFFFF;
    if (wide) {
        device->CreateIndexBuffer(static_cast<UINT>(indices32.size() * 4), 0, D3DFMT_INDEX32, D3DPOOL_DEFAULT, ib32.put(), nullptr);
        if (ib32) {
            void* memory = nullptr;
            ib32->Lock(0, 0, &memory, 0);
            std::memcpy(memory, indices32.data(), indices32.size() * 4);
            ib32->Unlock();
        }
    }
    auto render = [&](const std::function<HRESULT()>& draw, SceneImage& out) {
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, 0xFF000000u);
        device->SetFVF(D3d9Tools::kTlFvf);
        device->BeginScene();
        const HRESULT result = draw();
        device->EndScene();
        if (FAILED(result)) {
            return result;
        }
        return D3d9Tools::read_surface(device, target.get(), out, error) ? S_OK : E_FAIL;
    };
    SceneImage reference;
    Kit::expect_success(problems, "DrawPrimitiveUP", render([&] { return device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 8, expanded.data(), sizeof(TlVertex)); }, reference));
    {
        std::string where;
        const SceneImage analytic = image_from(128, 128, [&](UINT x, UINT y) {
            for (const Quad& q : quads) {
                if (static_cast<float>(x) >= q.l && static_cast<float>(x) < q.r && static_cast<float>(y) >= q.t && static_cast<float>(y) < q.b) {
                    return Rgba{static_cast<float>((q.colour >> 16) & 255u) / 255.0f, static_cast<float>((q.colour >> 8) & 255u) / 255.0f, static_cast<float>(q.colour & 255u) / 255.0f, 1.0f};
                }
            }
            return Rgba{0, 0, 0, 1};
        });
        if (!D3d9Tools::same(analytic, reference, 0, where)) {
            problems.add("DrawPrimitiveUP does not draw the analytic quads: " + where);
        }
    }
    struct Variant {
        const char* name;
        std::function<HRESULT()> draw;
        bool available;
    };
    const Variant variants[] = {
        {"DrawPrimitive", [&] { device->SetStreamSource(0, expanded_vb.get(), 0, sizeof(TlVertex)); return device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 8); }, true},
        {"DrawIndexedPrimitive 16-bit", [&] {
             device->SetStreamSource(0, vb.get(), 0, sizeof(TlVertex));
             device->SetIndices(ib16.get());
             return device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 16, 0, 8);
         }, true},
        {"DrawIndexedPrimitive with BaseVertexIndex", [&] {
             device->SetStreamSource(0, offset_vb.get(), 0, sizeof(TlVertex));
             device->SetIndices(ib16.get());
             return device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 8, 0, 16, 0, 8);
         }, true},
        {"DrawIndexedPrimitiveUP 16-bit", [&] { return device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 16, 8, indices16.data(), D3DFMT_INDEX16, vertices.data(), sizeof(TlVertex)); }, true},
        {"DrawIndexedPrimitiveUP 32-bit", [&] { return device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 16, 8, indices32.data(), D3DFMT_INDEX32, vertices.data(), sizeof(TlVertex)); }, wide},
        {"DrawIndexedPrimitive 32-bit", [&] {
             device->SetStreamSource(0, vb.get(), 0, sizeof(TlVertex));
             device->SetIndices(ib32.get());
             return device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 16, 0, 8);
         }, wide && ib32},
    };
    int compared = 0;
    for (const Variant& v : variants) {
        if (!v.available) {
            context.note(std::string(v.name) + ": skipped, 32-bit indices are not in the caps");
            continue;
        }
        SceneImage image;
        const HRESULT result = render(v.draw, image);
        if (FAILED(result)) {
            problems.add(std::string(v.name) + " failed with " + Kit::hr(result) + " " + error);
            continue;
        }
        std::string where;
        if (!D3d9Tools::same(reference, image, 0, where)) {
            problems.add(std::string(v.name) + " differs from DrawPrimitiveUP: " + where);
        }
        ++compared;
    }
    device->SetStreamSource(0, nullptr, 0, 0);
    device->SetIndices(nullptr);
    if (caps.VertexShaderVersion >= D3DVS_VERSION(3, 0) && caps.PixelShaderVersion >= D3DPS_VERSION(3, 0)) {
        struct Corner {
            float x, y;
        };
        struct Instance {
            float x, y;
            DWORD colour;
            float w, h;
        };
        const Corner unit[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        std::vector<Instance> instances;
        for (const Quad& q : quads) {
            instances.push_back({q.l, q.t, q.colour, q.r - q.l, q.b - q.t});
        }
        const std::uint16_t unit_indices[6] = {0, 1, 2, 2, 1, 3};
        Com<IDirect3DVertexBuffer9> unit_vb;
        Com<IDirect3DVertexBuffer9> instance_vb;
        Com<IDirect3DIndexBuffer9> unit_ib;
        device->CreateVertexBuffer(sizeof(unit), 0, 0, D3DPOOL_DEFAULT, unit_vb.put(), nullptr);
        device->CreateVertexBuffer(static_cast<UINT>(instances.size() * sizeof(Instance)), 0, 0, D3DPOOL_DEFAULT, instance_vb.put(), nullptr);
        device->CreateIndexBuffer(sizeof(unit_indices), 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, unit_ib.put(), nullptr);
        const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                              {1, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                              {1, 8, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                              {1, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                                              D3DDECL_END()};
        Com<IDirect3DVertexDeclaration9> declaration;
        const std::string vs_source =
            "float4 c : register(c0);\n"
            "struct I { float2 p : POSITION; float2 o : TEXCOORD0; float4 col : COLOR0; float2 s : TEXCOORD1; };\n"
            "struct O { float4 p : POSITION; float4 col : COLOR0; };\n"
            "O main(I i) { O o; float2 px = i.o + i.p * i.s; o.p = float4(px.x * c.x + c.z, px.y * c.y + c.w, 0.5, 1); o.col = i.col; return o; }\n";
        Com<IDirect3DVertexShader9> vs = D3d9Tools::vs(device, vs_source, "vs_3_0", error);
        Com<IDirect3DPixelShader9> ps = D3d9Tools::ps(device, "float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_3_0", error);
        if (!unit_vb || !instance_vb || !unit_ib || !vs || !ps || FAILED(device->CreateVertexDeclaration(elements, declaration.put())) ||
            !fill(unit_vb.get(), unit, sizeof(unit), 0) || !fill(instance_vb.get(), instances.data(), instances.size() * sizeof(Instance), 0)) {
            problems.add("the instancing resources cannot be created: " + error);
        } else {
            void* memory = nullptr;
            unit_ib->Lock(0, 0, &memory, 0);
            std::memcpy(memory, unit_indices, sizeof(unit_indices));
            unit_ib->Unlock();
            const float constants[4] = {2.0f / 128.0f, -2.0f / 128.0f, -1.0f - 1.0f / 128.0f, 1.0f + 1.0f / 128.0f};
            SceneImage image;
            const HRESULT result = render([&] {
                device->SetVertexDeclaration(declaration.get());
                device->SetVertexShader(vs.get());
                device->SetPixelShader(ps.get());
                device->SetVertexShaderConstantF(0, constants, 1);
                device->SetStreamSource(0, unit_vb.get(), 0, sizeof(Corner));
                device->SetStreamSource(1, instance_vb.get(), 0, sizeof(Instance));
                device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 4u);
                device->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
                device->SetIndices(unit_ib.get());
                return device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
            }, image);
            device->SetStreamSourceFreq(0, 1);
            device->SetStreamSourceFreq(1, 1);
            device->SetStreamSource(0, nullptr, 0, 0);
            device->SetStreamSource(1, nullptr, 0, 0);
            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
            device->SetVertexDeclaration(nullptr);
            if (FAILED(result)) {
                problems.add("instancing through SetStreamSourceFreq failed with " + Kit::hr(result) + " " + error);
            } else {
                std::string where;
                if (!D3d9Tools::same(reference, image, 0, where)) {
                    problems.add("instancing differs from DrawPrimitiveUP: " + where);
                }
                ++compared;
            }
        }
    } else {
        context.note("instancing: skipped, vertex shader model 3 is not in the caps");
    }
    context.measure("variants_compared", compared);
    Kit::check_usable(problems, "after the draw variants", device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
