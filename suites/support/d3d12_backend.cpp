#include "support/d3d12_backend.hpp"

#include "core/text.hpp"
#include "support/dxgi_budget.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites {

namespace {

class Buffer12 : public GpuBuffer {
public:
    ~Buffer12() override {
        if (owner != nullptr) {
            owner->drain();
        }
        if (mapped) {
            resource->Unmap(0, nullptr);
        }
        if (resource != nullptr) {
            resource->Release();
        }
    }

    D3d12Backend* owner = nullptr;
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    bool mapped = false;
};

std::string hr_text(HRESULT result) {
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

D3D12_RESOURCE_DESC buffer_desc(std::uint64_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

ID3D12Resource* committed(ID3D12Device* device, D3D12_HEAP_TYPE type, std::uint64_t size,
                          D3D12_RESOURCE_STATES state, HRESULT& result) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    const D3D12_RESOURCE_DESC desc = buffer_desc(size);
    ID3D12Resource* resource = nullptr;
    result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                             __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource));
    return SUCCEEDED(result) ? resource : nullptr;
}

void transition(ID3D12GraphicsCommandList* list, Buffer12& buffer, D3D12_RESOURCE_STATES wanted) {
    if (buffer.kind != BufferKind::Device || buffer.state == wanted) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer.resource;
    barrier.Transition.StateBefore = buffer.state;
    barrier.Transition.StateAfter = wanted;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
    buffer.state = wanted;
}

}  // namespace

D3d12Backend::~D3d12Backend() {
    std::string error;
    if (queue_ != nullptr) {
        static_cast<void>(submit(error));
    }
    if (ring_ != nullptr) {
        ring_->Unmap(0, nullptr);
        ring_->Release();
    }
    if (readback_ != nullptr) {
        readback_->Release();
    }
    release_queue();
}

void D3d12Backend::release_queue() {
    if (list_ != nullptr) {
        list_->Release();
    }
    if (allocator_ != nullptr) {
        allocator_->Release();
    }
    if (fence_ != nullptr) {
        fence_->Release();
    }
    if (queue_ != nullptr) {
        queue_->Release();
    }
    if (event_ != nullptr) {
        ::CloseHandle(event_);
    }
    list_ = nullptr;
    allocator_ = nullptr;
    fence_ = nullptr;
    queue_ = nullptr;
    event_ = nullptr;
}

bool D3d12Backend::init(const LUID& luid, std::string& error) {
    luid_ = luid;
    device_ = std::make_shared<D3d12Device>();
    if (!device_->create(luid, error)) {
        return false;
    }
    ID3D12Device* dev = device_->device;
    queue_ = device_->make_queue(error);
    if (queue_ == nullptr) {
        return false;
    }
    HRESULT result = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                 reinterpret_cast<void**>(&allocator_));
    if (SUCCEEDED(result)) {
        result = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_, nullptr,
                                        __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list_));
    }
    if (SUCCEEDED(result)) {
        list_->Close();
        result = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence_));
    }
    if (FAILED(result)) {
        error = "creating the D3D12 command objects failed: " + hr_text(result);
        return false;
    }
    event_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

std::unique_ptr<GpuBackend> D3d12Backend::new_context() {
    auto other = std::make_unique<D3d12Backend>();
    other->device_ = device_;
    other->luid_ = luid_;
    ID3D12Device* dev = device_->device;
    std::string error;
    other->queue_ = device_->make_queue(error);
    if (other->queue_ == nullptr) {
        return nullptr;
    }
    HRESULT result = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                 reinterpret_cast<void**>(&other->allocator_));
    if (SUCCEEDED(result)) {
        result = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, other->allocator_, nullptr,
                                        __uuidof(ID3D12GraphicsCommandList),
                                        reinterpret_cast<void**>(&other->list_));
    }
    if (SUCCEEDED(result)) {
        other->list_->Close();
        result = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                  reinterpret_cast<void**>(&other->fence_));
    }
    if (FAILED(result)) {
        return nullptr;
    }
    other->event_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return other;
}

