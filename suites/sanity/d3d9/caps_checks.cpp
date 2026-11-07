#include "sanity/d3d9/caps_checks.hpp"

#include "sanity/d3d9/kit.hpp"
#include "support/dyn_library.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d9 {

namespace {

constexpr const char* kPlainVs =
    "float4 main(float4 p : POSITION) : POSITION { return p; }\n";
constexpr const char* kPlainPs =
    "float4 main() : COLOR { return float4(1, 0, 0, 1); }\n";

struct VersionCase {
    const char* profile;
    DWORD version;
};

}  // namespace

Outcome CapsChecks::create_matrix(Context& context) {
    Problems problems;
    int created = 0;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        if (!rig.open(ex, error)) {
            if (ex) {
                context.note("Direct3D 9Ex is unavailable: " + error);
                continue;
            }
            return Outcome::fail(error);
        }
        D3DCAPS9 caps{};
        if (!rig.caps(caps)) {
            return Outcome::fail("GetDeviceCaps failed");
        }
        const bool tnl = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig.make_window(settings, error)) {
            return Outcome::fail(error);
        }
        struct Vp {
            const char* name;
            DWORD flag;
            bool supported;
        };
        const Vp processing[] = {{"HARDWARE", D3DCREATE_HARDWARE_VERTEXPROCESSING, tnl},
                                 {"MIXED", D3DCREATE_MIXED_VERTEXPROCESSING, tnl},
                                 {"SOFTWARE", D3DCREATE_SOFTWARE_VERTEXPROCESSING, true}};
        for (const Vp& vp : processing) {
            for (const DWORD multithreaded : {0ul, static_cast<unsigned long>(D3DCREATE_MULTITHREADED)}) {
                for (const DWORD preserve : {0ul, static_cast<unsigned long>(D3DCREATE_FPU_PRESERVE)}) {
                    const std::string name = std::string("D3D") + Kit::label(ex) + " " + vp.name + (multithreaded != 0 ? "+MT" : "") + (preserve != 0 ? "+FPU" : "");
                    if (!vp.supported) {
                        context.note(name + ": skipped, GetDeviceCaps reports no hardware transform and light");
                        continue;
                    }
                    settings.behavior = vp.flag | multithreaded | preserve;
                    const HRESULT result = rig.try_create(settings);
                    if (FAILED(result)) {
                        problems.add(name + ": CreateDevice failed with " + Kit::hr(result));
                        continue;
                    }
                    ++created;
                    Kit::check_usable(problems, name, rig.dev());
                    rig.close_device();
                }
            }
        }
    }
    context.measure("devices_created", created);
    return problems.outcome();
}

