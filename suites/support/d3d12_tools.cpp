#include "support/d3d12_tools.hpp"

#include "core/text.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites {

std::string D3d12Tools::hex(std::uint64_t value, int digits) {
    return Text::hex(value, digits);
}

std::string D3d12Tools::hr(HRESULT result) {
    struct Named {
        HRESULT code;
        const char* name;
    };
    static const Named names[] = {
        {S_OK, "S_OK"},
        {S_FALSE, "S_FALSE"},
        {E_FAIL, "E_FAIL"},
        {E_INVALIDARG, "E_INVALIDARG"},
        {E_OUTOFMEMORY, "E_OUTOFMEMORY"},
        {E_NOTIMPL, "E_NOTIMPL"},
        {E_NOINTERFACE, "E_NOINTERFACE"},
        {E_ACCESSDENIED, "E_ACCESSDENIED"},
        {kDeviceRemoved, "DXGI_ERROR_DEVICE_REMOVED"},
        {kDeviceHung, "DXGI_ERROR_DEVICE_HUNG"},
        {kDeviceReset, "DXGI_ERROR_DEVICE_RESET"},
        {kDriverInternalError, "DXGI_ERROR_DRIVER_INTERNAL_ERROR"},
        {kInvalidCall, "DXGI_ERROR_INVALID_CALL"},
        {kUnsupported, "DXGI_ERROR_UNSUPPORTED"},
        {kAdapterNotFound, "D3D12_ERROR_ADAPTER_NOT_FOUND"},
        {kDriverVersionMismatch, "D3D12_ERROR_DRIVER_VERSION_MISMATCH"},
    };
    for (const Named& entry : names) {
        if (entry.code == result) {
            return std::string(entry.name) + " (0x" + Text::hex(static_cast<std::uint32_t>(result), 8) + ")";
        }
    }
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

bool D3d12Tools::in(HRESULT got, std::initializer_list<HRESULT> accepted) {
    return std::find(accepted.begin(), accepted.end(), got) != accepted.end();
}

std::string D3d12Tools::accepted_text(std::initializer_list<HRESULT> accepted) {
    std::string text;
    for (HRESULT code : accepted) {
        text += (text.empty() ? "" : " or ") + hr(code);
    }
    return text;
}

D3D12_HEAP_PROPERTIES D3d12Tools::heap_props(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = type;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_DESC D3d12Tools::buffer_desc(UINT64 size, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

D3D12_RESOURCE_DESC D3d12Tools::texture_desc(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                             UINT mips, UINT array_size, UINT samples) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(array_size);
    desc.MipLevels = static_cast<UINT16>(mips);
    desc.Format = format;
    desc.SampleDesc.Count = samples;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

D3D12_RESOURCE_DESC D3d12Tools::volume_desc(UINT width, UINT height, UINT depth, DXGI_FORMAT format,
                                            D3D12_RESOURCE_FLAGS flags, UINT mips) {
    D3D12_RESOURCE_DESC desc = texture_desc(width, height, format, flags, mips, depth);
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    return desc;
}

ID3D12Resource* D3d12Tools::committed(D3d12Rig& rig, const D3D12_HEAP_PROPERTIES& properties, D3D12_HEAP_FLAGS flags,
                                      const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                                      const D3D12_CLEAR_VALUE* clear, HRESULT* result) {
    Com<ID3D12Resource> resource;
    const HRESULT created = rig.device()->CreateCommittedResource(&properties, flags, &desc, state, clear,
                                                                  __uuidof(ID3D12Resource), resource.put_void());
    if (result != nullptr) {
        *result = created;
    }
    return SUCCEEDED(created) ? rig.hold(std::move(resource)) : nullptr;
}

ID3D12Resource* D3d12Tools::buffer(D3d12Rig& rig, D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state,
                                   D3D12_RESOURCE_FLAGS flags, HRESULT* result) {
    return committed(rig, heap_props(type), D3D12_HEAP_FLAG_NONE, buffer_desc(size, flags), state, nullptr, result);
}

ID3D12Resource* D3d12Tools::upload(D3d12Rig& rig, UINT64 size, HRESULT* result) {
    return buffer(rig, D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, result);
}

ID3D12Resource* D3d12Tools::readback(D3d12Rig& rig, UINT64 size, HRESULT* result) {
    return buffer(rig, D3D12_HEAP_TYPE_READBACK, size, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, result);
}

ID3D12Resource* D3d12Tools::device_buffer(D3d12Rig& rig, UINT64 size, D3D12_RESOURCE_STATES state,
                                          D3D12_RESOURCE_FLAGS flags, HRESULT* result) {
    return buffer(rig, D3D12_HEAP_TYPE_DEFAULT, size, state, flags, result);
}

ID3D12Resource* D3d12Tools::texture(D3d12Rig& rig, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                                    const D3D12_CLEAR_VALUE* clear, HRESULT* result, D3D12_HEAP_FLAGS flags) {
    return committed(rig, heap_props(D3D12_HEAP_TYPE_DEFAULT), flags, desc, state, clear, result);
}

D3D12_RESOURCE_BARRIER D3d12Tools::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                              D3D12_RESOURCE_STATES after, UINT subresource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;
    return barrier;
}

D3D12_RESOURCE_BARRIER D3d12Tools::uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

D3D12_RESOURCE_BARRIER D3d12Tools::aliasing_barrier(ID3D12Resource* before, ID3D12Resource* after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Aliasing.pResourceBefore = before;
    barrier.Aliasing.pResourceAfter = after;
    return barrier;
}

void D3d12Tools::move(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                      D3D12_RESOURCE_STATES after, UINT subresource) {
    const D3D12_RESOURCE_BARRIER barrier = transition(resource, before, after, subresource);
    list->ResourceBarrier(1, &barrier);
}

void D3d12Tools::uav_wait(ID3D12GraphicsCommandList* list, ID3D12Resource* resource) {
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(resource);
    list->ResourceBarrier(1, &barrier);
}

bool D3d12Tools::compile(const std::string& source, const char* entry, const char* target, Bytes& code, std::string& error,
                         const D3D_SHADER_MACRO* macros) {
    return D3d11Tools::compile(source, entry, target, code, error, macros);
}

D3D12_SHADER_BYTECODE D3d12Tools::bytecode(const Bytes& code) {
    return {code.data(), code.size()};
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC D3d12Tools::graphics_desc(ID3D12RootSignature* signature, const Bytes& vs, const Bytes& ps,
                                                             DXGI_FORMAT target) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = signature;
    desc.VS = bytecode(vs);
    desc.PS = bytecode(ps);
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.SampleMask = 0xffffffffu;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.StencilReadMask = 0xff;
    desc.DepthStencilState.StencilWriteMask = 0xff;
    desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = target == DXGI_FORMAT_UNKNOWN ? 0 : 1;
    desc.RTVFormats[0] = target;
    desc.SampleDesc.Count = 1;
    return desc;
}

ID3D12PipelineState* D3d12Tools::graphics_pso(D3d12Rig& rig, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, std::string& error,
                                              HRESULT* result) {
    Com<ID3D12PipelineState> state;
    const HRESULT created = rig.device()->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
    if (result != nullptr) {
        *result = created;
    }
    if (FAILED(created)) {
        error = "CreateGraphicsPipelineState failed: " + hr(created);
        return nullptr;
    }
    return rig.hold(std::move(state));
}

ID3D12PipelineState* D3d12Tools::compute_pso(D3d12Rig& rig, ID3D12RootSignature* signature, const Bytes& cs, std::string& error,
                                             HRESULT* result) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = signature;
    desc.CS = bytecode(cs);
    Com<ID3D12PipelineState> state;
    const HRESULT created = rig.device()->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
    if (result != nullptr) {
        *result = created;
    }
    if (FAILED(created)) {
        error = "CreateComputePipelineState failed: " + hr(created);
        return nullptr;
    }
    return rig.hold(std::move(state));
}

ID3D12PipelineState* D3d12Tools::compute_from(D3d12Rig& rig, ID3D12RootSignature* signature, const std::string& source,
                                              std::string& error, const char* entry) {
    Bytes code;
    if (!compile(source, entry, "cs_5_1", code, error)) {
        return nullptr;
    }
    return compute_pso(rig, signature, code, error);
}

ID3D12DescriptorHeap* D3d12Tools::descriptor_heap(D3d12Rig& rig, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool shader_visible,
                                                  HRESULT* result) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    Com<ID3D12DescriptorHeap> heap;
    const HRESULT created = rig.device()->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.put_void());
    if (result != nullptr) {
        *result = created;
    }
    return SUCCEEDED(created) ? rig.hold(std::move(heap)) : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3d12Tools::cpu_at(ID3D12DescriptorHeap* heap, UINT index, UINT increment) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * increment;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3d12Tools::gpu_at(ID3D12DescriptorHeap* heap, UINT index, UINT increment) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * increment;
    return handle;
}

