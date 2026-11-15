#include "support/dxgi_budget.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"

#include <dxgi1_4.h>

namespace lsw::suites {

namespace {

IDXGIAdapter3* open3(const LUID& luid) {
    IUnknown* adapter = AvAdapters::open_adapter(luid);
    if (adapter == nullptr) {
        return nullptr;
    }
    IDXGIAdapter3* adapter3 = nullptr;
    const HRESULT result = adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3));
    adapter->Release();
    return SUCCEEDED(result) ? adapter3 : nullptr;
}

}  // namespace

bool DxgiBudget::query(const LUID& luid, bool local, VideoMemory& out, std::string& error) {
    IDXGIAdapter3* adapter = open3(luid);
    if (adapter == nullptr) {
        error = "IDXGIAdapter3 is not available for the adapter";
        return false;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    const HRESULT result = adapter->QueryVideoMemoryInfo(
        0, local ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info);
    adapter->Release();
    if (FAILED(result)) {
        error = "QueryVideoMemoryInfo failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return false;
    }
    out.budget = info.Budget;
    out.usage = info.CurrentUsage;
    out.available_for_reservation = info.AvailableForReservation;
    out.reservation = info.CurrentReservation;
    return true;
}

long DxgiBudget::set_reservation(const LUID& luid, bool local, std::uint64_t bytes) {
    IDXGIAdapter3* adapter = open3(luid);
    if (adapter == nullptr) {
        return static_cast<long>(E_NOINTERFACE);
    }
    const HRESULT result = adapter->SetVideoMemoryReservation(
        0, local ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, bytes);
    adapter->Release();
    return static_cast<long>(result);
}

}  // namespace lsw::suites
