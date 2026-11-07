#include "support/d3d12_rig.hpp"

#include "core/text.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsw::suites {

namespace {

std::string hr_text(HRESULT result) {
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

}  // namespace

D3d12Rig::~D3d12Rig() {
    shutdown();
}

std::vector<AvAdapter> D3d12Rig::target_adapters(std::string& error) {
    std::vector<AvAdapter> out;
    if (std::getenv("LSW_SUITES_DEV_ANY") != nullptr) {
        AvAdapter one;
        if (TargetAdapter::pick(one, error)) {
            out.push_back(std::move(one));
        }
        return out;
    }
    if (!AvAdapters::accel(out, error)) {
        out.clear();
    }
    return out;
}

bool D3d12Rig::open(const AvAdapter& target, D3D_FEATURE_LEVEL wanted, std::string& error) {
    name = target.name;
    return open_luid(target.luid, wanted, error);
}

bool D3d12Rig::open_luid(const LUID& wanted_luid, D3D_FEATURE_LEVEL wanted, std::string& error) {
    luid = wanted_luid;
    warp = false;
    HRESULT result = ::CreateDXGIFactory1(__uuidof(IDXGIFactory4), factory.put_void());
    if (FAILED(result)) {
        error = "IDXGIFactory4 is unavailable: " + hr_text(result);
        return false;
    }
    result = factory->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), adapter.put_void());
    if (FAILED(result)) {
        error = "EnumAdapterByLuid failed: " + hr_text(result);
        return false;
    }
    return create_device(wanted, error);
}

bool D3d12Rig::open_warp(D3D_FEATURE_LEVEL wanted, std::string& error) {
    warp = true;
    name = "WARP";
    HRESULT result = ::CreateDXGIFactory1(__uuidof(IDXGIFactory4), factory.put_void());
    if (FAILED(result)) {
        error = "IDXGIFactory4 is unavailable: " + hr_text(result);
        return false;
    }
    result = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), adapter.put_void());
    if (FAILED(result)) {
        error = "EnumWarpAdapter failed: " + hr_text(result);
        return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
        luid = desc.AdapterLuid;
    }
    return create_device(wanted, error);
}

bool D3d12Rig::create_device(D3D_FEATURE_LEVEL wanted, std::string& error) {
    level = wanted;
    const HRESULT result = ::D3D12CreateDevice(adapter.get(), wanted, __uuidof(ID3D12Device), device_.put_void());
    if (FAILED(result)) {
        error = "D3D12CreateDevice failed: " + hr_text(result);
        return false;
    }
    if (name.empty()) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            name = Text::utf8(desc.Description);
        }
    }
    return open_queue(D3D12_COMMAND_LIST_TYPE_DIRECT, error);
}

D3d12Queue& D3d12Rig::queue(D3D12_COMMAND_LIST_TYPE type) {
    switch (type) {
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        return compute_;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        return copy_;
    default:
        return direct_;
    }
}

bool D3d12Rig::open_queue(D3D12_COMMAND_LIST_TYPE type, std::string& error) {
    D3d12Queue& target = queue(type);
    return target.is_open() || target.open(device_.get(), type, error);
}

bool D3d12Rig::open_queues(std::string& error) {
    return open_queue(D3D12_COMMAND_LIST_TYPE_DIRECT, error) && open_queue(D3D12_COMMAND_LIST_TYPE_COMPUTE, error) &&
           open_queue(D3D12_COMMAND_LIST_TYPE_COPY, error);
}

void D3d12Rig::release(IUnknown* object) {
    const auto at = std::find(held_.begin(), held_.end(), object);
    if (at == held_.end()) {
        return;
    }
    held_.erase(at);
    direct_.retire_shared(object);
    compute_.retire_shared(object);
    copy_.retire_shared(object);
    object->Release();
    direct_.collect();
    compute_.collect();
    copy_.collect();
}

bool D3d12Rig::idle(std::string& error) {
    return direct_.idle(error) && compute_.idle(error) && copy_.idle(error);
}

void D3d12Rig::shutdown() {
    std::string error;
    bool clean = direct_.close(error);
    clean = compute_.close(error) && clean;
    clean = copy_.close(error) && clean;
    if (!clean) {
        held_.clear();
        static_cast<void>(device_.detach());
        static_cast<void>(adapter.detach());
        static_cast<void>(factory.detach());
        return;
    }
    for (auto at = held_.rbegin(); at != held_.rend(); ++at) {
        (*at)->Release();
    }
    held_.clear();
    device_.reset();
    adapter.reset();
    factory.reset();
}

std::string D3d12Rig::describe() const {
    return name.empty() ? std::string("d3d12 device") : name;
}

}  // namespace lsw::suites