Outcome CapsChecks::caps_vs_behaviour(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3DCAPS9 caps{};
    if (FAILED(device->GetDeviceCaps(&caps))) {
        return Outcome::fail("GetDeviceCaps failed");
    }
    Problems problems;
    const bool pow2 = (caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
    const bool conditional = (caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0;
    {
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> odd = D3d9Tools::texture(device, 100, 100, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
        if (!pow2 || conditional) {
            Kit::expect_success(problems, "a 100x100 texture although the caps allow non power of two sizes", result);
        } else {
            Kit::expect_failure(problems, "a 100x100 texture although the caps demand power of two sizes", result);
        }
    }
    {
        HRESULT result = S_OK;
        Com<IDirect3DCubeTexture9> cube;
        result = device->CreateCubeTexture(64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, cube.put(), nullptr);
        if ((caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0) {
            Kit::expect_success(problems, "CreateCubeTexture with the CUBEMAP cap", result);
        } else {
            Kit::expect_failure(problems, "CreateCubeTexture without the CUBEMAP cap", result);
        }
        Com<IDirect3DVolumeTexture9> volume;
        result = device->CreateVolumeTexture(16, 16, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, volume.put(), nullptr);
        if ((caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0) {
            Kit::expect_success(problems, "CreateVolumeTexture with the VOLUMEMAP cap", result);
        } else {
            Kit::expect_failure(problems, "CreateVolumeTexture without the VOLUMEMAP cap", result);
        }
    }
    {
        const UINT ratio = caps.MaxTextureAspectRatio;
        const UINT wide_h = ratio != 0 ? std::max<UINT>(1, caps.MaxTextureWidth / ratio) : 1;
        const UINT tall_w = ratio != 0 ? std::max<UINT>(1, caps.MaxTextureHeight / ratio) : 1;
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> t = D3d9Tools::texture(device, caps.MaxTextureWidth, wide_h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
        Kit::expect_success(problems, "a texture of MaxTextureWidth " + std::to_string(caps.MaxTextureWidth), result);
        t = D3d9Tools::texture(device, caps.MaxTextureWidth + 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
        Kit::expect_refused(problems, "a texture of MaxTextureWidth + 1", result);
        t = D3d9Tools::texture(device, tall_w, caps.MaxTextureHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
        Kit::expect_success(problems, "a texture of MaxTextureHeight " + std::to_string(caps.MaxTextureHeight), result);
        t = D3d9Tools::texture(device, 1, caps.MaxTextureHeight + 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
        Kit::expect_refused(problems, "a texture of MaxTextureHeight + 1", result);
        context.measure("max_texture", std::to_string(caps.MaxTextureWidth) + "x" + std::to_string(caps.MaxTextureHeight));
    }
    {
        Com<IDirect3DTexture9> tex = D3d9Tools::texture(device, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
        if (!tex) {
            tex = D3d9Tools::texture(device, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        }
        if (caps.MaxSimultaneousTextures < 1) {
            problems.add("MaxSimultaneousTextures is zero");
        }
        for (DWORD stage = 0; stage < 16; ++stage) {
            Kit::expect_success(problems, "SetTexture(" + std::to_string(stage) + ")", device->SetTexture(stage, tex.get()));
            device->SetTexture(stage, nullptr);
        }
        Kit::expect_failure(problems, "SetTexture(16)", device->SetTexture(16, tex.get()));
        context.measure("max_simultaneous_textures", caps.MaxSimultaneousTextures);
    }
    {
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        const UINT count = std::min<UINT>(caps.NumSimultaneousRTs, 4);
        std::vector<Com<IDirect3DSurface9>> targets;
        for (UINT i = 0; i < 4; ++i) {
            targets.push_back(D3d9Tools::target(device, 32, 32));
        }
        if (count < 1) {
            problems.add("NumSimultaneousRTs is zero");
        }
        for (UINT i = 1; i < count; ++i) {
            Kit::expect_success(problems, "SetRenderTarget(" + std::to_string(i) + ")", device->SetRenderTarget(i, targets[i].get()));
        }
        if (caps.NumSimultaneousRTs < 4) {
            Kit::expect_failure(problems, "SetRenderTarget(" + std::to_string(caps.NumSimultaneousRTs) + ") beyond NumSimultaneousRTs", device->SetRenderTarget(caps.NumSimultaneousRTs, targets[0].get()));
        }
        Kit::expect_failure(problems, "SetRenderTarget(4)", device->SetRenderTarget(4, targets[0].get()));
        for (UINT i = 1; i < count; ++i) {
            device->SetRenderTarget(i, nullptr);
        }
        device->SetRenderTarget(0, original.get());
        context.measure("num_simultaneous_rts", caps.NumSimultaneousRTs);
    }
    {
        Com<IDirect3DVertexBuffer9> vb;
        device->CreateVertexBuffer(64, 0, 0, D3DPOOL_DEFAULT, vb.put(), nullptr);
        const UINT streams = std::min<UINT>(caps.MaxStreams, 16);
        for (UINT i = 0; i < streams; ++i) {
            Kit::expect_success(problems, "SetStreamSource(" + std::to_string(i) + ")", device->SetStreamSource(i, vb.get(), 0, 16));
            device->SetStreamSource(i, nullptr, 0, 0);
        }
        Kit::expect_failure(problems, "SetStreamSource(" + std::to_string(streams) + ") beyond MaxStreams", device->SetStreamSource(streams, vb.get(), 0, 16));
        context.measure("max_streams", caps.MaxStreams);
    }
    {
        HRESULT result = S_OK;
        Com<IDirect3DIndexBuffer9> ib;
        result = device->CreateIndexBuffer(64, 0, D3DFMT_INDEX32, D3DPOOL_DEFAULT, ib.put(), nullptr);
        if (caps.MaxVertexIndex > 0xFFFF) {
            Kit::expect_success(problems, "a 32-bit index buffer with MaxVertexIndex " + std::to_string(caps.MaxVertexIndex), result);
        } else {
            Kit::expect_failure(problems, "a 32-bit index buffer with MaxVertexIndex " + std::to_string(caps.MaxVertexIndex), result);
        }
        if (caps.MaxVertexIndex < 0xFFFF) {
            problems.add("MaxVertexIndex " + std::to_string(caps.MaxVertexIndex) + " is below 65535");
        }
        Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 32, 32);
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        device->SetRenderTarget(0, rt.get());
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, 0xFF000000u);
        std::vector<TlVertex> vertices(65536, TlVertex{0, 0, 0.5f, 1.0f, 0xFF000000u, 0, 0});
        D3d9Tools::tl_quad(vertices.data() + 65532, -1, -1, 40, 40, 0xFF00FF00u);
        vertices[65535] = {-0.5f, 70.0f, 0.5f, 1.0f, 0xFF00FF00u, 0, 0};
        vertices[65534] = {70.0f, -0.5f, 0.5f, 1.0f, 0xFF00FF00u, 0, 0};
        vertices[65533] = {-0.5f, -0.5f, 0.5f, 1.0f, 0xFF00FF00u, 0, 0};
        Com<IDirect3DVertexBuffer9> big;
        void* mapped = nullptr;
        result = device->CreateVertexBuffer(static_cast<UINT>(vertices.size() * sizeof(TlVertex)), 0, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, big.put(), nullptr);
        Com<IDirect3DIndexBuffer9> narrow;
        device->CreateIndexBuffer(6, 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, narrow.put(), nullptr);
        if (big && narrow && SUCCEEDED(big->Lock(0, 0, &mapped, 0))) {
            std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(TlVertex));
            big->Unlock();
            void* index_memory = nullptr;
            narrow->Lock(0, 0, &index_memory, 0);
            const std::uint16_t indices[3] = {0xFFFD, 0xFFFE, 0xFFFF};
            std::memcpy(index_memory, indices, 6);
            narrow->Unlock();
            device->SetFVF(D3d9Tools::kTlFvf);
            device->SetStreamSource(0, big.get(), 0, sizeof(TlVertex));
            device->SetIndices(narrow.get());
            device->BeginScene();
            result = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0xFFFD, 3, 0, 1);
            device->EndScene();
            Kit::expect_success(problems, "a 16-bit indexed draw using vertex index 65535", result);
            SceneImage image;
            if (SUCCEEDED(result) && D3d9Tools::read_surface(device, rt.get(), image, error)) {
                if (D3d9Tools::pixel(image, 4, 4) != 0xFF00FF00u) {
                    problems.add("the triangle drawn through index 65535 is missing: pixel (4,4) is " + D3d9Tools::color_text(D3d9Tools::pixel(image, 4, 4)));
                }
            }
            device->SetStreamSource(0, nullptr, 0, 0);
            device->SetIndices(nullptr);
        } else {
            problems.add("a vertex buffer of 65536 vertices cannot be created");
        }
        const UINT triangles = std::min<UINT>(caps.MaxPrimitiveCount, 21845);
        std::vector<TlVertex> many(static_cast<std::size_t>(triangles) * 3, TlVertex{0, 0, 0.5f, 1.0f, 0xFF000000u, 0, 0});
        many[0] = {-0.5f, -0.5f, 0.5f, 1.0f, 0xFF0000FFu, 0, 0};
        many[1] = {70.0f, -0.5f, 0.5f, 1.0f, 0xFF0000FFu, 0, 0};
        many[2] = {-0.5f, 70.0f, 0.5f, 1.0f, 0xFF0000FFu, 0, 0};
        device->SetFVF(D3d9Tools::kTlFvf);
        device->BeginScene();
        result = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, many.data(), sizeof(TlVertex));
        device->EndScene();
        Kit::expect_success(problems, "a draw of " + std::to_string(triangles) + " primitives (MaxPrimitiveCount " + std::to_string(caps.MaxPrimitiveCount) + ")", result);
        SceneImage image;
        if (SUCCEEDED(result) && D3d9Tools::read_surface(device, rt.get(), image, error) && D3d9Tools::pixel(image, 4, 4) != 0xFF0000FFu) {
            problems.add("the many-primitive draw produced " + D3d9Tools::color_text(D3d9Tools::pixel(image, 4, 4)) + " instead of blue");
        }
        device->SetRenderTarget(0, original.get());
    }
    {
        const VersionCase vs_cases[] = {{"vs_2_0", D3DVS_VERSION(2, 0)}, {"vs_3_0", D3DVS_VERSION(3, 0)}};
        const VersionCase ps_cases[] = {{"ps_2_0", D3DPS_VERSION(2, 0)}, {"ps_3_0", D3DPS_VERSION(3, 0)}};
        for (const VersionCase& c : vs_cases) {
            std::vector<std::uint8_t> code;
            if (!D3d9Tools::compile(kPlainVs, c.profile, code, error)) {
                return Outcome::fail(error);
            }
            Com<IDirect3DVertexShader9> shader;
            const HRESULT result = device->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), shader.put());
            if (caps.VertexShaderVersion >= c.version) {
                Kit::expect_success(problems, std::string(c.profile) + " vertex shader with VertexShaderVersion 0x" + Text::hex(caps.VertexShaderVersion, 8), result);
            } else {
                Kit::expect_failure(problems, std::string(c.profile) + " vertex shader with VertexShaderVersion 0x" + Text::hex(caps.VertexShaderVersion, 8), result);
            }
        }
        for (const VersionCase& c : ps_cases) {
            std::vector<std::uint8_t> code;
            if (!D3d9Tools::compile(kPlainPs, c.profile, code, error)) {
                return Outcome::fail(error);
            }
            Com<IDirect3DPixelShader9> shader;
            const HRESULT result = device->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), shader.put());
            if (caps.PixelShaderVersion >= c.version) {
                Kit::expect_success(problems, std::string(c.profile) + " pixel shader with PixelShaderVersion 0x" + Text::hex(caps.PixelShaderVersion, 8), result);
            } else {
                Kit::expect_failure(problems, std::string(c.profile) + " pixel shader with PixelShaderVersion 0x" + Text::hex(caps.PixelShaderVersion, 8), result);
            }
        }
    }
    Kit::check_usable(problems, "after the cap probes", device);
    return problems.outcome();
}

Outcome CapsChecks::check_device_format_consistent(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    IDirect3D9* d3d = rig.d3d.get();
    const D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
    Problems problems;
    const D3DFORMAT color_formats[] = {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A1R5G5B5, D3DFMT_X1R5G5B5,
                                       D3DFMT_A4R4G4B4, D3DFMT_A2R10G10B10, D3DFMT_A16B16G16R16, D3DFMT_A16B16G16R16F,
                                       D3DFMT_A32B32G32R32F, D3DFMT_R32F, D3DFMT_G16R16F, D3DFMT_R16F, D3DFMT_L8,
                                       D3DFMT_A8, D3DFMT_V8U8, D3DFMT_DXT1, D3DFMT_DXT3, D3DFMT_DXT5, D3DFMT_MULTI2_ARGB8};
    const D3DFORMAT depth_formats[] = {D3DFMT_D16, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D32, D3DFMT_D15S1, D3DFMT_D24X4S4,
                                       D3DFMT_D16_LOCKABLE, D3DFMT_D32F_LOCKABLE, D3DFMT_D24FS8};
    int accepted = 0;
    int rejected = 0;
    auto judge = [&](const std::string& what, HRESULT query, HRESULT created) {
        if (query == D3D_OK) {
            ++accepted;
            if (FAILED(created)) {
                problems.add(what + ": CheckDeviceFormat says supported but creation failed with " + Kit::hr(created));
            }
        } else if (query == D3DERR_NOTAVAILABLE) {
            ++rejected;
            if (SUCCEEDED(created)) {
                problems.add(what + ": CheckDeviceFormat says not available but creation succeeded");
            }
        } else {
            problems.add(what + ": CheckDeviceFormat returned " + Kit::hr(query));
        }
    };
    for (const D3DFORMAT format : color_formats) {
        const std::string name = "format " + std::to_string(static_cast<unsigned>(format));
        for (const DWORD usage : {0ul, static_cast<unsigned long>(D3DUSAGE_RENDERTARGET), static_cast<unsigned long>(D3DUSAGE_DYNAMIC)}) {
            const HRESULT query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, usage, D3DRTYPE_TEXTURE, format);
            HRESULT created = S_OK;
            Com<IDirect3DTexture9> t = D3d9Tools::texture(device, 64, 64, 1, usage, format, D3DPOOL_DEFAULT, &created);
            judge(name + " texture usage " + std::to_string(usage), query, created);
        }
        {
            const HRESULT query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format);
            HRESULT created = S_OK;
            Com<IDirect3DSurface9> s = D3d9Tools::target(device, 64, 64, format, D3DMULTISAMPLE_NONE, false, &created);
            judge(name + " render target surface", query, created);
        }
        {
            const HRESULT query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, 0, D3DRTYPE_SURFACE, format);
            Com<IDirect3DSurface9> s;
            const HRESULT created = device->CreateOffscreenPlainSurface(64, 64, format, D3DPOOL_DEFAULT, s.put(), nullptr);
            judge(name + " offscreen plain surface", query, created);
        }
        if (SUCCEEDED(d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format))) {
            for (int samples = 2; samples <= 8; ++samples) {
                const auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
                DWORD levels = 0;
                const HRESULT query = d3d->CheckDeviceMultiSampleType(rig.ordinal, D3DDEVTYPE_HAL, format, TRUE, type, &levels);
                const std::string ms_name = name + " with " + std::to_string(samples) + " samples";
                if (query == D3D_OK) {
                    ++accepted;
                    if (levels < 1) {
                        problems.add(ms_name + ": CheckDeviceMultiSampleType succeeded with no quality level");
                        continue;
                    }
                    HRESULT created = S_OK;
                    Com<IDirect3DSurface9> top = D3d9Tools::target(device, 64, 64, format, type, false, &created, levels - 1);
                    if (FAILED(created)) {
                        problems.add(ms_name + ": the top quality level " + std::to_string(levels - 1) + " cannot be created: " + Kit::hr(created));
                    }
                    Com<IDirect3DSurface9> beyond = D3d9Tools::target(device, 64, 64, format, type, false, &created, levels);
                    if (SUCCEEDED(created)) {
                        problems.add(ms_name + ": quality level " + std::to_string(levels) + " is created although only " + std::to_string(levels) + " levels are reported");
                    }
                } else if (query == D3DERR_NOTAVAILABLE) {
                    ++rejected;
                    HRESULT created = S_OK;
                    Com<IDirect3DSurface9> s = D3d9Tools::target(device, 64, 64, format, type, false, &created, 0);
                    if (SUCCEEDED(created)) {
                        problems.add(ms_name + ": CheckDeviceMultiSampleType says not available but creation succeeded");
                    }
                } else {
                    problems.add(ms_name + ": CheckDeviceMultiSampleType returned " + Kit::hr(query));
                }
            }
        }
    }
    for (const D3DFORMAT format : depth_formats) {
        const std::string name = "depth format " + std::to_string(static_cast<unsigned>(format));
        const HRESULT query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, format);
        Com<IDirect3DSurface9> s;
        const HRESULT created = device->CreateDepthStencilSurface(64, 64, format, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr);
        judge(name + " surface", query, created);
        const HRESULT texture_query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, format);
        HRESULT texture_created = S_OK;
        Com<IDirect3DTexture9> t = D3d9Tools::texture(device, 64, 64, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT, &texture_created);
        judge(name + " texture", texture_query, texture_created);
        if (query == D3D_OK) {
            for (const D3DFORMAT color : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5}) {
                if (FAILED(d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, color))) {
                    continue;
                }
                const HRESULT match = d3d->CheckDepthStencilMatch(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, color, format);
                if (match != D3D_OK && match != D3DERR_NOTAVAILABLE) {
                    problems.add(name + ": CheckDepthStencilMatch returned " + Kit::hr(match));
                }
                if (match == D3D_OK) {
                    Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 64, 64, color);
                    Com<IDirect3DSurface9> original_rt;
                    Com<IDirect3DSurface9> original_ds;
                    device->GetRenderTarget(0, original_rt.put());
                    device->GetDepthStencilSurface(original_ds.put());
                    HRESULT set_rt = device->SetRenderTarget(0, rt.get());
                    HRESULT set_ds = device->SetDepthStencilSurface(s.get());
                    HRESULT clear = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
                    if (FAILED(set_rt) || FAILED(set_ds) || FAILED(clear)) {
                        problems.add(name + " with colour format " + std::to_string(static_cast<unsigned>(color)) + ": CheckDepthStencilMatch succeeded but binding or clearing the pair failed with " + Kit::hr(FAILED(set_rt) ? set_rt : FAILED(set_ds) ? set_ds : clear));
                    }
                    device->SetRenderTarget(0, original_rt.get());
                    device->SetDepthStencilSurface(original_ds.get());
                }
            }
        }
    }
    const D3DFORMAT convertible[] = {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A1R5G5B5};
    for (const D3DFORMAT source : convertible) {
        for (const D3DFORMAT destination : convertible) {
            const HRESULT query = d3d->CheckDeviceFormatConversion(rig.ordinal, D3DDEVTYPE_HAL, source, destination);
            if (query != D3D_OK && query != D3DERR_NOTAVAILABLE) {
                problems.add("CheckDeviceFormatConversion returned " + Kit::hr(query));
                continue;
            }
            Com<IDirect3DSurface9> from = D3d9Tools::target(device, 32, 32, source);
            Com<IDirect3DSurface9> to = D3d9Tools::target(device, 32, 32, destination);
            if (query == D3D_OK && from && to) {
                const HRESULT copy = device->StretchRect(from.get(), nullptr, to.get(), nullptr, D3DTEXF_NONE);
                if (FAILED(copy)) {
                    problems.add("format conversion " + std::to_string(static_cast<unsigned>(source)) + " to " + std::to_string(static_cast<unsigned>(destination)) + " is reported supported but StretchRect fails with " + Kit::hr(copy));
                }
            }
        }
    }
    context.measure("answers_supported", accepted);
    context.measure("answers_rejected", rejected);
    Kit::check_usable(problems, "after the format probes", device);
    return problems.outcome();
}

