#pragma once

#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class D3d12Tools {
public:
    using Bytes = std::vector<std::uint8_t>;

    static constexpr HRESULT kDeviceRemoved = static_cast<HRESULT>(0x887A0005);
    static constexpr HRESULT kDeviceHung = static_cast<HRESULT>(0x887A0006);
    static constexpr HRESULT kDeviceReset = static_cast<HRESULT>(0x887A0007);
    static constexpr HRESULT kDriverInternalError = static_cast<HRESULT>(0x887A0020);
    static constexpr HRESULT kInvalidCall = static_cast<HRESULT>(0x887A0001);
    static constexpr HRESULT kUnsupported = static_cast<HRESULT>(0x887A0004);
    static constexpr HRESULT kAdapterNotFound = static_cast<HRESULT>(0x887E0001);
    static constexpr HRESULT kDriverVersionMismatch = static_cast<HRESULT>(0x887E0002);

    [[nodiscard]] static std::string hr(HRESULT result);

    [[nodiscard]] static std::string hex(std::uint64_t value, int digits = 8);

    [[nodiscard]] static bool in(HRESULT got, std::initializer_list<HRESULT> accepted);

    [[nodiscard]] static std::string accepted_text(std::initializer_list<HRESULT> accepted);

    [[nodiscard]] static D3D12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE type);

    [[nodiscard]] static D3D12_RESOURCE_DESC buffer_desc(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

    [[nodiscard]] static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, DXGI_FORMAT format,
                                                          D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, UINT mips = 1,
                                                          UINT array_size = 1, UINT samples = 1);

    [[nodiscard]] static D3D12_RESOURCE_DESC volume_desc(UINT width, UINT height, UINT depth, DXGI_FORMAT format,
                                                         D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, UINT mips = 1);

    [[nodiscard]] static ID3D12Resource* committed(D3d12Rig& rig, const D3D12_HEAP_PROPERTIES& properties,
                                                   D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC& desc,
                                                   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr,
                                                   HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12Resource* buffer(D3d12Rig& rig, D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state,
                                                D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12Resource* upload(D3d12Rig& rig, UINT64 size, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12Resource* readback(D3d12Rig& rig, UINT64 size, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12Resource* device_buffer(D3d12Rig& rig, UINT64 size, D3D12_RESOURCE_STATES state,
                                                       D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12Resource* texture(D3d12Rig& rig, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                                                 const D3D12_CLEAR_VALUE* clear = nullptr, HRESULT* result = nullptr,
                                                 D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE);

    [[nodiscard]] static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                           D3D12_RESOURCE_STATES after,
                                                           UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    [[nodiscard]] static D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource);

    [[nodiscard]] static D3D12_RESOURCE_BARRIER aliasing_barrier(ID3D12Resource* before, ID3D12Resource* after);

    static void move(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                     D3D12_RESOURCE_STATES after, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    static void uav_wait(ID3D12GraphicsCommandList* list, ID3D12Resource* resource);

    [[nodiscard]] static bool compile(const std::string& source, const char* entry, const char* target, Bytes& code,
                                      std::string& error, const D3D_SHADER_MACRO* macros = nullptr);

    [[nodiscard]] static D3D12_SHADER_BYTECODE bytecode(const Bytes& code);

    [[nodiscard]] static D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc(ID3D12RootSignature* signature, const Bytes& vs,
                                                                          const Bytes& ps, DXGI_FORMAT target);

    [[nodiscard]] static ID3D12PipelineState* graphics_pso(D3d12Rig& rig, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                           std::string& error, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12PipelineState* compute_pso(D3d12Rig& rig, ID3D12RootSignature* signature, const Bytes& cs,
                                                          std::string& error, HRESULT* result = nullptr);

    [[nodiscard]] static ID3D12PipelineState* compute_from(D3d12Rig& rig, ID3D12RootSignature* signature,
                                                           const std::string& source, std::string& error,
                                                           const char* entry = "main");

    [[nodiscard]] static ID3D12DescriptorHeap* descriptor_heap(D3d12Rig& rig, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count,
                                                               bool shader_visible, HRESULT* result = nullptr);

    [[nodiscard]] static D3D12_CPU_DESCRIPTOR_HANDLE cpu_at(ID3D12DescriptorHeap* heap, UINT index, UINT increment);

    [[nodiscard]] static D3D12_GPU_DESCRIPTOR_HANDLE gpu_at(ID3D12DescriptorHeap* heap, UINT index, UINT increment);

    static void structured_srv(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT first,
                               UINT count, UINT stride);

    static void structured_uav(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT first,
                               UINT count, UINT stride);

    [[nodiscard]] static bool write_buffer(D3d12Rig& rig, ID3D12Resource* dest, D3D12_RESOURCE_STATES state, UINT64 offset,
                                           const void* data, UINT64 size, std::string& error);

    [[nodiscard]] static bool read_buffer(D3d12Rig& rig, ID3D12Resource* source, D3D12_RESOURCE_STATES state, UINT64 offset,
                                          UINT64 size, Bytes& out, std::string& error);

    [[nodiscard]] static bool map_bytes(ID3D12Resource* readback, UINT64 offset, UINT64 size, Bytes& out, std::string& error);

    [[nodiscard]] static bool read_texture(D3d12Rig& rig, ID3D12Resource* texture, D3D12_RESOURCE_STATES state,
                                           UINT subresource, TexelImage& out, std::string& error);

    [[nodiscard]] static bool write_texture(D3d12Rig& rig, ID3D12Resource* texture, D3D12_RESOURCE_STATES state,
                                            UINT subresource, const void* tight, std::string& error);

    [[nodiscard]] static bool read_words(D3d12Rig& rig, ID3D12Resource* source, D3D12_RESOURCE_STATES state, UINT count,
                                         std::vector<std::uint32_t>& out, std::string& error);

    [[nodiscard]] static UINT64 align(UINT64 value, UINT64 alignment) { return (value + alignment - 1) / alignment * alignment; }

    template <typename T>
    [[nodiscard]] static HRESULT feature(ID3D12Device* device, D3D12_FEATURE which, T& out) {
        out = T{};
        return device->CheckFeatureSupport(which, &out, sizeof(T));
    }

    static void tight_rows(const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint, UINT rows, UINT64 row_bytes,
                           const std::uint8_t* mapped, TexelImage& out);
};
}  // namespace lsw::suites
