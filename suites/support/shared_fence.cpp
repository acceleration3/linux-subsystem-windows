#include "support/shared_fence.hpp"

#include "core/text.hpp"
#include "support/d3d12_tools.hpp"

namespace lsw::suites {

bool SharedFence::create(D3d12Rig& rig, const std::string& name, Com<ID3D12Fence>& fence, HANDLE& handle, std::string& error) {
    const HRESULT made = rig.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence.put_void());
    if (FAILED(made)) {
        error = "CreateFence(SHARED) returned " + D3d12Tools::hr(made);
        return false;
    }
    const HRESULT shared = rig.device()->CreateSharedHandle(fence.get(), nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle);
    if (FAILED(shared)) {
        error = "CreateSharedHandle returned " + D3d12Tools::hr(shared);
        return false;
    }
    return true;
}

bool SharedFence::open(D3d12Rig& rig, const std::string& name, Com<ID3D12Fence>& fence, std::string& error) {
    HANDLE handle = nullptr;
    HRESULT result = rig.device()->OpenSharedHandleByName(Text::wide(name).c_str(), GENERIC_ALL, &handle);
    if (FAILED(result)) {
        error = "OpenSharedHandleByName returned " + D3d12Tools::hr(result);
        return false;
    }
    result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Fence), fence.put_void());
    ::CloseHandle(handle);
    if (FAILED(result)) {
        error = "OpenSharedHandle returned " + D3d12Tools::hr(result);
        return false;
    }
    return true;
}

bool SharedFence::reaches(ID3D12Fence* fence, std::uint64_t value, DWORD timeout_ms) {
    if (fence->GetCompletedValue() >= value) {
        return true;
    }
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr || FAILED(fence->SetEventOnCompletion(value, event))) {
        if (event != nullptr) {
            ::CloseHandle(event);
        }
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(event, timeout_ms);
    ::CloseHandle(event);
    return waited == WAIT_OBJECT_0;
}

}  // namespace lsw::suites