void D3d12Tools::structured_srv(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT first,
                                UINT count, UINT stride) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = count;
    desc.Buffer.StructureByteStride = stride;
    device->CreateShaderResourceView(resource, &desc, handle);
}

void D3d12Tools::structured_uav(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT first,
                                UINT count, UINT stride) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = count;
    desc.Buffer.StructureByteStride = stride;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, handle);
}

bool D3d12Tools::write_buffer(D3d12Rig& rig, ID3D12Resource* dest, D3D12_RESOURCE_STATES state, UINT64 offset, const void* data,
                              UINT64 size, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* staging = upload(rig, size, &result);
    if (staging == nullptr) {
        error = "creating the upload buffer failed: " + hr(result);
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    result = staging->Map(0, &none, &mapped);
    if (FAILED(result)) {
        error = "mapping the upload buffer failed: " + hr(result);
        rig.release(staging);
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    const D3D12_RANGE written{0, static_cast<SIZE_T>(size)};
    staging->Unmap(0, &written);
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
                move(list, dest, state, D3D12_RESOURCE_STATE_COPY_DEST);
            }
            list->CopyBufferRegion(dest, offset, staging, 0, size);
            if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
                move(list, dest, D3D12_RESOURCE_STATE_COPY_DEST, state);
            }
        },
        error);
    rig.release(staging);
    return done;
}

