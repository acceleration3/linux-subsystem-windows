#include "render/d3d9/harness.hpp"
#include "render/host/registration.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

class CapsRig {
public:
    [[nodiscard]] bool open(std::string& error) {
        const rr::CaseSpec spec("caps", "rig");
        return kit.open(false, spec, error) == OpenStatus::Ok;
    }

    [[nodiscard]] IDirect3DDevice9* device() { return kit.device(); }

    [[nodiscard]] Com<IDirect3D9> factory() {
        Com<IDirect3D9> f;
        kit.device()->GetDirect3D(f.put());
        return f;
    }

    [[nodiscard]] UINT ordinal() {
        D3DDEVICE_CREATION_PARAMETERS p{};
        kit.device()->GetCreationParameters(&p);
        return p.AdapterOrdinal;
    }

    Kit kit;
};

using CapsCheck = std::function<std::string(CapsRig&)>;

void add_check(Suite& suite, const char* name, CapsCheck check) {
    CaseRegistration::add(suite, "d3d9", "d3d9", rr::CaseSpec("caps", name), [check](Context&) {
        CapsRig rig;
        std::string error;
        if (!rig.open(error)) {
            return Outcome::fail("device creation failed: " + error);
        }
        const std::string problem = check(rig);
        return problem.empty() ? Outcome::ok() : Outcome::fail(problem);
    });
}

std::string format_contract(CapsRig& rig, DWORD usage, D3DRESOURCETYPE type, const std::vector<std::pair<D3DFORMAT, const char*>>& formats) {
    Com<IDirect3D9> factory = rig.factory();
    std::string problems;
    for (const auto& [format, name] : formats) {
        const bool listed = SUCCEEDED(factory->CheckDeviceFormat(rig.ordinal(), D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, type, format));
        bool created = false;
        IDirect3DDevice9* d = rig.device();
        if (type == D3DRTYPE_TEXTURE) {
            Com<IDirect3DTexture9> t;
            created = SUCCEEDED(d->CreateTexture(32, 32, 1, usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC), format,
                                                 (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, t.put(), nullptr));
        } else if (type == D3DRTYPE_SURFACE && (usage & D3DUSAGE_RENDERTARGET)) {
            Com<IDirect3DSurface9> s;
            created = SUCCEEDED(d->CreateRenderTarget(32, 32, format, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr));
        } else if (type == D3DRTYPE_SURFACE) {
            Com<IDirect3DSurface9> s;
            created = SUCCEEDED(d->CreateDepthStencilSurface(32, 32, format, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr));
        } else if (type == D3DRTYPE_CUBETEXTURE) {
            Com<IDirect3DCubeTexture9> t;
            created = SUCCEEDED(d->CreateCubeTexture(16, 1, 0, format, D3DPOOL_MANAGED, t.put(), nullptr));
        } else if (type == D3DRTYPE_VOLUMETEXTURE) {
            Com<IDirect3DVolumeTexture9> t;
            created = SUCCEEDED(d->CreateVolumeTexture(8, 8, 8, 1, 0, format, D3DPOOL_MANAGED, t.put(), nullptr));
        }
        if (listed != created) {
            problems += std::string(name) + (listed ? " is reported usable but cannot be created; " : " is created although CheckDeviceFormat refuses it; ");
        }
    }
    return problems;
}

