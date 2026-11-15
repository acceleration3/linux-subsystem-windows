#include "sanity/d3d9/lost_checks.hpp"

#include "core/text.hpp"
#include "sanity/d3d9/kit.hpp"

#include <vector>

namespace lsw::suites::sanity::d3d9 {

namespace {

class ModeGuard {
public:
    ModeGuard(const std::wstring& name, const DEVMODEW& saved) : name_(name), saved_(saved) {}

    ModeGuard(const ModeGuard&) = delete;
    ModeGuard& operator=(const ModeGuard&) = delete;

    ~ModeGuard() { restore(); }

    void restore() {
        if (changed_) {
            ::ChangeDisplaySettingsExW(name_.c_str(), &saved_, nullptr, 0, nullptr);
            changed_ = false;
        }
    }

    void changed() { changed_ = true; }

private:
    std::wstring name_;
    DEVMODEW saved_;
    bool changed_ = false;
};

bool presentable(HRESULT result) {
    return result == S_OK || result == S_PRESENT_OCCLUDED || result == S_PRESENT_MODE_CHANGED;
}

}  // namespace

Outcome LostChecks::reset_semantics(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.width = 128;
    settings.height = 128;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Kit::expect(problems, "TestCooperativeLevel on a fresh device", device->TestCooperativeLevel(), D3D_OK);
    Com<IDirect3DTexture9> managed = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    if (!managed || !D3d9Tools::write_texture_level(managed.get(), 0, 91, error)) {
        return Outcome::fail("the managed texture cannot be prepared: " + error);
    }
    const SceneImage expected = D3d9Tools::image_of(64, 64, D3d9Tools::pattern(91, 64, 64));
    auto managed_ok = [&](const std::string& stage) {
        SceneImage image;
        std::string where;
        if (!D3d9Tools::sample(device, managed.get(), 64, 64, image, error)) {
            problems.add(stage + ": " + error);
        } else if (!D3d9Tools::same(expected, image, 0, where)) {
            problems.add(stage + ": the managed texture lost its content: " + where);
        }
    };
    managed_ok("before Reset");
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Com<IDirect3DTexture9> held = D3d9Tools::texture(device, 32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    Com<IDirect3DSurface9> held_surface = held ? D3d9Tools::level(held.get(), 0) : Com<IDirect3DSurface9>();
    if (!held) {
        return Outcome::fail("a default-pool render target texture cannot be created");
    }
    device->ColorFill(held_surface.get(), nullptr, 0xFF123456u);
    settings.width = 160;
    settings.height = 96;
    D3DPRESENT_PARAMETERS pp = rig.parameters(settings);
    Kit::expect_failure(problems, "Reset with a default-pool resource still alive", device->Reset(&pp));
    held_surface.reset();
    held.reset();
    HRESULT result = device->Reset(&pp);
    if (FAILED(result)) {
        problems.add("Reset after releasing the default-pool resources failed with " + Kit::hr(result));
        return problems.outcome();
    }
    Kit::expect(problems, "TestCooperativeLevel after Reset", device->TestCooperativeLevel(), D3D_OK);
    Com<IDirect3DSurface9> back;
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put());
    D3DSURFACE_DESC desc{};
    if (back) {
        back->GetDesc(&desc);
    }
    if (desc.Width != 160 || desc.Height != 96) {
        problems.add("the back buffer is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " after Reset to 160x96");
    }
    D3DVIEWPORT9 viewport{};
    device->GetViewport(&viewport);
    if (viewport.Width != 160 || viewport.Height != 96) {
        problems.add("the viewport is " + std::to_string(viewport.Width) + "x" + std::to_string(viewport.Height) + " after Reset");
    }
    Com<IDirect3DSurface9> depth;
    if (SUCCEEDED(device->GetDepthStencilSurface(depth.put()))) {
        D3DSURFACE_DESC depth_desc{};
        depth->GetDesc(&depth_desc);
        if (depth_desc.Width != 160 || depth_desc.Height != 96) {
            problems.add("the automatic depth buffer is " + std::to_string(depth_desc.Width) + "x" + std::to_string(depth_desc.Height) + " after Reset");
        }
    }
    DWORD cull = 0;
    device->GetRenderState(D3DRS_CULLMODE, &cull);
    if (cull != D3DCULL_CCW) {
        problems.add("a render state kept its value across Reset: CULLMODE is " + std::to_string(cull));
    }
    managed_ok("after Reset to 160x96");
    Com<IDirect3DTexture9> fresh = D3d9Tools::texture(device, 32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    if (!fresh) {
        problems.add("a default-pool resource cannot be created after Reset");
    } else {
        Com<IDirect3DSurface9> fresh_surface = D3d9Tools::level(fresh.get(), 0);
        device->ColorFill(fresh_surface.get(), nullptr, 0xFF654321u);
        SceneImage image;
        std::string where;
        if (!D3d9Tools::sample(device, fresh.get(), 32, 32, image, error) || !D3d9Tools::uniform(image, 0xFF654321u, 0, where)) {
            problems.add("a default-pool resource created after Reset does not render: " + where + error);
        }
    }
    fresh.reset();
    back.reset();
    depth.reset();
    DWORD levels = 0;
    if (SUCCEEDED(rig.d3d->CheckDeviceMultiSampleType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &levels)) &&
        SUCCEEDED(rig.d3d->CheckDeviceMultiSampleType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_D24S8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &levels))) {
        settings.multisample = D3DMULTISAMPLE_4_SAMPLES;
        pp = rig.parameters(settings);
        result = device->Reset(&pp);
        if (FAILED(result)) {
            problems.add("Reset to a 4x multisampled back buffer failed with " + Kit::hr(result));
        } else {
            Com<IDirect3DSurface9> ms_back;
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, ms_back.put());
            D3DSURFACE_DESC ms_desc{};
            if (ms_back) {
                ms_back->GetDesc(&ms_desc);
            }
            if (ms_desc.MultiSampleType != D3DMULTISAMPLE_4_SAMPLES) {
                problems.add("the back buffer reports multisample type " + std::to_string(ms_desc.MultiSampleType) + " after Reset to 4 samples");
            }
            managed_ok("after Reset to 4 samples");
        }
        settings.multisample = D3DMULTISAMPLE_NONE;
        pp = rig.parameters(settings);
        Kit::expect_success(problems, "Reset back to no multisampling", device->Reset(&pp));
    }
    if (SUCCEEDED(rig.d3d->CheckDeviceType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, TRUE))) {
        settings.back = D3DFMT_A8R8G8B8;
        pp = rig.parameters(settings);
        result = device->Reset(&pp);
        if (FAILED(result)) {
            problems.add("Reset to an A8R8G8B8 back buffer failed with " + Kit::hr(result));
        } else {
            Com<IDirect3DSurface9> format_back;
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, format_back.put());
            D3DSURFACE_DESC format_desc{};
            if (format_back) {
                format_back->GetDesc(&format_desc);
            }
            if (format_desc.Format != D3DFMT_A8R8G8B8) {
                problems.add("the back buffer format is " + std::to_string(format_desc.Format) + " after Reset to A8R8G8B8");
            }
        }
        settings.back = D3DFMT_X8R8G8B8;
        pp = rig.parameters(settings);
        Kit::expect_success(problems, "Reset back to X8R8G8B8", device->Reset(&pp));
    }
    managed_ok("after the last Reset");
    D3d9Tools::clear(device, 0xFF203040u);
    Kit::expect_success(problems, "Present after Reset", device->Present(nullptr, nullptr, nullptr, nullptr));
    Kit::check_usable(problems, "after Reset", device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome LostChecks::ex_never_lost(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.make_window(settings, error)) {
        return Outcome::fail(error);
    }
    const HRESULT created = rig.try_create(settings);
    if (FAILED(created)) {
        return Outcome::fail("CreateDeviceEx failed with " + Kit::hr(created));
    }
    IDirect3DDevice9Ex* device = rig.device_ex.get();
    D3DADAPTER_IDENTIFIER9 identifier{};
    rig.d3d->GetAdapterIdentifier(rig.ordinal, 0, &identifier);
    const std::wstring name = Text::wide(identifier.DeviceName);
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (!::EnumDisplaySettingsW(name.c_str(), ENUM_CURRENT_SETTINGS, &current)) {
        return Context::skip("the current display mode of " + std::string(identifier.DeviceName) + " cannot be read");
    }
    DEVMODEW other{};
    bool found = false;
    for (DWORD i = 0;; ++i) {
        DEVMODEW candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!::EnumDisplaySettingsW(name.c_str(), i, &candidate)) {
            break;
        }
        if (candidate.dmBitsPerPel == current.dmBitsPerPel && candidate.dmPelsWidth != current.dmPelsWidth && candidate.dmPelsWidth >= 800 && candidate.dmDisplayFrequency == current.dmDisplayFrequency) {
            if (!found || candidate.dmPelsWidth < other.dmPelsWidth) {
                other = candidate;
                found = true;
            }
        }
    }
    if (!found) {
        return Context::skip("the output offers no second display mode of the same depth and refresh rate");
    }
    Problems problems;
    int presents = 0;
    auto exercise = [&](const std::string& stage) {
        for (int i = 0; i < 15; ++i) {
            D3d9Tools::clear(rig.dev(), Kit::frame_color(i));
            const HRESULT present = device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
            if (!presentable(present)) {
                problems.add(stage + ": PresentEx returned " + Kit::hr(present));
                return;
            }
            const HRESULT state = device->CheckDeviceState(rig.window.hwnd());
            if (!presentable(state)) {
                problems.add(stage + ": CheckDeviceState returned " + Kit::hr(state));
                return;
            }
            const HRESULT level = device->TestCooperativeLevel();
            if (level == D3DERR_DEVICELOST || level == D3DERR_DEVICENOTRESET) {
                problems.add(stage + ": TestCooperativeLevel returned " + Kit::hr(level));
                return;
            }
            ++presents;
            ::Sleep(60);
        }
    };
    exercise("before the mode switch");
    {
        ModeGuard guard(name, current);
        const LONG change = ::ChangeDisplaySettingsExW(name.c_str(), &other, nullptr, 0, nullptr);
        if (change != DISP_CHANGE_SUCCESSFUL) {
            return Context::skip("the display mode cannot be switched (code " + std::to_string(change) + ")");
        }
        guard.changed();
        context.measure("switched_to", std::to_string(other.dmPelsWidth) + "x" + std::to_string(other.dmPelsHeight));
        ::Sleep(1500);
        exercise("after switching to " + std::to_string(other.dmPelsWidth) + "x" + std::to_string(other.dmPelsHeight));
        guard.restore();
        ::Sleep(1500);
    }
    DEVMODEW after{};
    after.dmSize = sizeof(after);
    ::EnumDisplaySettingsW(name.c_str(), ENUM_CURRENT_SETTINGS, &after);
    if (after.dmPelsWidth != current.dmPelsWidth || after.dmPelsHeight != current.dmPelsHeight) {
        problems.add("the display mode was not restored: " + std::to_string(after.dmPelsWidth) + "x" + std::to_string(after.dmPelsHeight));
    }
    exercise("after restoring the mode");
    Kit::check_usable(problems, "after the mode switches", rig.dev());
    context.measure("presents", presents);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
