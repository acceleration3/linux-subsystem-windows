#include "support/d3d12_device.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/dyn_library.hpp"

namespace lsw::suites {

D3d12Device::~D3d12Device() {
    if (device != nullptr) {
        device->Release();
    }
}

bool D3d12Device::create(const LUID& luid, std::string& error) {
    static DynLibrary library;
    static std::mutex load_mutex;
    PFN_D3D12_CREATE_DEVICE create_device = nullptr;
    {
        std::lock_guard lock(load_mutex);
        if (!library.loaded() && !library.load("d3d12.dll")) {
            error = "d3d12.dll cannot be loaded";
            return false;
        }
        create_device = library.symbol<PFN_D3D12_CREATE_DEVICE>("D3D12CreateDevice");
    }
    if (create_device == nullptr) {
        error = "d3d12.dll has no D3D12CreateDevice";
        return false;
    }
    IUnknown* adapter = AvAdapters::open_adapter(luid);
    if (adapter == nullptr) {
        error = "the adapter cannot be opened by LUID for D3D12";
        return false;
    }
    const HRESULT result = create_device(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                         reinterpret_cast<void**>(&device));
    adapter->Release();
    if (FAILED(result)) {
        error = "D3D12CreateDevice failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        device = nullptr;
        return false;
    }
    return true;
}

ID3D12CommandQueue* D3d12Device::make_queue(std::string& error) {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    const HRESULT result = device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
    if (FAILED(result)) {
        error = "CreateCommandQueue failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return nullptr;
    }
    return queue;
}

}  // namespace lsw::suites
