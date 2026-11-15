#include "support/av_adapters.hpp"

#include "core/text.hpp"

#include "support/kmt_api.hpp"

#include <dxgi.h>

namespace lsw::suites {

std::string AvAdapter::luid_text() const {
    return Text::hex(static_cast<std::uint32_t>(luid.HighPart), 8) + ":" + Text::hex(luid.LowPart, 8);
}

std::uint64_t AvAdapter::luid_key() const {
    return AvAdapters::key(luid);
}

std::uint64_t AvAdapters::key(const LUID& luid) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32) | luid.LowPart;
}

bool AvAdapters::all(std::vector<AvAdapter>& out, std::string& error) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT created =
        ::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(created)) {
        error = "CreateDXGIFactory1 failed: 0x" + Text::hex(static_cast<std::uint32_t>(created), 8);
        return false;
    }
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT listed = factory->EnumAdapters1(index, &adapter);
        if (listed == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(listed)) {
            error = "EnumAdapters1(" + std::to_string(index) + ") failed: 0x" +
                    Text::hex(static_cast<std::uint32_t>(listed), 8);
            factory->Release();
            return false;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) {
            error = "GetDesc1 failed for adapter " + std::to_string(index);
            adapter->Release();
            factory->Release();
            return false;
        }
        AvAdapter entry;
        entry.raw_name.assign(desc.Description, 128);
        entry.name = Text::utf8(desc.Description);
        entry.luid = desc.AdapterLuid;
        entry.vendor = desc.VendorId;
        entry.device = desc.DeviceId;
        entry.subsystem = desc.SubSysId;
        entry.revision = desc.Revision;
        entry.dedicated_video = desc.DedicatedVideoMemory;
        entry.dedicated_system = desc.DedicatedSystemMemory;
        entry.shared_system = desc.SharedSystemMemory;
        entry.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        LARGE_INTEGER version{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
            entry.umd_version_known = true;
            entry.umd_version = static_cast<std::uint64_t>(version.QuadPart);
        }
        for (UINT o = 0;; ++o) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(o, &output) != S_OK) {
                break;
            }
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(output->GetDesc(&od))) {
                AvOutput item;
                item.device_name = Text::utf8(od.DeviceName);
                item.rect = od.DesktopCoordinates;
                item.attached = od.AttachedToDesktop != FALSE;
                item.monitor = od.Monitor;
                entry.outputs.push_back(std::move(item));
            }
            output->Release();
        }
        out.push_back(std::move(entry));
        adapter->Release();
    }
    factory->Release();
    KmtApi kmt;
    std::string ignored;
    if (kmt.load(ignored)) {
        constexpr std::uint32_t kHybridDiscrete = 0x10;
        constexpr std::uint32_t kHybridIntegrated = 0x20;
        for (AvAdapter& adapter : out) {
            std::uint32_t flags = 0;
            if (kmt.adapter_type(adapter.luid, flags)) {
                adapter.hybrid_discrete = (flags & kHybridDiscrete) != 0;
                adapter.hybrid_integrated = (flags & kHybridIntegrated) != 0;
            }
        }
    }
    return true;
}

bool AvAdapters::accel(std::vector<AvAdapter>& out, std::string& error) {
    std::vector<AvAdapter> all_adapters;
    if (!all(all_adapters, error)) {
        return false;
    }
    for (AvAdapter& adapter : all_adapters) {
        if (adapter.accel()) {
            out.push_back(std::move(adapter));
        }
    }
    if (out.empty()) {
        error = "no accel-virt adapter (SUBSYS_52561AF4) in DXGI";
        return false;
    }
    return true;
}

const AvAdapter* AvAdapters::find(const std::vector<AvAdapter>& adapters, std::uint64_t luid_key) {
    for (const AvAdapter& adapter : adapters) {
        if (adapter.luid_key() == luid_key) {
            return &adapter;
        }
    }
    return nullptr;
}

IUnknown* AvAdapters::open_adapter(const LUID& luid) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return nullptr;
    }
    IDXGIAdapter1* found = nullptr;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) != S_OK) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && key(desc.AdapterLuid) == key(luid)) {
            found = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();
    return found;
}

}  // namespace lsw::suites