bool D3d12Backend::begin(std::string& error) {
    if (open_) {
        return true;
    }
    HRESULT result = allocator_->Reset();
    if (SUCCEEDED(result)) {
        result = list_->Reset(allocator_, nullptr);
    }
    if (FAILED(result)) {
        error = "resetting the D3D12 command list failed: " + hr_text(result);
        return false;
    }
    open_ = true;
    return true;
}

bool D3d12Backend::submit(std::string& error) {
    if (!open_) {
        return true;
    }
    open_ = false;
    HRESULT result = list_->Close();
    if (FAILED(result)) {
        error = "closing the D3D12 command list failed: " + hr_text(result);
        return false;
    }
    ID3D12CommandList* lists[] = {list_};
    queue_->ExecuteCommandLists(1, lists);
    ++fence_value_;
    result = queue_->Signal(fence_, fence_value_);
    if (SUCCEEDED(result) && fence_->GetCompletedValue() < fence_value_) {
        fence_->SetEventOnCompletion(fence_value_, event_);
        if (::WaitForSingleObject(event_, 60000) != WAIT_OBJECT_0) {
            error = "the D3D12 queue did not finish in 60 s";
            return false;
        }
    }
    ring_head_ = 0;
    const HRESULT removed = device_->device->GetDeviceRemovedReason();
    if (FAILED(result) || FAILED(removed)) {
        error = "the D3D12 device reports an error: " + hr_text(FAILED(result) ? result : removed);
        return false;
    }
    return true;
}

bool D3d12Backend::stage(const void* data, std::uint64_t size, std::uint64_t& offset, std::string& error) {
    const std::uint64_t needed = (size + 255) & ~255ull;
    if (ring_ == nullptr || needed > ring_size_ - ring_head_) {
        if (!submit(error)) {
            return false;
        }
        if (ring_ == nullptr || needed > ring_size_) {
            if (ring_ != nullptr) {
                ring_->Unmap(0, nullptr);
                ring_->Release();
                ring_ = nullptr;
            }
            ring_size_ = std::max<std::uint64_t>(needed, 16ull << 20);
            HRESULT result = S_OK;
            ring_ = committed(device_->device, D3D12_HEAP_TYPE_UPLOAD, ring_size_, D3D12_RESOURCE_STATE_GENERIC_READ, result);
            if (ring_ == nullptr || FAILED(ring_->Map(0, nullptr, reinterpret_cast<void**>(&ring_map_)))) {
                error = "creating the D3D12 staging buffer failed: " + hr_text(result);
                return false;
            }
        }
        ring_head_ = 0;
    }
    std::memcpy(ring_map_ + ring_head_, data, static_cast<std::size_t>(size));
    offset = ring_head_;
    ring_head_ += needed;
    return true;
}

bool D3d12Backend::ensure_readback(std::uint64_t size, std::string& error) {
    if (readback_ != nullptr && readback_size_ >= size) {
        return true;
    }
    if (readback_ != nullptr) {
        readback_->Release();
        readback_ = nullptr;
    }
    HRESULT result = S_OK;
    readback_size_ = std::max<std::uint64_t>(size, 4ull << 20);
    readback_ = committed(device_->device, D3D12_HEAP_TYPE_READBACK, readback_size_, D3D12_RESOURCE_STATE_COPY_DEST, result);
    if (readback_ == nullptr) {
        error = "creating the D3D12 readback buffer failed: " + hr_text(result);
        return false;
    }
    return true;
}

std::unique_ptr<GpuBuffer> D3d12Backend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                                std::string& detail) {
    if (size == 0) {
        status = AllocStatus::Failed;
        detail = "zero-sized buffer";
    }
    auto buffer = std::make_unique<Buffer12>();
    HRESULT result = E_INVALIDARG;
    if (size != 0) {
        const D3D12_HEAP_TYPE type = kind == BufferKind::Device ? D3D12_HEAP_TYPE_DEFAULT
                                     : kind == BufferKind::Upload ? D3D12_HEAP_TYPE_UPLOAD
                                                                  : D3D12_HEAP_TYPE_READBACK;
        const D3D12_RESOURCE_STATES state = kind == BufferKind::Upload   ? D3D12_RESOURCE_STATE_GENERIC_READ
                                            : kind == BufferKind::Readback ? D3D12_RESOURCE_STATE_COPY_DEST
                                                                           : D3D12_RESOURCE_STATE_COPY_DEST;
        buffer->resource = committed(device_->device, type, size, state, result);
    }
    if (buffer->resource == nullptr) {
        status = result == E_OUTOFMEMORY ? AllocStatus::OutOfMemory : AllocStatus::Failed;
        detail = "CreateCommittedResource failed: " + hr_text(result);
        return nullptr;
    }
    buffer->owner = this;
    buffer->size = size;
    buffer->kind = kind;
    status = AllocStatus::Ok;
    return buffer;
}

