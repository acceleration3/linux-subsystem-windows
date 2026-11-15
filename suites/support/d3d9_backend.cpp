#include "support/d3d9_backend.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/dxgi_budget.hpp"

#include <cstring>
#include <vector>

namespace lsw::suites {

namespace {

class BufferD3d9 : public GpuBuffer {
public:
    ~BufferD3d9() override {
        if (resource != nullptr) {
            resource->Release();
        }
    }

    IDirect3DVertexBuffer9* resource = nullptr;
};

}  // namespace

D3d9Backend::~D3d9Backend() {
    if (device_ != nullptr) {
        device_->Release();
    }
    if (d3d_ != nullptr) {
        d3d_->Release();
    }
}

bool D3d9Backend::init(const LUID& luid, std::string& error) {
    luid_ = luid;
    HMODULE module = ::LoadLibraryA("d3d9.dll");
    void* address = module != nullptr ? reinterpret_cast<void*>(::GetProcAddress(module, "Direct3DCreate9Ex")) : nullptr;
    using Create = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    if (address == nullptr || FAILED(reinterpret_cast<Create>(address)(D3D_SDK_VERSION, &d3d_))) {
        error = "Direct3DCreate9Ex is not available";
        return false;
    }
    UINT ordinal = 0xffffffff;
    for (UINT i = 0; i < d3d_->GetAdapterCount(); ++i) {
        LUID found{};
        if (SUCCEEDED(d3d_->GetAdapterLUID(i, &found)) && AvAdapters::key(found) == AvAdapters::key(luid)) {
            ordinal = i;
        }
    }
    if (ordinal == 0xffffffff) {
        error = "no D3D9 adapter carries the LUID";
        return false;
    }
    if (!window_.create({}, error)) {
        return false;
    }
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.hDeviceWindow = window_.hwnd();
    const HRESULT result = d3d_->CreateDeviceEx(ordinal, D3DDEVTYPE_HAL, window_.hwnd(), D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                                                &parameters, nullptr, &device_);
    if (FAILED(result)) {
        error = "CreateDeviceEx failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return false;
    }
    return true;
}

std::unique_ptr<GpuBuffer> D3d9Backend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                               std::string& detail) {
    auto buffer = std::make_unique<BufferD3d9>();
    const HRESULT result = size > 0xffffffffull ? D3DERR_INVALIDCALL
                                                 : device_->CreateVertexBuffer(static_cast<UINT>(size), D3DUSAGE_DYNAMIC, 0,
                                                                               D3DPOOL_DEFAULT, &buffer->resource, nullptr);
    if (FAILED(result)) {
        status = result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY ? AllocStatus::OutOfMemory : AllocStatus::Failed;
        detail = "CreateVertexBuffer failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return nullptr;
    }
    buffer->size = size;
    buffer->kind = kind;
    status = AllocStatus::Ok;
    return buffer;
}

bool D3d9Backend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size, std::string& error) {
    void* pointer = nullptr;
    auto& b = static_cast<BufferD3d9&>(buffer);
    if (FAILED(b.resource->Lock(static_cast<UINT>(offset), static_cast<UINT>(size), &pointer, 0))) {
        error = "Lock failed";
        return false;
    }
    std::memcpy(pointer, data, static_cast<std::size_t>(size));
    b.resource->Unlock();
    return true;
}

bool D3d9Backend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size, std::string& error) {
    void* pointer = nullptr;
    auto& b = static_cast<BufferD3d9&>(buffer);
    if (FAILED(b.resource->Lock(static_cast<UINT>(offset), static_cast<UINT>(size), &pointer, D3DLOCK_READONLY))) {
        error = "Lock (read) failed";
        return false;
    }
    std::memcpy(out, pointer, static_cast<std::size_t>(size));
    b.resource->Unlock();
    return true;
}

bool D3d9Backend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                       std::uint64_t destination_offset, std::uint64_t size, std::string& error) {
    std::vector<char> temporary(static_cast<std::size_t>(size));
    return download(source, source_offset, temporary.data(), size, error) &&
           upload(destination, destination_offset, temporary.data(), size, error);
}

bool D3d9Backend::finish(std::string&) {
    return true;
}

std::uint64_t D3d9Backend::available_memory() {
    VideoMemory memory;
    std::string error;
    if (!DxgiBudget::query(luid_, true, memory, error)) {
        return 0;
    }
    return memory.budget > memory.usage ? memory.budget - memory.usage : 0;
}

}  // namespace lsw::suites
