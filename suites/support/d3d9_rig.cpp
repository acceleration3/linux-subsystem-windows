#include "support/d3d9_rig.hpp"

#include "core/text.hpp"
#include "support/dyn_library.hpp"
#include "support/target_adapter.hpp"

namespace lsw::suites {

namespace {

using CreateEx = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);

CreateEx create_ex_function() {
    static DynLibrary library;
    static CreateEx function = [] {
        if (!library.load("d3d9.dll")) {
            return static_cast<CreateEx>(nullptr);
        }
        return library.symbol<CreateEx>("Direct3DCreate9Ex");
    }();
    return function;
}

}  // namespace

bool D3d9Rig::create_ex_available() {
    return create_ex_function() != nullptr;
}

bool D3d9Rig::open(bool use_ex, std::string& error) {
    ex = use_ex;
    if (!TargetAdapter::pick(adapter, error)) {
        return false;
    }
    CreateEx factory = create_ex_function();
    if (factory == nullptr) {
        error = "d3d9.dll has no Direct3DCreate9Ex";
        return false;
    }
    Com<IDirect3D9Ex> found;
    if (FAILED(factory(D3D_SDK_VERSION, found.put()))) {
        error = "Direct3DCreate9Ex failed";
        return false;
    }
    bool matched = false;
    for (UINT i = 0; i < found->GetAdapterCount(); ++i) {
        LUID luid{};
        if (SUCCEEDED(found->GetAdapterLUID(i, &luid)) && AvAdapters::key(luid) == AvAdapters::key(adapter.luid)) {
            ordinal = i;
            matched = true;
            break;
        }
    }
    if (!matched) {
        error = "no Direct3D 9 adapter carries the LUID " + adapter.luid_text();
        return false;
    }
    if (use_ex) {
        d3d_ex = std::move(found);
        d3d = d3d_ex.as<IDirect3D9>();
        return true;
    }
    d3d.reset(Direct3DCreate9(D3D_SDK_VERSION));
    if (!d3d) {
        error = "Direct3DCreate9 failed";
        return false;
    }
    return true;
}

DWORD D3d9Rig::auto_behavior() const {
    D3DCAPS9 data{};
    if (SUCCEEDED(d3d->GetDeviceCaps(ordinal, D3DDEVTYPE_HAL, &data)) && (data.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0) {
        return D3DCREATE_HARDWARE_VERTEXPROCESSING;
    }
    return D3DCREATE_SOFTWARE_VERTEXPROCESSING;
}

bool D3d9Rig::caps(D3DCAPS9& out) const {
    return SUCCEEDED(d3d->GetDeviceCaps(ordinal, D3DDEVTYPE_HAL, &out));
}

RECT D3d9Rig::monitor_rect() const {
    RECT rect{0, 0, 1024, 768};
    HMONITOR monitor = d3d->GetAdapterMonitor(ordinal);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &info)) {
        rect = info.rcMonitor;
    }
    return rect;
}

POINT D3d9Rig::client_origin() const {
    POINT point{0, 0};
    ::ClientToScreen(window.hwnd(), &point);
    return point;
}

bool D3d9Rig::make_window(const D3d9Settings& wanted, std::string& error) {
    const RECT monitor = monitor_rect();
    RECT frame{0, 0, static_cast<LONG>(wanted.width), static_cast<LONG>(wanted.height)};
    ::AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    TestWindowConfig config;
    config.x = static_cast<int>(monitor.left) + wanted.x;
    config.y = static_cast<int>(monitor.top) + wanted.y;
    config.width = static_cast<int>(frame.right - frame.left);
    config.height = static_cast<int>(frame.bottom - frame.top);
    config.visible = wanted.visible;
    if (!window.create(config, error)) {
        return false;
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

D3DPRESENT_PARAMETERS D3d9Rig::parameters(const D3d9Settings& wanted) const {
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = wanted.width;
    pp.BackBufferHeight = wanted.height;
    pp.BackBufferFormat = wanted.back;
    pp.BackBufferCount = wanted.buffers;
    pp.MultiSampleType = wanted.multisample;
    pp.SwapEffect = wanted.effect;
    pp.hDeviceWindow = window.hwnd();
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = wanted.depth ? TRUE : FALSE;
    pp.AutoDepthStencilFormat = wanted.depth_format;
    pp.PresentationInterval = wanted.interval;
    return pp;
}

HRESULT D3d9Rig::try_create(const D3d9Settings& wanted) {
    close_device();
    settings = wanted;
    D3DPRESENT_PARAMETERS pp = parameters(wanted);
    const DWORD behavior = (wanted.behavior != 0 ? wanted.behavior : auto_behavior()) | wanted.extra_behavior;
    if (ex) {
        const HRESULT result = d3d_ex->CreateDeviceEx(ordinal, D3DDEVTYPE_HAL, window.hwnd(), behavior, &pp, nullptr, device_ex.put());
        if (SUCCEEDED(result)) {
            device = device_ex.as<IDirect3DDevice9>();
        }
        return result;
    }
    return d3d->CreateDevice(ordinal, D3DDEVTYPE_HAL, window.hwnd(), behavior, &pp, device.put());
}

bool D3d9Rig::create(const D3d9Settings& wanted, std::string& error) {
    if (window.hwnd() == nullptr && !make_window(wanted, error)) {
        return false;
    }
    const HRESULT result = try_create(wanted);
    if (FAILED(result)) {
        error = std::string(ex ? "CreateDeviceEx" : "CreateDevice") + " failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return false;
    }
    return true;
}

void D3d9Rig::close_device() {
    device.reset();
    device_ex.reset();
}

}  // namespace lsw::suites
