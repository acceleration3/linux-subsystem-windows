#include "support/d3d11_device.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/dyn_library.hpp"

#include <chrono>

namespace lsw::suites {

D3d11Device::~D3d11Device() {
    if (context != nullptr) {
        context->ClearState();
        context->Flush();
        context->Release();
    }
    if (device != nullptr) {
        device->Release();
    }
}

bool D3d11Device::create(const LUID& luid, std::string& error) {
    static DynLibrary library;
    static std::mutex load_mutex;
    PFN_D3D11_CREATE_DEVICE create_device = nullptr;
    {
        std::lock_guard lock(load_mutex);
        if (!library.loaded() && !library.load("d3d11.dll")) {
            error = "d3d11.dll cannot be loaded";
            return false;
        }
        create_device = library.symbol<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
    }
    if (create_device == nullptr) {
        error = "d3d11.dll has no D3D11CreateDevice";
        return false;
    }
    IUnknown* adapter = AvAdapters::open_adapter(luid);
    if (adapter == nullptr) {
        error = "the adapter cannot be opened by LUID for D3D11";
        return false;
    }
    const HRESULT result = create_device(static_cast<IDXGIAdapter*>(adapter), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                         nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    adapter->Release();
    if (FAILED(result)) {
        error = "D3D11CreateDevice failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device = nullptr;
        context = nullptr;
        return false;
    }
    return true;
}

bool D3d11Device::wait_idle(std::string& error) {
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    ID3D11Query* query = nullptr;
    if (FAILED(device->CreateQuery(&desc, &query))) {
        error = "CreateQuery failed";
        return false;
    }
    context->End(query);
    context->Flush();
    BOOL done = FALSE;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    HRESULT result = S_FALSE;
    while ((result = context->GetData(query, &done, sizeof(done), 0)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            query->Release();
            error = "the D3D11 event query did not complete in 60 s";
            return false;
        }
        ::SwitchToThread();
    }
    query->Release();
    if (FAILED(result)) {
        error = "GetData failed";
        return false;
    }
    return true;
}

}  // namespace lsw::suites