bool D3d12Backend::map(GpuBuffer& buffer, void** pointer, std::string& error) {
    auto& b = static_cast<Buffer12&>(buffer);
    if (b.kind == BufferKind::Device) {
        error = "device buffers cannot be mapped";
        return false;
    }
    const HRESULT result = b.resource->Map(0, nullptr, pointer);
    if (FAILED(result)) {
        error = "Map failed: " + hr_text(result);
        return false;
    }
    b.mapped = true;
    return true;
}

bool D3d12Backend::unmap(GpuBuffer& buffer, std::string&) {
    auto& b = static_cast<Buffer12&>(buffer);
    if (b.mapped) {
        b.resource->Unmap(0, nullptr);
        b.mapped = false;
    }
    return true;
}

bool D3d12Backend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size,
                          std::string& error) {
    auto& b = static_cast<Buffer12&>(buffer);
    if (b.kind != BufferKind::Device) {
        if (b.kind == BufferKind::Readback) {
            error = "readback buffers are not written by the CPU";
            return false;
        }
        void* pointer = nullptr;
        const HRESULT result = b.resource->Map(0, nullptr, &pointer);
        if (FAILED(result)) {
            error = "Map failed: " + hr_text(result);
            return false;
        }
        std::memcpy(static_cast<char*>(pointer) + offset, data, static_cast<std::size_t>(size));
        b.resource->Unmap(0, nullptr);
        return true;
    }
    std::uint64_t staged = 0;
    if (!stage(data, size, staged, error) || !begin(error)) {
        return false;
    }
    transition(list_, b, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(b.resource, offset, ring_, staged, size);
    return true;
}

bool D3d12Backend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size,
                            std::string& error) {
    auto& b = static_cast<Buffer12&>(buffer);
    if (b.kind == BufferKind::Upload) {
        if (!submit(error)) {
            return false;
        }
        void* pointer = nullptr;
        if (FAILED(b.resource->Map(0, nullptr, &pointer))) {
            error = "Map failed";
            return false;
        }
        std::memcpy(out, static_cast<char*>(pointer) + offset, static_cast<std::size_t>(size));
        b.resource->Unmap(0, nullptr);
        return true;
    }
    if (b.kind == BufferKind::Device) {
        if (!ensure_readback(size, error) || !begin(error)) {
            return false;
        }
        transition(list_, b, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list_->CopyBufferRegion(readback_, 0, b.resource, offset, size);
        if (!submit(error)) {
            return false;
        }
        D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
        void* pointer = nullptr;
        if (FAILED(readback_->Map(0, &range, &pointer))) {
            error = "Map (readback) failed";
            return false;
        }
        std::memcpy(out, pointer, static_cast<std::size_t>(size));
        D3D12_RANGE none{0, 0};
        readback_->Unmap(0, &none);
        return true;
    }
    if (!submit(error)) {
        return false;
    }
    D3D12_RANGE range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)};
    void* pointer = nullptr;
    if (FAILED(b.resource->Map(0, &range, &pointer))) {
        error = "Map (readback) failed";
        return false;
    }
    std::memcpy(out, static_cast<char*>(pointer) + offset, static_cast<std::size_t>(size));
    D3D12_RANGE none{0, 0};
    b.resource->Unmap(0, &none);
    return true;
}

