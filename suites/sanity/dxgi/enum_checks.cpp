#include "sanity/dxgi/enum_checks.hpp"

#include "core/text.hpp"
#include "support/com_ptr.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_device.hpp"
#include "support/dxgi_budget.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"
#include "support/vulkan_api.hpp"

#include <d3d9.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace lsw::suites::sanity::dxgi {

namespace {

struct Entry {
    std::uint64_t key = 0;
    UINT vendor = 0;
    UINT device = 0;
    std::wstring name;
    bool software = false;
};

Entry entry_of(const DXGI_ADAPTER_DESC1& d) {
    Entry e;
    e.key = AvAdapters::key(d.AdapterLuid);
    e.vendor = d.VendorId;
    e.device = d.DeviceId;
    e.name.assign(d.Description);
    e.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    return e;
}

std::string key_text(std::uint64_t key) {
    return Text::hex(key >> 32, 8) + ":" + Text::hex(key & 0xFFFFFFFFu, 8);
}

template <typename Getter>
bool collect(const char* label, Getter get, std::map<std::uint64_t, Entry>& out, Problems& problems) {
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> adapter;
        const HRESULT result = get(i, adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            Com<IDXGIAdapter1> extra;
            if (get(i, extra) != DXGI_ERROR_NOT_FOUND) {
                problems.add(std::string(label) + ": enumeration past the end does not keep returning NOT_FOUND");
            }
            return true;
        }
        if (FAILED(result)) {
            problems.add(std::string(label) + " index " + std::to_string(i) + " failed: " + D3d11Tools::hr(result));
            return false;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        out[entry_of(desc).key] = entry_of(desc);
        if (i > 64) {
            problems.add(std::string(label) + " never ends");
            return false;
        }
    }
}

}  // namespace

Outcome EnumChecks::adapters_consistent(Context& context) {
    Com<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()))) {
        return Outcome::fail("CreateDXGIFactory1 failed");
    }
    Problems problems;
    std::map<std::uint64_t, Entry> plain;
    std::map<std::uint64_t, Entry> one;
    std::map<std::uint64_t, Entry> by_luid;
    collect("EnumAdapters", [&](UINT i, Com<IDXGIAdapter1>& out) {
        Com<IDXGIAdapter> a;
        const HRESULT r = factory->EnumAdapters(i, a.put());
        if (SUCCEEDED(r)) {
            a->QueryInterface(__uuidof(IDXGIAdapter1), out.put_void());
        }
        return r;
    }, plain, problems);
    collect("EnumAdapters1", [&](UINT i, Com<IDXGIAdapter1>& out) { return factory->EnumAdapters1(i, out.put()); }, one, problems);
    Com<IDXGIFactory4> factory4 = factory.as<IDXGIFactory4>();
    if (factory4) {
        for (const auto& [key, e] : one) {
            LUID luid{static_cast<DWORD>(key & 0xFFFFFFFFu), static_cast<LONG>(key >> 32)};
            Com<IDXGIAdapter1> adapter;
            const HRESULT r = factory4->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), adapter.put_void());
            if (FAILED(r)) {
                problems.add("EnumAdapterByLuid failed for " + key_text(key) + ": " + D3d11Tools::hr(r));
                continue;
            }
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            by_luid[key] = entry_of(desc);
        }
        LUID bogus{0x12345678, 0x7777};
        Com<IDXGIAdapter1> none;
        if (SUCCEEDED(factory4->EnumAdapterByLuid(bogus, __uuidof(IDXGIAdapter1), none.put_void()))) {
            problems.add("EnumAdapterByLuid accepts a LUID that belongs to no adapter");
        }
    } else {
        context.note("IDXGIFactory4 is not available; EnumAdapterByLuid is not tested");
    }
    Com<IDXGIFactory6> factory6 = factory.as<IDXGIFactory6>();
    std::map<std::string, std::map<std::uint64_t, Entry>> preferences;
    if (factory6) {
        const std::pair<const char*, DXGI_GPU_PREFERENCE> kinds[] = {{"unspecified", DXGI_GPU_PREFERENCE_UNSPECIFIED}, {"minimum power", DXGI_GPU_PREFERENCE_MINIMUM_POWER}, {"high performance", DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE}};
        for (const auto& [name, kind] : kinds) {
            collect(name, [&](UINT i, Com<IDXGIAdapter1>& out) { return factory6->EnumAdapterByGpuPreference(i, kind, __uuidof(IDXGIAdapter1), out.put_void()); }, preferences[name], problems);
        }
    } else {
        context.note("IDXGIFactory6 is not available; EnumAdapterByGpuPreference is not tested");
    }
    auto hardware = [](const std::map<std::uint64_t, Entry>& all) {
        std::set<std::uint64_t> keys;
        for (const auto& [key, e] : all) {
            if (!e.software) {
                keys.insert(key);
            }
        }
        return keys;
    };
    auto same_keys = [&](const char* label, const std::map<std::uint64_t, Entry>& other, bool include_software) {
        const std::set<std::uint64_t> a = include_software ? [&] { std::set<std::uint64_t> k; for (auto& p : one) k.insert(p.first); return k; }() : hardware(one);
        const std::set<std::uint64_t> b = include_software ? [&] { std::set<std::uint64_t> k; for (auto& p : other) k.insert(p.first); return k; }() : hardware(other);
        if (a != b) {
            problems.add(std::string(label) + " returns a different adapter set than EnumAdapters1");
        }
        for (const auto& [key, e] : other) {
            auto base = one.find(key);
            if (base != one.end() && (base->second.vendor != e.vendor || base->second.device != e.device || base->second.name != e.name)) {
                problems.add(std::string(label) + " describes adapter " + key_text(key) + " differently than EnumAdapters1");
            }
        }
    };
    same_keys("EnumAdapters", plain, true);
    if (factory4) {
        same_keys("EnumAdapterByLuid", by_luid, true);
    }
    for (const auto& [name, set] : preferences) {
        same_keys(("EnumAdapterByGpuPreference(" + name + ")").c_str(), set, false);
    }
    if (one.empty()) {
        problems.add("no adapter is enumerated");
    }
    context.measure("adapters", static_cast<long long>(one.size()));
    return problems.outcome();
}

