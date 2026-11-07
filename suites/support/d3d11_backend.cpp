#include "support/d3d11_backend.hpp"

#include "core/text.hpp"
#include "support/dxgi_budget.hpp"

#include <dxgi1_2.h>

#include <algorithm>
#include <cstring>

namespace lsw::suites {

namespace {

class Buffer11 : public GpuBuffer {
public:
    ~Buffer11() override {
        if (resource != nullptr) {
            resource->Release();
        }
    }

    ID3D11Buffer* resource = nullptr;
};

std::string hr_text(HRESULT result) {
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

}  // namespace

bool D3d11Backend::init(const LUID& luid, std::string& error) {
    luid_ = luid;
    device_ = std::make_shared<D3d11Device>();
    return device_->create(luid, error);
}

std::unique_ptr<GpuBackend> D3d11Backend::new_context() {
    auto other = std::make_unique<D3d11Backend>();
    other->device_ = device_;
    other->luid_ = luid_;
    return other;
}

std::unique_ptr<GpuBuffer> D3d11Backend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                                std::string& detail) {
    std::lock_guard lock(device_->mutex);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(size);
    if (kind == BufferKind::Device) {
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    } else {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    }
    auto buffer = std::make_unique<Buffer11>();
    const HRESULT result = size > 0xffffffffull ? E_INVALIDARG : device_->device->CreateBuffer(&desc, nullptr, &buffer->resource);
    if (FAILED(result)) {
        status = result == E_OUTOFMEMORY ? AllocStatus::OutOfMemory : AllocStatus::Failed;
        detail = "CreateBuffer failed: " + hr_text(result);
        return nullptr;
    }
    buffer->size = size;
    buffer->kind = kind;
    status = AllocStatus::Ok;
    return buffer;
}

std::unique_ptr<GpuBuffer> D3d11Backend::create_shared(std::uint64_t size, HANDLE& handle, std::string& error) {
    std::lock_guard lock(device_->mutex);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(size);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | 0x800;
    auto buffer = std::make_unique<Buffer11>();
    HRESULT result = device_->device->CreateBuffer(&desc, nullptr, &buffer->resource);
    if (FAILED(result)) {
        error = "CreateBuffer (shared) failed: " + hr_text(result);
        return nullptr;
    }
    IDXGIResource1* resource = nullptr;
    result = buffer->resource->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&resource));
    if (SUCCEEDED(result)) {
        result = resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
        resource->Release();
    }
    if (FAILED(result)) {
        error = "CreateSharedHandle failed: " + hr_text(result);
        return nullptr;
    }
    buffer->size = size;
    return buffer;
}

bool D3d11Backend::map(GpuBuffer& buffer, void** pointer, std::string& error) {
    auto& b = static_cast<Buffer11&>(buffer);
    if (b.kind == BufferKind::Device) {
        error = "device buffers cannot be mapped";
        return false;
    }
    std::lock_guard lock(device_->mutex);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device_->context->Map(b.resource, 0, D3D11_MAP_READ_WRITE, 0, &mapped);
    if (FAILED(result)) {
        error = "Map failed: " + hr_text(result);
        return false;
    }
    *pointer = mapped.pData;
    return true;
}

bool D3d11Backend::unmap(GpuBuffer& buffer, std::string&) {
    std::lock_guard lock(device_->mutex);
    device_->context->Unmap(static_cast<Buffer11&>(buffer).resource, 0);
    return true;
}

bool D3d11Backend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size,
                          std::string& error) {
    auto& b = static_cast<Buffer11&>(buffer);
    std::lock_guard lock(device_->mutex);
    if (b.kind == BufferKind::Device) {
        D3D11_BOX box{static_cast<UINT>(offset), 0, 0, static_cast<UINT>(offset + size), 1, 1};
        device_->context->UpdateSubresource(b.resource, 0, offset == 0 && size == b.size ? nullptr : &box, data, 0, 0);
        return true;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device_->context->Map(b.resource, 0, D3D11_MAP_READ_WRITE, 0, &mapped);
    if (FAILED(result)) {
        error = "Map failed: " + hr_text(result);
        return false;
    }
    std::memcpy(static_cast<char*>(mapped.pData) + offset, data, static_cast<std::size_t>(size));
    device_->context->Unmap(b.resource, 0);
    return true;
}

bool D3d11Backend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size,
                            std::string& error) {
    auto& b = static_cast<Buffer11&>(buffer);
    std::lock_guard lock(device_->mutex);
    ID3D11Buffer* staging = b.resource;
    ID3D11Buffer* temporary = nullptr;
    if (b.kind == BufferKind::Device) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(size);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        const HRESULT created = device_->device->CreateBuffer(&desc, nullptr, &temporary);
        if (FAILED(created)) {
            error = "CreateBuffer (readback) failed: " + hr_text(created);
            return false;
        }
        D3D11_BOX box{static_cast<UINT>(offset), 0, 0, static_cast<UINT>(offset + size), 1, 1};
        device_->context->CopySubresourceRegion(temporary, 0, 0, 0, 0, b.resource, 0, &box);
        staging = temporary;
        offset = 0;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device_->context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map (read) failed: " + hr_text(result);
        if (temporary != nullptr) {
            temporary->Release();
        }
        return false;
    }
    std::memcpy(out, static_cast<const char*>(mapped.pData) + offset, static_cast<std::size_t>(size));
    device_->context->Unmap(staging, 0);
    if (temporary != nullptr) {
        temporary->Release();
    }
    return true;
}

bool D3d11Backend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                        std::uint64_t destination_offset, std::uint64_t size, std::string&) {
    std::lock_guard lock(device_->mutex);
    D3D11_BOX box{static_cast<UINT>(source_offset), 0, 0, static_cast<UINT>(source_offset + size), 1, 1};
    device_->context->CopySubresourceRegion(static_cast<Buffer11&>(destination).resource, 0,
                                            static_cast<UINT>(destination_offset), 0, 0,
                                            static_cast<Buffer11&>(source).resource, 0, &box);
    return true;
}

bool D3d11Backend::finish(std::string& error) {
    std::lock_guard lock(device_->mutex);
    return device_->wait_idle(error);
}

std::uint64_t D3d11Backend::max_allocation() {
    VideoMemory memory;
    std::string error;
    std::uint64_t dedicated = 0;
    if (DxgiBudget::query(luid_, true, memory, error)) {
        dedicated = memory.budget;
    }
    const std::uint64_t quarter = dedicated / 4;
    return std::min<std::uint64_t>(2048ull << 20, std::max<std::uint64_t>(128ull << 20, quarter));
}

std::uint64_t D3d11Backend::available_memory() {
    VideoMemory memory;
    std::string error;
    if (!DxgiBudget::query(luid_, true, memory, error)) {
        return 0;
    }
    return memory.budget > memory.usage ? memory.budget - memory.usage : 0;
}

}  // namespace lsw::suites