bool D3d12Tools::read_buffer(D3d12Rig& rig, ID3D12Resource* source, D3D12_RESOURCE_STATES state, UINT64 offset, UINT64 size,
                             Bytes& out, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* staging = readback(rig, size, &result);
    if (staging == nullptr) {
        error = "creating the readback buffer failed: " + hr(result);
        return false;
    }
    const bool covers = (state & D3D12_RESOURCE_STATE_COPY_SOURCE) == D3D12_RESOURCE_STATE_COPY_SOURCE;
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            if (!covers) {
                move(list, source, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            list->CopyBufferRegion(staging, 0, source, offset, size);
            if (!covers) {
                move(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
            }
        },
        error);
    if (!done) {
        rig.release(staging);
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE all{0, static_cast<SIZE_T>(size)};
    result = staging->Map(0, &all, &mapped);
    if (FAILED(result)) {
        error = "mapping the readback buffer failed: " + hr(result);
        rig.release(staging);
        return false;
    }
    out.assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + size);
    const D3D12_RANGE none{0, 0};
    staging->Unmap(0, &none);
    rig.release(staging);
    return true;
}

bool D3d12Tools::map_bytes(ID3D12Resource* readback, UINT64 offset, UINT64 size, Bytes& out, std::string& error) {
    void* mapped = nullptr;
    const D3D12_RANGE range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)};
    const HRESULT result = readback->Map(0, &range, &mapped);
    if (FAILED(result)) {
        error = "mapping the readback buffer failed: " + hr(result);
        return false;
    }
    out.assign(static_cast<const std::uint8_t*>(mapped) + offset, static_cast<const std::uint8_t*>(mapped) + offset + size);
    const D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return true;
}