Outcome CapsChecks::ex_interface(Context& context) {
    DynLibrary library;
    if (!library.load("d3d9.dll")) {
        return Outcome::fail("d3d9.dll cannot be loaded");
    }
    using CreateEx = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    const CreateEx create = library.symbol<CreateEx>("Direct3DCreate9Ex");
    Problems problems;
    D3d9Rig plain;
    std::string error;
    if (!Kit::open(plain, false, error)) {
        return Outcome::fail(error);
    }
    if (create == nullptr) {
        context.note("d3d9.dll exports no Direct3DCreate9Ex");
        D3d9Rig attempt;
        if (attempt.open(true, error)) {
            problems.add("the Ex entry point is absent but the rig could create an IDirect3D9Ex");
        }
        return problems.outcome();
    }
    D3d9Rig ex;
    if (!Kit::open(ex, true, error)) {
        return Outcome::fail("Direct3DCreate9Ex is exported but unusable: " + error);
    }
    D3DCAPS9 a{};
    D3DCAPS9 b{};
    Problems clean;
    if (FAILED(plain.dev()->GetDeviceCaps(&a)) || FAILED(ex.dev()->GetDeviceCaps(&b))) {
        return Outcome::fail("GetDeviceCaps failed");
    }
    a.Caps = b.Caps = 0;
    a.Caps2 = b.Caps2 = 0;
    a.Caps3 = b.Caps3 = 0;
    if (std::memcmp(&a, &b, sizeof(a)) != 0) {
        const auto* pa = reinterpret_cast<const std::uint32_t*>(&a);
        const auto* pb = reinterpret_cast<const std::uint32_t*>(&b);
        for (std::size_t i = 0; i < sizeof(a) / 4; ++i) {
            if (pa[i] != pb[i]) {
                clean.add("the caps of the 9 and 9Ex devices differ at dword " + std::to_string(i) + ": 0x" + Text::hex(pa[i], 8) + " against 0x" + Text::hex(pb[i], 8));
                break;
            }
        }
    }
    D3DCAPS9 ex_from_object{};
    if (FAILED(ex.d3d->GetDeviceCaps(ex.ordinal, D3DDEVTYPE_HAL, &ex_from_object)) || ex_from_object.VertexShaderVersion != b.VertexShaderVersion || ex_from_object.PixelShaderVersion != b.PixelShaderVersion) {
        clean.add("the caps of the 9Ex object differ from those of its device");
    }
    IDirect3DDevice9Ex* device = ex.device_ex.get();
    int priority = 99;
    Kit::expect_success(clean, "GetGPUThreadPriority", device->GetGPUThreadPriority(&priority));
    if (priority < -7 || priority > 7) {
        clean.add("the GPU thread priority " + std::to_string(priority) + " is outside -7..7");
    }
    Kit::expect_success(clean, "SetGPUThreadPriority(2)", device->SetGPUThreadPriority(2));
    int again = 0;
    device->GetGPUThreadPriority(&again);
    if (again != 2) {
        clean.add("the GPU thread priority reads " + std::to_string(again) + " after setting 2");
    }
    device->SetGPUThreadPriority(priority);
    Kit::expect_failure(clean, "SetGPUThreadPriority(100)", device->SetGPUThreadPriority(100));
    UINT latency = 0;
    Kit::expect_success(clean, "GetMaximumFrameLatency", device->GetMaximumFrameLatency(&latency));
    if (latency < 1 || latency > 20) {
        clean.add("the maximum frame latency " + std::to_string(latency) + " is outside 1..20");
    }
    Kit::expect_success(clean, "SetMaximumFrameLatency(1)", device->SetMaximumFrameLatency(1));
    UINT one = 0;
    device->GetMaximumFrameLatency(&one);
    if (one != 1) {
        clean.add("the maximum frame latency reads " + std::to_string(one) + " after setting 1");
    }
    Kit::expect_failure(clean, "SetMaximumFrameLatency(21)", device->SetMaximumFrameLatency(21));
    device->SetMaximumFrameLatency(latency);
    const HRESULT state = device->CheckDeviceState(ex.window.hwnd());
    if (state != S_OK && state != S_PRESENT_OCCLUDED && state != S_PRESENT_MODE_CHANGED) {
        clean.add("CheckDeviceState returned " + Kit::hr(state));
    }
    Com<IDirect3D9Ex> from_plain;
    plain.d3d->QueryInterface(__uuidof(IDirect3D9Ex), from_plain.put_void());
    context.measure("plain_object_exposes_ex", static_cast<bool>(from_plain));
    return clean.outcome();
}

