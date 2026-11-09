#include "render/d3d9/kit.hpp"

#include "render/d3d/convert.hpp"
#include "support/d3d9_tools.hpp"
#include "support/dyn_library.hpp"

#include <cstring>

namespace lsw::suites::render::d3d9 {

namespace {

struct On12Args {
    BOOL enable;
    IUnknown* device12;
    IUnknown* queues[2];
    UINT queue_count;
    UINT node_mask;
};

using CreateOn12 = IDirect3D9*(WINAPI*)(UINT, On12Args*, UINT);
using CreateOn12Ex = HRESULT(WINAPI*)(UINT, On12Args*, UINT, IDirect3D9Ex**);

}  // namespace

bool Kit::create_device(IDirect3D9* factory, UINT ordinal, bool ex, std::string& error) {
    TestWindowConfig config;
    TestWindow::fit_config(config, 128, 128);
    if (!window_.create(config, error)) {
        return false;
    }
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 128;
    pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window_.hwnd();
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    HRESULT result = E_FAIL;
    if (ex) {
        IDirect3D9Ex* raw_ex = nullptr;
        factory->QueryInterface(__uuidof(IDirect3D9Ex), reinterpret_cast<void**>(&raw_ex));
        Com<IDirect3D9Ex> factory_ex(raw_ex);
        Com<IDirect3DDevice9Ex> created;
        result = factory_ex ? factory_ex->CreateDeviceEx(ordinal, D3DDEVTYPE_HAL, window_.hwnd(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                                         nullptr, created.put())
                            : E_NOINTERFACE;
        if (SUCCEEDED(result)) {
            device_ = created.as<IDirect3DDevice9>();
        }
    } else {
        result = factory->CreateDevice(ordinal, D3DDEVTYPE_HAL, window_.hwnd(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, device_.put());
        if (FAILED(result)) {
            result = factory->CreateDevice(ordinal, D3DDEVTYPE_HAL, window_.hwnd(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, device_.put());
        }
    }
    if (FAILED(result)) {
        error = "CreateDevice: " + D3d9Tools::hr(result);
        return false;
    }
    return true;
}

OpenStatus Kit::open(bool use_warp, const rr::CaseSpec& spec, std::string& error) {
    warp = use_warp;
    const bool ex = spec.variant() != 0;
    if (!warp) {
        if (!rig_.open(ex, error)) {
            return OpenStatus::Failed;
        }
        name = rig_.adapter.name;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig_.create(settings, error)) {
            return OpenStatus::Failed;
        }
        device_.reset(rig_.dev());
        device_->AddRef();
        return OpenStatus::Ok;
    }
    name = "WARP (D3D9On12)";
    if (!warp12_.open_warp(warp_level, error)) {
        return OpenStatus::Failed;
    }
    static DynLibrary library;
    if (!library.loaded() && !library.load("d3d9.dll")) {
        error = "d3d9.dll cannot be loaded";
        return OpenStatus::Failed;
    }
    On12Args args{};
    args.enable = TRUE;
    args.device12 = warp12_.device();
    if (ex) {
        auto create = library.symbol<CreateOn12Ex>("Direct3DCreate9On12Ex");
        if (create == nullptr) {
            error = "d3d9.dll has no Direct3DCreate9On12Ex";
            return OpenStatus::Failed;
        }
        Com<IDirect3D9Ex> factory;
        const HRESULT result = create(D3D_SDK_VERSION, &args, 1, factory.put());
        if (FAILED(result)) {
            error = "Direct3DCreate9On12Ex: " + D3d9Tools::hr(result);
            return OpenStatus::Failed;
        }
        on12_factory_ = factory.as<IDirect3D9>();
    } else {
        auto create = library.symbol<CreateOn12>("Direct3DCreate9On12");
        if (create == nullptr) {
            error = "d3d9.dll has no Direct3DCreate9On12";
            return OpenStatus::Failed;
        }
        on12_factory_.reset(create(D3D_SDK_VERSION, &args, 1));
        if (!on12_factory_) {
            error = "Direct3DCreate9On12 returned no factory";
            return OpenStatus::Failed;
        }
    }
    return create_device(on12_factory_.get(), 0, ex, error) ? OpenStatus::Ok : OpenStatus::Failed;
}

Com<IDirect3DVertexShader9> Kit::vertex_shader(const std::string& source, std::string& error) {
    return D3d9Tools::vs(device(), source, "vs_3_0", error);
}

Com<IDirect3DPixelShader9> Kit::pixel_shader(const std::string& source, std::string& error) {
    return D3d9Tools::ps(device(), source, "ps_3_0", error);
}

bool Kit::make_target(Surface& out, UINT width, UINT height, D3DFORMAT format, std::string& error) {
    const HRESULT result = device()->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, FALSE, out.surface.put(), nullptr);
    if (FAILED(result)) {
        error = "CreateRenderTarget: " + D3d9Tools::hr(result);
        return false;
    }
    out.format = format;
    out.width = width;
    out.height = height;
    return true;
}

bool Kit::make_depth(Surface& out, UINT width, UINT height, std::string& error) {
    const HRESULT result = device()->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, out.surface.put(), nullptr);
    if (FAILED(result)) {
        error = "CreateDepthStencilSurface: " + D3d9Tools::hr(result);
        return false;
    }
    out.format = D3DFMT_D24S8;
    out.width = width;
    out.height = height;
    return true;
}

bool Kit::read_color(Surface& target, rr::Image& out, std::string& error) {
    rr::PixelKind kind = rr::PixelKind::Bgra8Unorm;
    switch (target.format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
            kind = rr::PixelKind::Bgra8Unorm;
            break;
        case D3DFMT_R32F:
            kind = rr::PixelKind::R32Float;
            break;
        case D3DFMT_A16B16G16R16F:
            kind = rr::PixelKind::Rgba16Float;
            break;
        case D3DFMT_A32B32G32R32F:
            kind = rr::PixelKind::Rgba32Float;
            break;
        default:
            error = "no image kind for the surface format";
            return false;
    }
    rr::Image image(target.width, target.height, kind);
    const auto copy_rows = [&](IDirect3DSurface9* surface) {
        D3DLOCKED_RECT locked{};
        if (FAILED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            return false;
        }
        for (UINT y = 0; y < target.height; ++y) {
            std::memcpy(image.row(y), static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch),
                        image.stride());
        }
        surface->UnlockRect();
        return true;
    };
    Com<IDirect3DSurface9> staging;
    HRESULT result = device()->CreateOffscreenPlainSurface(target.width, target.height, target.format, D3DPOOL_SYSTEMMEM, staging.put(), nullptr);
    if (FAILED(result)) {
        error = "CreateOffscreenPlainSurface: " + D3d9Tools::hr(result);
        return false;
    }
    result = device()->GetRenderTargetData(target.surface.get(), staging.get());
    bool read = SUCCEEDED(result) && copy_rows(staging.get());
    if (!read && warp) {
        Com<IDirect3DSurface9> lockable;
        if (SUCCEEDED(device()->CreateRenderTarget(target.width, target.height, target.format, D3DMULTISAMPLE_NONE, 0, TRUE, lockable.put(), nullptr)) &&
            SUCCEEDED(device()->StretchRect(target.surface.get(), nullptr, lockable.get(), nullptr, D3DTEXF_NONE))) {
            read = copy_rows(lockable.get());
        }
    }
    if (!read) {
        error = "GetRenderTargetData: " + D3d9Tools::hr(result);
        return false;
    }
    if (target.format == D3DFMT_X8R8G8B8) {
        for (UINT y = 0; y < target.height; ++y) {
            for (UINT x = 0; x < target.width; ++x) {
                image.row(y)[x * 4 + 3] = 255;
            }
        }
    }
    out = std::move(image);
    return true;
}

bool Kit::end_frame(std::string& error) {
    Com<IDirect3DDevice9Ex> extended = device_.as<IDirect3DDevice9Ex>();
    const HRESULT result = extended ? extended->PresentEx(nullptr, nullptr, nullptr, nullptr, 0) : device_->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(result)) {
        error = "Present: " + D3d9Tools::hr(result);
        return false;
    }
    return true;
}

bool Kit::begin(std::string& error) {
    const HRESULT result = device()->BeginScene();
    if (FAILED(result)) {
        error = "BeginScene: " + D3d9Tools::hr(result);
        return false;
    }
    return true;
}

bool Kit::end(std::string& error) {
    const HRESULT result = device()->EndScene();
    if (FAILED(result)) {
        error = "EndScene: " + D3d9Tools::hr(result);
        return false;
    }
    return true;
}

}  // namespace lsw::suites::render::d3d9