Outcome EnumChecks::factory_lifecycle(Context& context) {
    Problems problems;
    using CreateFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    DynLibrary library;
    CreateFactory2 create = library.load("dxgi.dll") ? library.symbol<CreateFactory2>("CreateDXGIFactory2") : nullptr;
    if (create == nullptr) {
        return Context::skip("dxgi.dll has no CreateDXGIFactory2, which the API reports as an older DXGI runtime");
    }
    Com<IDXGIFactory2> plain;
    Com<IDXGIFactory2> debug;
    const HRESULT a = create(0, __uuidof(IDXGIFactory2), plain.put_void());
    const HRESULT b = create(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory2), debug.put_void());
    if (FAILED(a)) {
        return Outcome::fail("CreateDXGIFactory2(0) failed: " + D3d11Tools::hr(a));
    }
    if (FAILED(b)) {
        DynLibrary debug_library;
        if (debug_library.load("dxgidebug.dll")) {
            problems.add("CreateDXGIFactory2 with the debug flag failed although dxgidebug.dll is installed: " + D3d11Tools::hr(b));
        } else {
            context.note("CreateDXGIFactory2 with the debug flag fails with " + D3d11Tools::hr(b) + " because dxgidebug.dll is not installed");
        }
    }
    for (IDXGIFactory2* factory : {plain.get(), debug.get()}) {
        if (factory == nullptr) {
            continue;
        }
        if (factory->IsCurrent() != TRUE) {
            problems.add("a freshly created factory is not current");
        }
        Com<IDXGIAdapter1> adapter;
        Com<IDXGIFactory1> factory1;
        factory->QueryInterface(__uuidof(IDXGIFactory1), factory1.put_void());
        if (!factory1 || FAILED(factory1->EnumAdapters1(0, adapter.put())) || !adapter) {
            problems.add("a factory cannot enumerate an adapter");
        }
    }
    AvAdapter target;
    std::string error;
    if (plain && TargetAdapter::pick(target, error)) {
        D3d11Device device;
        if (device.create(target.luid, error)) {
            Com<IDXGIFactory2> second;
            create(0, __uuidof(IDXGIFactory2), second.put_void());
            if (plain->IsCurrent() != TRUE) {
                problems.add("the first factory stops being current after a device and a second factory are created");
            }
            if (second && second->IsCurrent() != TRUE) {
                problems.add("the second factory is not current");
            }
        }
    }
    if (plain) {
        TestWindow window;
        if (window.create({}, error)) {
            const UINT flags = DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES;
            HRESULT r = plain->MakeWindowAssociation(window.hwnd(), flags);
            HWND associated = nullptr;
            const HRESULT got = plain->GetWindowAssociation(&associated);
            context.measure("window_association.make_result", D3d11Tools::hr(r));
            context.measure("window_association.get_result", D3d11Tools::hr(got));
            context.measure("window_association.round_trips", SUCCEEDED(r) && SUCCEEDED(got) && associated == window.hwnd());
            r = plain->MakeWindowAssociation(window.hwnd(), 0);
            if (FAILED(r)) {
                problems.add("MakeWindowAssociation with no flags failed: " + D3d11Tools::hr(r));
            }
            r = plain->MakeWindowAssociation(window.hwnd(), 0xFFFF);
            if (SUCCEEDED(r)) {
                problems.add("MakeWindowAssociation accepts undefined flag bits");
            }
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome EnumChecks::adapter_desc_matches_apis(Context& context) {
    AvAdapter target;
    std::string error;
    if (!TargetAdapter::pick(target, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDXGIFactory1> factory;
    ::CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void());
    Com<IDXGIAdapter1> adapter;
    IUnknown* opened = AvAdapters::open_adapter(target.luid);
    if (opened == nullptr) {
        return Outcome::fail("the adapter cannot be opened");
    }
    adapter.reset(static_cast<IDXGIAdapter1*>(opened));
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.DedicatedVideoMemory + desc.SharedSystemMemory + desc.DedicatedSystemMemory == 0) {
        problems.add("every memory field of DXGI_ADAPTER_DESC is zero");
    }
    VideoMemory local;
    if (DxgiBudget::query(target.luid, true, local, error)) {
        if (local.budget == 0) {
            problems.add("QueryVideoMemoryInfo reports a zero budget");
        }
        const double reference = desc.DedicatedVideoMemory != 0 ? static_cast<double>(desc.DedicatedVideoMemory) : static_cast<double>(desc.SharedSystemMemory);
        const double ratio = static_cast<double>(local.budget) / std::max(reference, 1.0);
        context.measure("budget_to_desc_ratio", ratio);
        if (ratio < 1.0 / 16.0 || ratio > 16.0) {
            problems.add("the local memory budget " + std::to_string(local.budget >> 20) + " MiB is not within a factor of 16 of the adapter description " + std::to_string(static_cast<std::uint64_t>(reference) >> 20) + " MiB");
        }
    } else {
        problems.add("QueryVideoMemoryInfo: " + error);
    }
    const std::uint64_t key = AvAdapters::key(target.luid);
    D3d11Device device;
    if (device.create(target.luid, error)) {
        Com<IDXGIDevice> dxgi_device;
        device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
        Com<IDXGIAdapter> from_device;
        DXGI_ADAPTER_DESC d{};
        if (dxgi_device && SUCCEEDED(dxgi_device->GetAdapter(from_device.put())) && SUCCEEDED(from_device->GetDesc(&d))) {
            if (AvAdapters::key(d.AdapterLuid) != key) {
                problems.add("the adapter behind a D3D11 device has a different LUID");
            }
        } else {
            problems.add("the adapter behind a D3D11 device cannot be queried");
        }
    }
    D3d12Device device12;
    if (device12.create(target.luid, error)) {
        const LUID luid12 = device12.device->GetAdapterLuid();
        if (AvAdapters::key(luid12) != key) {
            problems.add("ID3D12Device::GetAdapterLuid differs from the DXGI LUID");
        }
    } else {
        context.note("D3D12 is not available: " + error);
    }
    {
        DynLibrary d3d9;
        using Create9Ex = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
        Create9Ex create = d3d9.load("d3d9.dll") ? d3d9.symbol<Create9Ex>("Direct3DCreate9Ex") : nullptr;
        IDirect3D9Ex* d3d = nullptr;
        if (create != nullptr && SUCCEEDED(create(D3D_SDK_VERSION, &d3d))) {
            bool found = false;
            for (UINT i = 0; i < d3d->GetAdapterCount(); ++i) {
                LUID luid{};
                if (SUCCEEDED(d3d->GetAdapterLUID(i, &luid)) && AvAdapters::key(luid) == key) {
                    found = true;
                }
            }
            d3d->Release();
            if (!found) {
                problems.add("no Direct3D 9Ex adapter has the DXGI LUID");
            }
        } else {
            context.note("Direct3D 9Ex is not available");
        }
    }
    {
        VulkanApi api;
        std::vector<VulkanDevice> devices;
        if (api.load(error) && api.create_instance(error) && api.devices(devices, error)) {
            bool found = false;
            bool any_luid = false;
            for (const VulkanDevice& v : devices) {
                any_luid = any_luid || v.luid_valid;
                found = found || (v.luid_valid && AvAdapters::key(v.luid) == key);
            }
            if (any_luid && !found) {
                problems.add("no Vulkan physical device has the DXGI LUID");
            }
        } else {
            context.note("Vulkan is not available: " + error);
        }
    }
    context.note("OpenGL LUID identity is covered by sanity.enum.identity.gl_renderer_matches");
    return problems.outcome();
}

Outcome EnumChecks::output_modes(Context& context) {
    AvAdapter target;
    std::string error;
    if (!TargetAdapter::pick(target, error)) {
        return Outcome::fail(error);
    }
    IUnknown* opened = AvAdapters::open_adapter(target.luid);
    if (opened == nullptr) {
        return Outcome::fail("the adapter cannot be opened");
    }
    Com<IDXGIAdapter1> adapter(static_cast<IDXGIAdapter1*>(opened));
    Problems problems;
    int outputs = 0;
    for (UINT index = 0;; ++index) {
        Com<IDXGIOutput> output;
        if (adapter->EnumOutputs(index, output.put()) != S_OK) {
            break;
        }
        ++outputs;
        DXGI_OUTPUT_DESC od{};
        output->GetDesc(&od);
        const std::string name = Text::utf8(od.DeviceName);
        if (od.DesktopCoordinates.right <= od.DesktopCoordinates.left || od.DesktopCoordinates.bottom <= od.DesktopCoordinates.top) {
            problems.add(name + ": the desktop rectangle is empty");
            continue;
        }
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        const bool have_mode = ::EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &mode) != FALSE;
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
        bool current_found = false;
        for (DXGI_FORMAT format : formats) {
            UINT count = 0;
            const HRESULT counted = output->GetDisplayModeList(format, 0, &count, nullptr);
            if (counted != S_OK || count == 0) {
                continue;
            }
            std::vector<DXGI_MODE_DESC> modes(count);
            if (FAILED(output->GetDisplayModeList(format, 0, &count, modes.data()))) {
                problems.add(name + ": GetDisplayModeList(" + std::to_string(format) + ") failed on the second call");
                continue;
            }
            modes.resize(count);
            std::set<std::tuple<UINT, UINT, UINT, UINT, int, int>> unique;
            for (std::size_t i = 0; i < modes.size(); ++i) {
                const DXGI_MODE_DESC& m = modes[i];
                if (!unique.insert({m.Width, m.Height, m.RefreshRate.Numerator, m.RefreshRate.Denominator, m.ScanlineOrdering, m.Scaling}).second) {
                    problems.add(name + ": format " + std::to_string(format) + " lists a duplicate mode " + std::to_string(m.Width) + "x" + std::to_string(m.Height));
                    break;
                }
                if (i > 0 && (m.Width < modes[i - 1].Width || (m.Width == modes[i - 1].Width && m.Height < modes[i - 1].Height))) {
                    problems.add(name + ": format " + std::to_string(format) + " is not sorted by size at entry " + std::to_string(i));
                    break;
                }
                if (have_mode && m.Width == mode.dmPelsWidth && m.Height == mode.dmPelsHeight) {
                    const double refresh = m.RefreshRate.Denominator != 0 ? static_cast<double>(m.RefreshRate.Numerator) / m.RefreshRate.Denominator : 0.0;
                    if (mode.dmDisplayFrequency <= 1 || std::fabs(refresh - mode.dmDisplayFrequency) < 1.5) {
                        current_found = true;
                    }
                }
            }
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM && have_mode) {
                DXGI_MODE_DESC wanted{};
                wanted.Width = mode.dmPelsWidth;
                wanted.Height = mode.dmPelsHeight;
                wanted.Format = format;
                DXGI_MODE_DESC closest{};
                if (FAILED(output->FindClosestMatchingMode(&wanted, &closest, nullptr))) {
                    problems.add(name + ": FindClosestMatchingMode failed for the current size");
                } else {
                    bool listed = false;
                    for (const DXGI_MODE_DESC& m : modes) {
                        listed = listed || (m.Width == closest.Width && m.Height == closest.Height && m.RefreshRate.Numerator == closest.RefreshRate.Numerator &&
                                            m.RefreshRate.Denominator == closest.RefreshRate.Denominator && m.ScanlineOrdering == closest.ScanlineOrdering && m.Scaling == closest.Scaling);
                    }
                    if (!listed) {
                        problems.add(name + ": FindClosestMatchingMode returned a mode that is not in the list");
                    }
                    if (closest.Width != wanted.Width || closest.Height != wanted.Height) {
                        problems.add(name + ": FindClosestMatchingMode changed the size of an exactly listed mode");
                    }
                }
            }
        }
        if (have_mode && !current_found) {
            problems.add(name + ": the current mode " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + "@" + std::to_string(mode.dmDisplayFrequency) + " is in no mode list");
        }
        D3d11Device device;
        if (device.create(target.luid, error)) {
            const UINT width = static_cast<UINT>(od.DesktopCoordinates.right - od.DesktopCoordinates.left);
            const UINT height = static_cast<UINT>(od.DesktopCoordinates.bottom - od.DesktopCoordinates.top);
            Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
            Com<IDXGISurface> surface;
            if (staging) {
                staging->QueryInterface(__uuidof(IDXGISurface), surface.put_void());
            }
            if (surface) {
                const HRESULT result = output->GetDisplaySurfaceData(surface.get());
                context.measure("surface_data." + name, D3d11Tools::hr(result));
                if (result == S_OK) {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (FAILED(device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)) || mapped.RowPitch < width * 4) {
                        problems.add(name + ": the surface filled by GetDisplaySurfaceData cannot be read");
                    } else {
                        device.context->Unmap(staging.get(), 0);
                    }
                } else if (result != DXGI_ERROR_INVALID_CALL && result != DXGI_ERROR_ACCESS_DENIED && result != DXGI_ERROR_UNSUPPORTED && result != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE && result != E_NOTIMPL) {
                    problems.add(name + ": GetDisplaySurfaceData returned the undocumented " + D3d11Tools::hr(result));
                }
            }
        }
    }
    if (outputs == 0) {
        problems.add("the adapter has no output");
    }
    context.measure("outputs", outputs);
    return problems.outcome();
}

