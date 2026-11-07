#pragma once

#include "support/d3d12_rig.hpp"

#include <deque>
#include <string>
#include <vector>

namespace lsw::suites {
class D3d12Root {
public:
    D3d12Root& constants(UINT count, UINT shader_register, UINT space = 0,
                         D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& cbv(UINT shader_register, UINT space = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& srv(UINT shader_register, UINT space = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& uav(UINT shader_register, UINT space = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& table(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT count, UINT base_register, UINT space = 0,
                     D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& table(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges,
                     D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    D3d12Root& sampler(const D3D12_STATIC_SAMPLER_DESC& desc);

    D3d12Root& flags(D3D12_ROOT_SIGNATURE_FLAGS value);

    [[nodiscard]] HRESULT serialize(Com<ID3DBlob>& blob, std::string& error) const;

    [[nodiscard]] ID3D12RootSignature* build(D3d12Rig& rig, std::string& error) const;

    [[nodiscard]] HRESULT build_from(D3d12Rig& rig, const void* blob, SIZE_T size, ID3D12RootSignature*& out) const;

    [[nodiscard]] UINT parameter_count() const { return static_cast<UINT>(parameters_.size()); }

private:
    D3D12_ROOT_SIGNATURE_DESC description() const;

    std::vector<D3D12_ROOT_PARAMETER> parameters_;
    std::deque<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges_;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers_;
    D3D12_ROOT_SIGNATURE_FLAGS flags_ = D3D12_ROOT_SIGNATURE_FLAG_NONE;
};
}  // namespace lsw::suites