bool D3d12Tools::read_words(D3d12Rig& rig, ID3D12Resource* source, D3D12_RESOURCE_STATES state, UINT count,
                            std::vector<std::uint32_t>& out, std::string& error) {
    Bytes bytes;
    if (!read_buffer(rig, source, state, 0, static_cast<UINT64>(count) * 4, bytes, error)) {
        return false;
    }
    out.resize(count);
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

void D3d12Tools::tight_rows(const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint, UINT rows, UINT64 row_bytes,
                            const std::uint8_t* mapped, TexelImage& out) {
    out.width = footprint.Footprint.Width;
    out.height = footprint.Footprint.Height;
    out.depth = footprint.Footprint.Depth;
    out.row_bytes = static_cast<UINT>(row_bytes);
    out.rows = rows;
    out.data.assign(static_cast<std::size_t>(row_bytes) * rows * out.depth, 0);
    for (UINT z = 0; z < out.depth; ++z) {
        for (UINT r = 0; r < rows; ++r) {
            const std::uint8_t* from = mapped + footprint.Offset + (static_cast<UINT64>(z) * rows + r) * footprint.Footprint.RowPitch;
            std::memcpy(out.data.data() + (static_cast<std::size_t>(z) * rows + r) * row_bytes, from, static_cast<std::size_t>(row_bytes));
        }
    }
}

bool D3d12Tools::read_texture(D3d12Rig& rig, ID3D12Resource* texture, D3D12_RESOURCE_STATES state, UINT subresource,
                              TexelImage& out, std::string& error) {
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    rig.device()->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint, &rows, &row_size, &total);
    HRESULT result = S_OK;
    ID3D12Resource* staging = readback(rig, total, &result);
    if (staging == nullptr) {
        error = "creating the readback buffer failed: " + hr(result);
        return false;
    }
    const bool covers = (state & D3D12_RESOURCE_STATE_COPY_SOURCE) == D3D12_RESOURCE_STATE_COPY_SOURCE;
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            if (!covers) {
                move(list, texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = staging;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = subresource;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            if (!covers) {
                move(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
            }
        },
        error);
    if (!done) {
        rig.release(staging);
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE all{0, static_cast<SIZE_T>(total)};
    result = staging->Map(0, &all, &mapped);
    if (FAILED(result)) {
        error = "mapping the readback buffer failed: " + hr(result);
        rig.release(staging);
        return false;
    }
    tight_rows(footprint, rows, row_size, static_cast<const std::uint8_t*>(mapped), out);
    const D3D12_RANGE none{0, 0};
    staging->Unmap(0, &none);
    rig.release(staging);
    return true;
}

bool D3d12Tools::write_texture(D3d12Rig& rig, ID3D12Resource* texture, D3D12_RESOURCE_STATES state, UINT subresource,
                               const void* tight, std::string& error) {
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    rig.device()->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint, &rows, &row_size, &total);
    HRESULT result = S_OK;
    ID3D12Resource* staging = upload(rig, total, &result);
    if (staging == nullptr) {
        error = "creating the upload buffer failed: " + hr(result);
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    result = staging->Map(0, &none, &mapped);
    if (FAILED(result)) {
        error = "mapping the upload buffer failed: " + hr(result);
        rig.release(staging);
        return false;
    }
    std::memset(mapped, 0, static_cast<std::size_t>(total));
    const auto* from = static_cast<const std::uint8_t*>(tight);
    for (UINT z = 0; z < footprint.Footprint.Depth; ++z) {
        for (UINT r = 0; r < rows; ++r) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + footprint.Offset + (static_cast<UINT64>(z) * rows + r) * footprint.Footprint.RowPitch,
                        from + (static_cast<std::size_t>(z) * rows + r) * row_size, static_cast<std::size_t>(row_size));
        }
    }
    const D3D12_RANGE written{0, static_cast<SIZE_T>(total)};
    staging->Unmap(0, &written);
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
                move(list, texture, state, D3D12_RESOURCE_STATE_COPY_DEST);
            }
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = staging;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
                move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, state);
            }
        },
        error);
    rig.release(staging);
    return done;
}

}  // namespace lsw::suites
