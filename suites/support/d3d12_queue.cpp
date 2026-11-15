#include "support/d3d12_queue.hpp"

#include "core/text.hpp"

namespace lsw::suites {

namespace {

std::string hr_text(HRESULT result) {
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

}  // namespace

D3d12Queue::~D3d12Queue() {
    std::string error;
    static_cast<void>(close(error));
}

bool D3d12Queue::open(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, std::string& error,
                      D3D12_COMMAND_QUEUE_PRIORITY priority) {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = type;
    desc.Priority = priority;
    HRESULT result = device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue_));
    if (FAILED(result)) {
        queue_ = nullptr;
        error = "CreateCommandQueue failed: " + hr_text(result);
        return false;
    }
    result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence_));
    if (FAILED(result)) {
        fence_ = nullptr;
        queue_->Release();
        queue_ = nullptr;
        error = "CreateFence failed: " + hr_text(result);
        return false;
    }
    device_ = device;
    type_ = type;
    return true;
}

std::uint64_t D3d12Queue::signal() {
    ++value_;
    queue_->Signal(fence_, value_);
    dirty_ = false;
    return value_;
}

std::uint64_t D3d12Queue::execute(ID3D12CommandList* const* lists, UINT count) {
    queue_->ExecuteCommandLists(count, lists);
    const std::uint64_t value = signal();
    for (IUnknown* object : recorded_) {
        retired_.emplace_back(value, object);
    }
    recorded_.clear();
    return value;
}

ID3D12CommandList* D3d12Queue::record_as(D3D12_COMMAND_LIST_TYPE list_type, const Recorder& body, std::string& error) {
    ID3D12CommandAllocator* allocator = nullptr;
    HRESULT result = device_->CreateCommandAllocator(list_type, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    if (FAILED(result)) {
        error = "CreateCommandAllocator failed: " + hr_text(result);
        return nullptr;
    }
    ID3D12GraphicsCommandList* list = nullptr;
    result = device_->CreateCommandList(0, list_type, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list));
    if (FAILED(result)) {
        allocator->Release();
        error = "CreateCommandList failed: " + hr_text(result);
        return nullptr;
    }
    body(list);
    result = list->Close();
    if (FAILED(result)) {
        list->Release();
        allocator->Release();
        error = "closing the command list failed: " + hr_text(result);
        return nullptr;
    }
    recorded_.push_back(allocator);
    recorded_.push_back(list);
    return list;
}

bool D3d12Queue::run(const Recorder& body, std::uint64_t& value, std::string& error) {
    ID3D12CommandList* list = record(body, error);
    if (list == nullptr) {
        return false;
    }
    ID3D12CommandList* lists[] = {list};
    value = execute(lists, 1);
    return true;
}

bool D3d12Queue::run_wait(const Recorder& body, std::string& error) {
    std::uint64_t value = 0;
    return run(body, value, error) && wait(value, error);
}

bool D3d12Queue::reached(std::uint64_t value) const {
    return fence_->GetCompletedValue() >= value;
}

bool D3d12Queue::wait(std::uint64_t value, DWORD timeout_ms, std::string& error) {
    if (reached(value)) {
        collect();
        return true;
    }
    HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        error = "CreateEvent failed";
        return false;
    }
    const HRESULT result = fence_->SetEventOnCompletion(value, event);
    if (FAILED(result)) {
        ::CloseHandle(event);
        error = "SetEventOnCompletion failed: " + hr_text(result);
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(event, timeout_ms);
    ::CloseHandle(event);
    if (waited != WAIT_OBJECT_0) {
        error = "the fence did not reach " + std::to_string(value) + " within " + std::to_string(timeout_ms) + " ms";
        return false;
    }
    collect();
    return true;
}

bool D3d12Queue::idle(std::string& error) {
    if (queue_ == nullptr) {
        return true;
    }
    return wait(signal(), error);
}

bool D3d12Queue::gpu_wait(D3d12Queue& other, std::uint64_t value) {
    if (value > other.value_) {
        return false;
    }
    queue_->Wait(other.fence_, value);
    dirty_ = true;
    return true;
}

void D3d12Queue::gpu_wait_fence(ID3D12Fence* fence, std::uint64_t value) {
    fence->AddRef();
    external_.push_back(fence);
    queue_->Wait(fence, value);
    dirty_ = true;
}

void D3d12Queue::retire(IUnknown* object) {
    if (object == nullptr) {
        return;
    }
    if (queue_ == nullptr) {
        object->Release();
        return;
    }
    if (dirty_) {
        signal();
    }
    retired_.emplace_back(value_, object);
}

void D3d12Queue::retire_shared(IUnknown* object) {
    if (object == nullptr || queue_ == nullptr) {
        return;
    }
    object->AddRef();
    retire(object);
}

void D3d12Queue::collect() {
    if (fence_ == nullptr || retired_.empty()) {
        return;
    }
    const std::uint64_t completed = fence_->GetCompletedValue();
    std::size_t kept = 0;
    for (auto& entry : retired_) {
        if (entry.first <= completed) {
            entry.second->Release();
        } else {
            retired_[kept++] = entry;
        }
    }
    retired_.resize(kept);
}

bool D3d12Queue::drain(std::string& error) {
    if (queue_ == nullptr) {
        return true;
    }
    for (ID3D12Fence* fence : external_) {
        fence->Signal(~0ull);
    }
    return idle(error);
}

bool D3d12Queue::close(std::string& error) {
    if (queue_ == nullptr) {
        return true;
    }
    const bool drained = drain(error);
    if (!drained) {
        retired_.clear();
        recorded_.clear();
        external_.clear();
        queue_ = nullptr;
        fence_ = nullptr;
        return false;
    }
    for (auto& entry : retired_) {
        entry.second->Release();
    }
    retired_.clear();
    for (IUnknown* object : recorded_) {
        object->Release();
    }
    recorded_.clear();
    for (ID3D12Fence* fence : external_) {
        fence->Release();
    }
    external_.clear();
    fence_->Release();
    queue_->Release();
    fence_ = nullptr;
    queue_ = nullptr;
    return true;
}

}  // namespace lsw::suites
