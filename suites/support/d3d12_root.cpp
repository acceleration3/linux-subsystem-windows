#include "support/d3d12_root.hpp"

#include "support/d3d12_tools.hpp"

namespace lsw::suites {

D3d12Root& D3d12Root::constants(UINT count, UINT shader_register, UINT space, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = shader_register;
    parameter.Constants.RegisterSpace = space;
    parameter.Constants.Num32BitValues = count;
    parameter.ShaderVisibility = visibility;
    parameters_.push_back(parameter);
    return *this;
}

namespace {

D3D12_ROOT_PARAMETER descriptor(D3D12_ROOT_PARAMETER_TYPE type, UINT shader_register, UINT space,
                                D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = type;
    parameter.Descriptor.ShaderRegister = shader_register;
    parameter.Descriptor.RegisterSpace = space;
    parameter.ShaderVisibility = visibility;
    return parameter;
}

}  // namespace

D3d12Root& D3d12Root::cbv(UINT shader_register, UINT space, D3D12_SHADER_VISIBILITY visibility) {
    parameters_.push_back(descriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, shader_register, space, visibility));
    return *this;
}

D3d12Root& D3d12Root::srv(UINT shader_register, UINT space, D3D12_SHADER_VISIBILITY visibility) {
    parameters_.push_back(descriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, shader_register, space, visibility));
    return *this;
}

D3d12Root& D3d12Root::uav(UINT shader_register, UINT space, D3D12_SHADER_VISIBILITY visibility) {
    parameters_.push_back(descriptor(D3D12_ROOT_PARAMETER_TYPE_UAV, shader_register, space, visibility));
    return *this;
}

D3d12Root& D3d12Root::table(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT count, UINT base_register, UINT space,
                            D3D12_SHADER_VISIBILITY visibility) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = type;
    range.NumDescriptors = count;
    range.BaseShaderRegister = base_register;
    range.RegisterSpace = space;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return table(std::vector<D3D12_DESCRIPTOR_RANGE>{range}, visibility);
}

D3d12Root& D3d12Root::table(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges, D3D12_SHADER_VISIBILITY visibility) {
    ranges_.push_back(ranges);
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges_.back().size());
    parameter.DescriptorTable.pDescriptorRanges = ranges_.back().data();
    parameter.ShaderVisibility = visibility;
    parameters_.push_back(parameter);
    return *this;
}

D3d12Root& D3d12Root::sampler(const D3D12_STATIC_SAMPLER_DESC& desc) {
    samplers_.push_back(desc);
    return *this;
}

D3d12Root& D3d12Root::flags(D3D12_ROOT_SIGNATURE_FLAGS value) {
    flags_ = value;
    return *this;
}

D3D12_ROOT_SIGNATURE_DESC D3d12Root::description() const {
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = static_cast<UINT>(parameters_.size());
    desc.pParameters = parameters_.empty() ? nullptr : parameters_.data();
    desc.NumStaticSamplers = static_cast<UINT>(samplers_.size());
    desc.pStaticSamplers = samplers_.empty() ? nullptr : samplers_.data();
    desc.Flags = flags_;
    return desc;
}

HRESULT D3d12Root::serialize(Com<ID3DBlob>& blob, std::string& error) const {
    const D3D12_ROOT_SIGNATURE_DESC desc = description();
    Com<ID3DBlob> messages;
    const HRESULT result = ::D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), messages.put());
    if (FAILED(result)) {
        error = "D3D12SerializeRootSignature failed: " + D3d12Tools::hr(result);
        if (messages) {
            error += " " + std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize());
        }
    }
    return result;
}

HRESULT D3d12Root::build_from(D3d12Rig& rig, const void* blob, SIZE_T size, ID3D12RootSignature*& out) const {
    Com<ID3D12RootSignature> signature;
    const HRESULT result = rig.device()->CreateRootSignature(0, blob, size, __uuidof(ID3D12RootSignature), signature.put_void());
    out = SUCCEEDED(result) ? rig.hold(std::move(signature)) : nullptr;
    return result;
}

ID3D12RootSignature* D3d12Root::build(D3d12Rig& rig, std::string& error) const {
    Com<ID3DBlob> blob;
    if (FAILED(serialize(blob, error))) {
        return nullptr;
    }
    ID3D12RootSignature* signature = nullptr;
    const HRESULT result = build_from(rig, blob->GetBufferPointer(), blob->GetBufferSize(), signature);
    if (FAILED(result)) {
        error = "CreateRootSignature failed: " + D3d12Tools::hr(result);
    }
    return signature;
}

}  // namespace lsw::suites
