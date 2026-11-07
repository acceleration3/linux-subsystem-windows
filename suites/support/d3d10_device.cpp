#include "support/d3d10_device.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/dyn_library.hpp"

#include <chrono>
#include <mutex>

namespace lsw::suites {

namespace {

using CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device**);
using CreateDevice1 = HRESULT(WINAPI*)(IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT, ID3D10Device1**);

CreateDevice loader() {
    static DynLibrary library;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (!library.loaded() && !library.load("d3d10.dll")) {
        return nullptr;
    }
    return library.symbol<CreateDevice>("D3D10CreateDevice");
}

CreateDevice1 loader1() {
    static DynLibrary library;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (!library.loaded() && !library.load("d3d10_1.dll")) {
        return nullptr;
    }
    return library.symbol<CreateDevice1>("D3D10CreateDevice1");
}

}  // namespace

D3d10Device::~D3d10Device() {
    destroy();
}

void D3d10Device::destroy() {
    if (device != nullptr) {
        device->ClearState();
        device->Flush();
        device->Release();
        device = nullptr;
    }
    if (device1 != nullptr) {
        device1->Release();
        device1 = nullptr;
    }
}

bool D3d10Device::library_available(bool with_1) {
    return with_1 ? loader1() != nullptr : loader() != nullptr;
}

HRESULT D3d10Device::raw_create(IDXGIAdapter* adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags, ID3D10Device** out) {
    CreateDevice create_device = loader();
    if (create_device == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }
    return create_device(adapter, type, software, flags, D3D10_SDK_VERSION, out);
}

HRESULT D3d10Device::raw_create1(IDXGIAdapter* adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags, D3D10_FEATURE_LEVEL1 level, ID3D10Device1** out) {
    CreateDevice1 create_device = loader1();
    if (create_device == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }
    return create_device(adapter, type, software, flags, level, D3D10_1_SDK_VERSION, out);
}

bool D3d10Device::create(const LUID& luid, UINT flags, std::string& error) {
    IUnknown* adapter = AvAdapters::open_adapter(luid);
    if (adapter == nullptr) {
        error = "the adapter cannot be opened by LUID for D3D10";
        return false;
    }
    result = raw_create(static_cast<IDXGIAdapter*>(adapter), D3D10_DRIVER_TYPE_HARDWARE, nullptr, flags, &device);
    adapter->Release();
    if (FAILED(result)) {
        error = "D3D10CreateDevice failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device = nullptr;
        return false;
    }
    return true;
}

bool D3d10Device::create1(const LUID& luid, UINT flags, D3D10_FEATURE_LEVEL1 level, std::string& error) {
    IUnknown* adapter = AvAdapters::open_adapter(luid);
    if (adapter == nullptr) {
        error = "the adapter cannot be opened by LUID for D3D10.1";
        return false;
    }
    result = raw_create1(static_cast<IDXGIAdapter*>(adapter), D3D10_DRIVER_TYPE_HARDWARE, nullptr, flags, level, &device1);
    adapter->Release();
    if (FAILED(result)) {
        error = "D3D10CreateDevice1 failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device1 = nullptr;
        return false;
    }
    device = device1;
    device->AddRef();
    return true;
}

bool D3d10Device::create_warp(UINT flags, std::string& error) {
    result = raw_create(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, flags, &device);
    if (FAILED(result)) {
        error = "D3D10CreateDevice(WARP) failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device = nullptr;
        return false;
    }
    return true;
}

bool D3d10Device::create_warp1(UINT flags, D3D10_FEATURE_LEVEL1 level, std::string& error) {
    result = raw_create1(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, flags, level, &device1);
    if (FAILED(result)) {
        error = "D3D10CreateDevice1(WARP) failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device1 = nullptr;
        return false;
    }
    device = device1;
    device->AddRef();
    return true;
}

bool D3d10Device::wait_idle(std::string& error) {
    D3D10_QUERY_DESC desc{};
    desc.Query = D3D10_QUERY_EVENT;
    ID3D10Query* query = nullptr;
    if (FAILED(device->CreateQuery(&desc, &query))) {
        error = "CreateQuery(EVENT) failed";
        return false;
    }
    query->End();
    device->Flush();
    BOOL done = FALSE;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    HRESULT status = S_FALSE;
    while ((status = query->GetData(&done, sizeof(done), 0)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            query->Release();
            error = "the D3D10 event query did not complete in 60 s";
            return false;
        }
        ::SwitchToThread();
    }
    query->Release();
    if (FAILED(status)) {
        error = "GetData(EVENT) failed";
        return false;
    }
    return true;
}

}  // namespace lsw::suites