void add_all(Suite& suite) {
    const std::vector<std::pair<D3DFORMAT, const char*>> textures = {
        {D3DFMT_A8R8G8B8, "A8R8G8B8"}, {D3DFMT_X8R8G8B8, "X8R8G8B8"}, {D3DFMT_R5G6B5, "R5G6B5"}, {D3DFMT_X1R5G5B5, "X1R5G5B5"}, {D3DFMT_A1R5G5B5, "A1R5G5B5"},
        {D3DFMT_A4R4G4B4, "A4R4G4B4"}, {D3DFMT_A8, "A8"}, {D3DFMT_L8, "L8"}, {D3DFMT_A8L8, "A8L8"}, {D3DFMT_L16, "L16"}, {D3DFMT_A2R10G10B10, "A2R10G10B10"},
        {D3DFMT_A2B10G10R10, "A2B10G10R10"}, {D3DFMT_A16B16G16R16, "A16B16G16R16"}, {D3DFMT_G16R16, "G16R16"}, {D3DFMT_R16F, "R16F"}, {D3DFMT_G16R16F, "G16R16F"},
        {D3DFMT_A16B16G16R16F, "A16B16G16R16F"}, {D3DFMT_R32F, "R32F"}, {D3DFMT_G32R32F, "G32R32F"}, {D3DFMT_A32B32G32R32F, "A32B32G32R32F"}, {D3DFMT_V8U8, "V8U8"},
        {D3DFMT_Q8W8V8U8, "Q8W8V8U8"}, {D3DFMT_DXT1, "DXT1"}, {D3DFMT_DXT3, "DXT3"}, {D3DFMT_DXT5, "DXT5"}, {D3DFMT_A8B8G8R8, "A8B8G8R8"}, {D3DFMT_X8B8G8R8, "X8B8G8R8"},
    };
    add_check(suite, "texture_formats_match_creation", [textures](CapsRig& r) { return format_contract(r, 0, D3DRTYPE_TEXTURE, textures); });
    add_check(suite, "cube_texture_formats_match_creation", [textures](CapsRig& r) { return format_contract(r, 0, D3DRTYPE_CUBETEXTURE, textures); });
    add_check(suite, "volume_texture_formats_match_creation", [textures](CapsRig& r) { return format_contract(r, 0, D3DRTYPE_VOLUMETEXTURE, textures); });
    add_check(suite, "render_target_formats_match_creation", [textures](CapsRig& r) { return format_contract(r, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, textures); });
    add_check(suite, "render_target_texture_formats_match_creation", [textures](CapsRig& r) { return format_contract(r, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, textures); });
    add_check(suite, "depth_stencil_formats_match_creation", [](CapsRig& r) {
        return format_contract(r, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
                               {{D3DFMT_D16, "D16"}, {D3DFMT_D24X8, "D24X8"}, {D3DFMT_D24S8, "D24S8"}, {D3DFMT_D32, "D32"}, {D3DFMT_D24FS8, "D24FS8"}, {D3DFMT_D15S1, "D15S1"},
                                {D3DFMT_D24X4S4, "D24X4S4"}, {D3DFMT_D16_LOCKABLE, "D16_LOCKABLE"}, {D3DFMT_D32F_LOCKABLE, "D32F_LOCKABLE"}});
    });
    add_check(suite, "depth_stencil_match_allows_creation_pairs", [](CapsRig& r) {
        Com<IDirect3D9> f = r.factory();
        std::string problems;
        const D3DFORMAT depths[] = {D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D32};
        for (D3DFORMAT depth : depths) {
            const bool ok = SUCCEEDED(f->CheckDepthStencilMatch(r.ordinal(), D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, depth));
            Com<IDirect3DSurface9> rt;
            Com<IDirect3DSurface9> ds;
            const bool made = SUCCEEDED(r.device()->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, rt.put(), nullptr)) &&
                              SUCCEEDED(r.device()->CreateDepthStencilSurface(32, 32, depth, D3DMULTISAMPLE_NONE, 0, FALSE, ds.put(), nullptr)) &&
                              SUCCEEDED(r.device()->SetRenderTarget(0, rt.get())) && SUCCEEDED(r.device()->SetDepthStencilSurface(ds.get()));
            if (ok && !made) {
                problems += "a pairing CheckDepthStencilMatch allows cannot be bound; ";
            }
        }
        return problems;
    });
    add_check(suite, "multisample_types_match_creation", [](CapsRig& r) {
        Com<IDirect3D9> f = r.factory();
        std::string problems;
        for (int type = 2; type <= 8; ++type) {
            DWORD quality = 0;
            const bool ok = SUCCEEDED(f->CheckDeviceMultiSampleType(r.ordinal(), D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, static_cast<D3DMULTISAMPLE_TYPE>(type), &quality));
            Com<IDirect3DSurface9> s;
            const bool made = SUCCEEDED(r.device()->CreateRenderTarget(64, 64, D3DFMT_A8R8G8B8, static_cast<D3DMULTISAMPLE_TYPE>(type), 0, FALSE, s.put(), nullptr));
            if (ok != made) {
                problems += std::to_string(type) + "x samples: reported " + (ok ? "usable" : "unusable") + " but creation " + (made ? "worked" : "failed") + "; ";
            }
        }
        return problems;
    });
    add_check(suite, "declaration_types_match_caps", [](CapsRig& r) {
        D3DCAPS9 caps = {};
        r.device()->GetDeviceCaps(&caps);
        struct T { D3DDECLTYPE type; DWORD cap; const char* name; };
        const T types[] = {{D3DDECLTYPE_UBYTE4, D3DDTCAPS_UBYTE4, "UBYTE4"}, {D3DDECLTYPE_UBYTE4N, D3DDTCAPS_UBYTE4N, "UBYTE4N"}, {D3DDECLTYPE_SHORT2N, D3DDTCAPS_SHORT2N, "SHORT2N"},
                           {D3DDECLTYPE_SHORT4N, D3DDTCAPS_SHORT4N, "SHORT4N"}, {D3DDECLTYPE_USHORT2N, D3DDTCAPS_USHORT2N, "USHORT2N"}, {D3DDECLTYPE_USHORT4N, D3DDTCAPS_USHORT4N, "USHORT4N"},
                           {D3DDECLTYPE_UDEC3, D3DDTCAPS_UDEC3, "UDEC3"}, {D3DDECLTYPE_DEC3N, D3DDTCAPS_DEC3N, "DEC3N"}, {D3DDECLTYPE_FLOAT16_2, D3DDTCAPS_FLOAT16_2, "FLOAT16_2"},
                           {D3DDECLTYPE_FLOAT16_4, D3DDTCAPS_FLOAT16_4, "FLOAT16_4"}};
        std::string problems;
        for (const T& t : types) {
            const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0}, {0, 12, static_cast<BYTE>(t.type), 0, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
            Com<IDirect3DVertexDeclaration9> d;
            const bool made = SUCCEEDED(r.device()->CreateVertexDeclaration(el, d.put()));
            const bool claimed = (caps.DeclTypes & t.cap) != 0;
            if (made != claimed) {
                problems += std::string(t.name) + ": caps say " + (claimed ? "supported" : "unsupported") + " but declaration creation " + (made ? "worked" : "failed") + "; ";
            }
        }
        return problems;
    });
    add_check(suite, "max_texture_size_is_creatable", [](CapsRig& r) {
        D3DCAPS9 caps = {};
        r.device()->GetDeviceCaps(&caps);
        Com<IDirect3DTexture9> wide;
        Com<IDirect3DTexture9> tall;
        const HRESULT a = r.device()->CreateTexture(caps.MaxTextureWidth, 1, 1, 0, D3DFMT_A8, D3DPOOL_DEFAULT, wide.put(), nullptr);
        const HRESULT b = r.device()->CreateTexture(1, caps.MaxTextureHeight, 1, 0, D3DFMT_A8, D3DPOOL_DEFAULT, tall.put(), nullptr);
        std::string problems;
        if (FAILED(a) && caps.MaxTextureAspectRatio == 0) problems += "a " + std::to_string(caps.MaxTextureWidth) + "x1 texture within the reported maximum cannot be created: " + D3d9Tools::hr(a) + "; ";
        if (FAILED(b) && caps.MaxTextureAspectRatio == 0) problems += "a 1x" + std::to_string(caps.MaxTextureHeight) + " texture within the reported maximum cannot be created: " + D3d9Tools::hr(b) + "; ";
        Com<IDirect3DTexture9> over;
        if (SUCCEEDED(r.device()->CreateTexture(caps.MaxTextureWidth * 2, 1, 1, 0, D3DFMT_A8, D3DPOOL_DEFAULT, over.put(), nullptr)) && caps.MaxTextureWidth < 65536) problems += "a texture twice the reported maximum width was created; ";
        return problems;
    });
    add_check(suite, "limits_are_self_consistent", [](CapsRig& r) {
        D3DCAPS9 caps = {};
        r.device()->GetDeviceCaps(&caps);
        std::string problems;
        if (caps.DeviceType != D3DDEVTYPE_HAL) problems += "device type is not HAL; ";
        if (caps.MaxStreams < 1 || caps.MaxStreams > 16) problems += "MaxStreams out of range; ";
        if (caps.MaxVertexIndex < 0xFFFF) problems += "MaxVertexIndex below 65535; ";
        if (caps.MaxPrimitiveCount < 0xFFFF) problems += "MaxPrimitiveCount below 65535; ";
        if (caps.NumSimultaneousRTs < 1) problems += "no render target; ";
        if (caps.MaxTextureWidth < 256 || caps.MaxTextureHeight < 256) problems += "tiny maximum texture; ";
        if (caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) && caps.VertexShaderVersion < D3DVS_VERSION(3, 0)) problems += "ps 3.0 without vs 3.0; ";
        if (caps.MaxSimultaneousTextures < 1 || caps.MaxTextureBlendStages < 1) problems += "no fixed-function texturing; ";
        return problems;
    });
    add_check(suite, "adapter_caps_equal_device_caps", [](CapsRig& r) {
        D3DCAPS9 from_device = {};
        D3DCAPS9 from_adapter = {};
        r.device()->GetDeviceCaps(&from_device);
        const HRESULT result = r.factory()->GetDeviceCaps(r.ordinal(), D3DDEVTYPE_HAL, &from_adapter);
        if (FAILED(result)) {
            return "IDirect3D9::GetDeviceCaps: " + D3d9Tools::hr(result);
        }
        std::string problems;
        if (from_device.VertexShaderVersion != from_adapter.VertexShaderVersion) problems += "vertex shader versions differ; ";
        if (from_device.PixelShaderVersion != from_adapter.PixelShaderVersion) problems += "pixel shader versions differ; ";
        if (from_device.MaxTextureWidth != from_adapter.MaxTextureWidth) problems += "texture sizes differ; ";
        if (from_device.NumSimultaneousRTs != from_adapter.NumSimultaneousRTs) problems += "render target counts differ; ";
        if (from_device.TextureOpCaps != from_adapter.TextureOpCaps) problems += "texture op caps differ; ";
        return problems;
    });
    add_check(suite, "display_modes_enumerate", [](CapsRig& r) {
        Com<IDirect3D9> f = r.factory();
        const UINT count = f->GetAdapterModeCount(r.ordinal(), D3DFMT_X8R8G8B8);
        D3DDISPLAYMODE current = {};
        const HRESULT result = f->GetAdapterDisplayMode(r.ordinal(), &current);
        if (count == 0) return std::string("no X8R8G8B8 display modes");
        if (FAILED(result)) return "GetAdapterDisplayMode: " + D3d9Tools::hr(result);
        D3DDISPLAYMODE first = {};
        if (FAILED(f->EnumAdapterModes(r.ordinal(), D3DFMT_X8R8G8B8, 0, &first)) || first.Width == 0 || first.Height == 0) return std::string("EnumAdapterModes returned an empty mode");
        return std::string();
    });
}

}  // namespace

void add_caps(Suite& suite) { add_all(suite); }

}  // namespace lsw::suites::render::d3d9
