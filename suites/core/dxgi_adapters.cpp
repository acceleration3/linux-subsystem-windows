#include "core/dxgi_adapters.hpp"

#include "core/text.hpp"

#include <dxgi.h>
#include <windows.h>

namespace lsw::suites {

std::string DxgiAdapter::luid() const {
    return Text::hex(static_cast<std::uint32_t>(luid_high), 8) + ":" + Text::hex(luid_low, 8);
}

bool DxgiAdapters::enumerate(std::vector<DxgiAdapter>& out, std::string& error) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT created = ::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                                 reinterpret_cast<void**>(&factory));
    if (FAILED(created)) {
        error = "CreateDXGIFactory1 failed: 0x" +
                Text::hex(static_cast<std::uint32_t>(created), 8);
        return false;
    }
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) != S_OK) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            DxgiAdapter entry;
            entry.name = Text::utf8(desc.Description);
            entry.luid_low = desc.AdapterLuid.LowPart;
            entry.luid_high = desc.AdapterLuid.HighPart;
            entry.vendor = desc.VendorId;
            entry.device = desc.DeviceId;
            entry.subsystem = desc.SubSysId;
            out.push_back(std::move(entry));
        }
        adapter->Release();
    }
    factory->Release();
    return true;
}

}  // namespace lsw::suites