Outcome EnumChecks::output_desc_vs_win32(Context& context) {
    AvAdapter target;
    std::string error;
    if (!TargetAdapter::pick(target, error)) {
        return Outcome::fail(error);
    }
    IUnknown* opened = AvAdapters::open_adapter(target.luid);
    if (opened == nullptr) {
        return Outcome::fail("the adapter cannot be opened");
    }
    Com<IDXGIAdapter1> adapter(static_cast<IDXGIAdapter1*>(opened));
    Problems problems;
    int outputs = 0;
    struct AdvancedColor {
        DISPLAYCONFIG_DEVICE_INFO_HEADER header;
        UINT32 value;
        UINT32 encoding;
        UINT32 bits;
    };
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) == ERROR_SUCCESS) {
            paths.resize(path_count);
            modes.resize(mode_count);
            if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) == ERROR_SUCCESS) {
                paths.resize(path_count);
            } else {
                paths.clear();
            }
        }
    }
    for (UINT index = 0;; ++index) {
        Com<IDXGIOutput> output;
        if (adapter->EnumOutputs(index, output.put()) != S_OK) {
            break;
        }
        ++outputs;
        DXGI_OUTPUT_DESC od{};
        output->GetDesc(&od);
        const std::string name = Text::utf8(od.DeviceName);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!::GetMonitorInfoW(od.Monitor, &info)) {
            problems.add(name + ": GetMonitorInfo fails for the monitor handle of the output");
            continue;
        }
        if (info.rcMonitor.left != od.DesktopCoordinates.left || info.rcMonitor.top != od.DesktopCoordinates.top ||
            info.rcMonitor.right != od.DesktopCoordinates.right || info.rcMonitor.bottom != od.DesktopCoordinates.bottom) {
            problems.add(name + ": DesktopCoordinates differ from GetMonitorInfo");
        }
        if (std::wstring(info.szDevice) != std::wstring(od.DeviceName)) {
            problems.add(name + ": the device name differs from GetMonitorInfo");
        }
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (::EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &mode) && (mode.dmFields & DM_DISPLAYORIENTATION) != 0) {
            const DXGI_MODE_ROTATION expected[] = {DXGI_MODE_ROTATION_IDENTITY, DXGI_MODE_ROTATION_ROTATE90, DXGI_MODE_ROTATION_ROTATE180, DXGI_MODE_ROTATION_ROTATE270};
            if (mode.dmDisplayOrientation < 4 && od.Rotation != expected[mode.dmDisplayOrientation]) {
                problems.add(name + ": Rotation " + std::to_string(od.Rotation) + " differs from the display orientation " + std::to_string(mode.dmDisplayOrientation));
            }
        }
        Com<IDXGIOutput6> output6;
        output->QueryInterface(__uuidof(IDXGIOutput6), output6.put_void());
        if (!output6) {
            context.note(name + ": IDXGIOutput6 is not available, colour space and luminance are not compared");
            continue;
        }
        DXGI_OUTPUT_DESC1 d1{};
        if (FAILED(output6->GetDesc1(&d1))) {
            problems.add(name + ": GetDesc1 failed");
            continue;
        }
        if (d1.MaxLuminance < d1.MinLuminance || d1.MaxFullFrameLuminance > d1.MaxLuminance + 0.5f || d1.MinLuminance < 0.0f || d1.MaxLuminance <= 0.0f) {
            problems.add(name + ": the luminance fields are inconsistent: min " + std::to_string(d1.MinLuminance) + ", max " + std::to_string(d1.MaxLuminance) + ", full frame " + std::to_string(d1.MaxFullFrameLuminance));
        }
        if (d1.BitsPerColor < 6 || d1.BitsPerColor > 16) {
            problems.add(name + ": BitsPerColor " + std::to_string(d1.BitsPerColor) + " is implausible");
        }
        bool known = false;
        bool enabled = false;
        for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            if (::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || std::wstring(source.viewGdiDeviceName) != std::wstring(od.DeviceName)) {
                continue;
            }
            AdvancedColor color{};
            color.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(9);
            color.header.size = sizeof(color);
            color.header.adapterId = path.targetInfo.adapterId;
            color.header.id = path.targetInfo.id;
            if (::DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
                known = true;
                enabled = (color.value & 2) != 0;
            }
        }
        if (!known) {
            context.note(name + ": the advanced colour state cannot be queried");
            continue;
        }
        const bool hdr = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        if (enabled && !hdr) {
            problems.add(name + ": advanced colour is enabled but GetDesc1 reports colour space " + std::to_string(d1.ColorSpace));
        }
        if (!enabled && d1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
            problems.add(name + ": advanced colour is off but GetDesc1 reports colour space " + std::to_string(d1.ColorSpace));
        }
    }
    if (outputs == 0) {
        problems.add("the adapter has no output");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::dxgi