Outcome CapsChecks::adapter_identity(Context& context) {
    D3d9Rig rig;
    std::string error;
    if (!rig.open(true, error)) {
        return Outcome::fail(error);
    }
    std::vector<AvAdapter> dxgi;
    if (!AvAdapters::all(dxgi, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    IDirect3D9Ex* d3d = rig.d3d_ex.get();
    const UINT count = d3d->GetAdapterCount();
    if (count == 0) {
        return Outcome::fail("GetAdapterCount is zero");
    }
    std::vector<std::uint64_t> seen;
    int ours = 0;
    for (UINT i = 0; i < count; ++i) {
        D3DADAPTER_IDENTIFIER9 id{};
        LUID luid{};
        D3DDISPLAYMODE mode{};
        const std::string name = "adapter " + std::to_string(i);
        Kit::expect_success(problems, name + " GetAdapterIdentifier", d3d->GetAdapterIdentifier(i, 0, &id));
        Kit::expect_success(problems, name + " GetAdapterLUID", d3d->GetAdapterLUID(i, &luid));
        Kit::expect_success(problems, name + " GetAdapterDisplayMode", d3d->GetAdapterDisplayMode(i, &mode));
        if (d3d->GetAdapterMonitor(i) == nullptr) {
            problems.add(name + " has no monitor");
        }
        if (mode.Width == 0 || mode.Height == 0 || mode.Format == D3DFMT_UNKNOWN) {
            problems.add(name + " reports an empty display mode");
        }
        if (id.Description[0] == '\0' || id.DeviceName[0] == '\0') {
            problems.add(name + " has an empty description or device name");
        }
        const std::uint64_t key = AvAdapters::key(luid);
        const AvAdapter* match = AvAdapters::find(dxgi, key);
        if (match == nullptr) {
            problems.add(name + " carries LUID " + Text::hex(key, 16) + " that DXGI does not report");
            continue;
        }
        if (id.VendorId != match->vendor || id.DeviceId != match->device || id.SubSysId != match->subsystem || id.Revision != match->revision) {
            problems.add(name + " identifies as " + Text::hex(id.VendorId, 4) + ":" + Text::hex(id.DeviceId, 4) + " subsystem " + Text::hex(id.SubSysId, 8) + " revision " + Text::hex(id.Revision, 2) + " while DXGI reports " + Text::hex(match->vendor, 4) + ":" + Text::hex(match->device, 4) + " subsystem " + Text::hex(match->subsystem, 8) + " revision " + Text::hex(match->revision, 2));
        }
        if (key == AvAdapters::key(rig.adapter.luid)) {
            ++ours;
            bool named = rig.adapter.outputs.empty();
            for (const AvOutput& output : rig.adapter.outputs) {
                named = named || output.device_name == id.DeviceName;
            }
            if (!named) {
                problems.add(name + " has device name " + std::string(id.DeviceName) + " that is not one of the DXGI outputs of the adapter");
            }
            const UINT modes = d3d->GetAdapterModeCount(i, D3DFMT_X8R8G8B8);
            if (modes == 0) {
                problems.add(name + " enumerates no X8R8G8B8 modes");
            } else {
                D3DDISPLAYMODE first{};
                Kit::expect_success(problems, name + " EnumAdapterModes(0)", d3d->EnumAdapterModes(i, D3DFMT_X8R8G8B8, 0, &first));
                D3DDISPLAYMODE past{};
                Kit::expect_failure(problems, name + " EnumAdapterModes past the count", d3d->EnumAdapterModes(i, D3DFMT_X8R8G8B8, modes, &past));
            }
            D3DDISPLAYMODEEX ex_mode{};
            ex_mode.Size = sizeof(ex_mode);
            D3DDISPLAYROTATION rotation{};
            if (SUCCEEDED(d3d->GetAdapterDisplayModeEx(i, &ex_mode, &rotation)) && (ex_mode.Width != mode.Width || ex_mode.Height != mode.Height)) {
                problems.add(name + ": GetAdapterDisplayModeEx and GetAdapterDisplayMode disagree on the size");
            }
        }
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
        }
    }
    if (ours == 0) {
        problems.add("no Direct3D 9 adapter carries the LUID of the accel-virt adapter");
    }
    D3DADAPTER_IDENTIFIER9 again{};
    D3DADAPTER_IDENTIFIER9 first{};
    d3d->GetAdapterIdentifier(rig.ordinal, 0, &first);
    d3d->GetAdapterIdentifier(rig.ordinal, 0, &again);
    if (std::memcmp(&first, &again, sizeof(first)) != 0) {
        problems.add("two GetAdapterIdentifier calls disagree");
    }
    context.measure("adapters", count);
    context.measure("ordinal", rig.ordinal);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
