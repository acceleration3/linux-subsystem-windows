#pragma once

#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/oracle_rig.hpp"

namespace lsw::suites {
class D3d12Frame {
public:
    [[nodiscard]] bool open(D3d12Rig& rig, UINT width, UINT height, std::string& error,
                            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, bool with_depth = false, UINT samples = 1);

    void bind(ID3D12GraphicsCommandList* list) const;

    void clear(ID3D12GraphicsCommandList* list, float r, float g, float b, float a) const;

    void clear_depth(ID3D12GraphicsCommandList* list, float depth) const;

    [[nodiscard]] bool run(const D3d12Queue::Recorder& body, std::string& error);

    [[nodiscard]] bool read(TexelImage& out, std::string& error);

    [[nodiscard]] bool scene(SceneImage& out, std::string& error);

    D3d12Rig* rig = nullptr;
    ID3D12Resource* target = nullptr;
    ID3D12Resource* depth = nullptr;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
};
}  // namespace lsw::suites