bool D3d12Backend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                        std::uint64_t destination_offset, std::uint64_t size, std::string& error) {
    auto& s = static_cast<Buffer12&>(source);
    auto& d = static_cast<Buffer12&>(destination);
    if (d.kind == BufferKind::Upload) {
        error = "D3D12 cannot copy into an upload-heap buffer";
        return false;
    }
    if (!begin(error)) {
        return false;
    }
    transition(list_, s, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list_, d, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(d.resource, destination_offset, s.resource, source_offset, size);
    return true;
}

void D3d12Backend::drain() {
    std::string error;
    static_cast<void>(submit(error));
}

bool D3d12Backend::finish(std::string& error) {
    return submit(error);
}

std::unique_ptr<GpuBuffer> D3d12Backend::open_shared(HANDLE handle, std::uint64_t size, std::string& error) {
    auto buffer = std::make_unique<Buffer12>();
    const HRESULT result = device_->device->OpenSharedHandle(handle, __uuidof(ID3D12Resource),
                                                             reinterpret_cast<void**>(&buffer->resource));
    if (FAILED(result)) {
        error = "OpenSharedHandle failed: " + hr_text(result);
        return nullptr;
    }
    buffer->owner = this;
    buffer->size = size;
    buffer->kind = BufferKind::Device;
    buffer->state = D3D12_RESOURCE_STATE_COMMON;
    return buffer;
}

ID3D12Resource* D3d12Backend::open_shared_raw(HANDLE handle, std::string& error) {
    ID3D12Resource* resource = nullptr;
    const HRESULT result = device_->device->OpenSharedHandle(handle, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource));
    if (FAILED(result)) {
        error = "OpenSharedHandle failed: " + hr_text(result);
        return nullptr;
    }
    return resource;
}

bool D3d12Backend::read_texture(ID3D12Resource* texture, UINT width, UINT height, UINT bytes_per_pixel,
                                std::vector<std::uint8_t>& tight, std::string& error) {
    const UINT pitch = (width * bytes_per_pixel + 255) & ~255u;
    const std::uint64_t total = static_cast<std::uint64_t>(pitch) * height;
    if (!ensure_readback(total, error) || !begin(error)) {
        return false;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback_;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = texture->GetDesc().Format;
    destination.PlacedFootprint.Footprint.Width = width;
    destination.PlacedFootprint.Footprint.Height = height;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = pitch;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list_->ResourceBarrier(1, &barrier);
    if (!submit(error)) {
        return false;
    }
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    void* pointer = nullptr;
    if (FAILED(readback_->Map(0, &range, &pointer))) {
        error = "Map (readback) failed";
        return false;
    }
    tight.assign(static_cast<std::size_t>(width) * height * bytes_per_pixel, 0);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(tight.data() + static_cast<std::size_t>(y) * width * bytes_per_pixel,
                    static_cast<const char*>(pointer) + static_cast<std::size_t>(y) * pitch, static_cast<std::size_t>(width) * bytes_per_pixel);
    }
    D3D12_RANGE none{0, 0};
    readback_->Unmap(0, &none);
    return true;
}

bool D3d12Backend::evict(GpuBuffer& buffer, std::string& error) {
    if (!submit(error)) {
        return false;
    }
    ID3D12Pageable* pageable = static_cast<Buffer12&>(buffer).resource;
    const HRESULT result = device_->device->Evict(1, &pageable);
    if (FAILED(result)) {
        error = "Evict failed: " + hr_text(result);
        return false;
    }
    return true;
}

bool D3d12Backend::make_resident(GpuBuffer& buffer, std::string& error) {
    ID3D12Pageable* pageable = static_cast<Buffer12&>(buffer).resource;
    const HRESULT result = device_->device->MakeResident(1, &pageable);
    if (FAILED(result)) {
        error = "MakeResident failed: " + hr_text(result);
        return false;
    }
    return true;
}

std::uint64_t D3d12Backend::available_memory() {
    VideoMemory memory;
    std::string error;
    if (!DxgiBudget::query(luid_, true, memory, error)) {
        return 0;
    }
    return memory.budget > memory.usage ? memory.budget - memory.usage : 0;
}

std::uint64_t D3d12Backend::oversize_request() {
    VideoMemory local;
    VideoMemory shared;
    std::string error;
    if (!DxgiBudget::query(luid_, true, local, error) || !DxgiBudget::query(luid_, false, shared, error)) {
        return 0;
    }
    return (local.budget + shared.budget) * 2 + (1ull << 30);
}

}  // namespace lsw::suites
