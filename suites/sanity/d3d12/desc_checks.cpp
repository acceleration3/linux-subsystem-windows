#include "sanity/d3d12/desc_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kTableShader =
    "StructuredBuffer<uint> bufs[N] : register(t0);\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[id.x] = bufs[NonUniformResourceIndex(id.x)][0] ^ id.x; }\n";

const char* kSamplerShader =
    "Texture2D<float> tex : register(t0);\n"
    "SamplerState smp[2] : register(s0);\n"
    "RWStructuredBuffer<float> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() { o[0] = tex.SampleLevel(smp[0], float2(0.375, 0.5), 0); o[1] = tex.SampleLevel(smp[1], float2(0.375, 0.5), 0); }\n";

bool run_table(D3d12Rig& rig, Context& context, Problems& problems, const std::string& label, UINT count, UINT heap_total, UINT base, bool unbounded, bool use_copy_multi) {
    std::string error;
    ID3D12Device* d = rig.device();
    const UINT increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3d12Root root;
    root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, unbounded ? 0xffffffffu : count, 0).uav(0);
    ID3D12RootSignature* signature = root.build(rig, error);
    const std::string size_text = std::to_string(count);
    const D3D_SHADER_MACRO macros[] = {{"N", size_text.c_str()}, {nullptr, nullptr}};
    D3d12Tools::Bytes code;
    if (signature == nullptr || !D3d12Tools::compile(kTableShader, "main", "cs_5_1", code, error, macros)) {
        problems.add(label + ": " + error);
        return false;
    }
    ID3D12PipelineState* pso = D3d12Tools::compute_pso(rig, signature, code, error);
    const std::vector<std::uint8_t> raw = Kit::bytes(count * 13ull + 1, static_cast<std::size_t>(count) * 4);
    ID3D12Resource* data = D3d12Tools::device_buffer(rig, count * 4ull, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* output = D3d12Tools::device_buffer(rig, count * 4ull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRESULT made = S_OK;
    ID3D12DescriptorHeap* staging = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, count, false, &made);
    ID3D12DescriptorHeap* visible = staging != nullptr ? D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap_total, true, &made) : nullptr;
    if (pso == nullptr || data == nullptr || output == nullptr || visible == nullptr) {
        problems.add(label + ": setup failed: " + error + " " + Kit::hr(made));
        return false;
    }
    if (!D3d12Tools::write_buffer(rig, data, D3D12_RESOURCE_STATE_COPY_DEST, 0, raw.data(), raw.size(), error)) {
        problems.add(label + ": " + error);
        return false;
    }
    for (UINT i = 0; i < count; ++i) {
        D3d12Tools::structured_srv(d, data, D3d12Tools::cpu_at(staging, i, increment), i, 1, 4);
    }
    const UINT half = count / 2;
    if (use_copy_multi) {
        const D3D12_CPU_DESCRIPTOR_HANDLE destination_start[1] = {D3d12Tools::cpu_at(visible, base, increment)};
        const UINT destination_size[1] = {count};
        const D3D12_CPU_DESCRIPTOR_HANDLE source_start[2] = {D3d12Tools::cpu_at(staging, half, increment), D3d12Tools::cpu_at(staging, 0, increment)};
        const UINT source_size[2] = {count - half, half};
        d->CopyDescriptors(1, destination_start, destination_size, 2, source_start, source_size, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    } else {
        d->CopyDescriptorsSimple(count, D3d12Tools::cpu_at(visible, base, increment), D3d12Tools::cpu_at(staging, 0, increment), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    std::vector<std::uint8_t> back;
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            D3d12Tools::move(list, data, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ID3D12DescriptorHeap* heaps[] = {visible};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetComputeRootDescriptorTable(0, D3d12Tools::gpu_at(visible, base, increment));
            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
            list->Dispatch((count + 63) / 64, 1, 1);
            D3d12Tools::move(list, data, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        },
        error);
    if (!done || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, count * 4ull, back, error)) {
        problems.add(label + ": " + error);
        return false;
    }
    const std::vector<std::uint32_t> source = Kit::words(raw);
    std::vector<std::uint32_t> expected(count);
    for (UINT i = 0; i < count; ++i) {
        expected[i] = source[use_copy_multi ? (i + half) % count : i] ^ i;
    }
    Kit::compare_words(context, problems, label, expected, Kit::words(back));
    rig.release(visible);
    return true;
}

}  // namespace

Outcome DescChecks::heaps_and_tables(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        const D3D12_RESOURCE_BINDING_TIER tier = Kit::binding_tier(rig);
        context.measure("d3d12.binding_tier", static_cast<int>(tier));
        for (D3D12_DESCRIPTOR_HEAP_TYPE type : {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_DSV}) {
            const UINT increment = d->GetDescriptorHandleIncrementSize(type);
            if (increment == 0) {
                problems.add("a descriptor heap type reports an increment of 0");
                continue;
            }
            const bool visible_capable = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            for (bool shader_visible : {false, true}) {
                if (shader_visible && !visible_capable) {
                    continue;
                }
                HRESULT made = S_OK;
                ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, type, 16, shader_visible, &made);
                if (heap == nullptr) {
                    problems.add("a 16 descriptor heap of type " + std::to_string(static_cast<int>(type)) + (shader_visible ? " (shader visible)" : "") + " failed: " + Kit::hr(made));
                    continue;
                }
                const D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
                if (desc.Type != type || desc.NumDescriptors != 16 || ((desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0) != shader_visible) {
                    problems.add("GetDesc of a descriptor heap does not return the creation description");
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE first = heap->GetCPUDescriptorHandleForHeapStart();
                if (first.ptr == 0) {
                    problems.add("a descriptor heap has a null CPU start handle");
                }
                if (shader_visible && heap->GetGPUDescriptorHandleForHeapStart().ptr == 0) {
                    problems.add("a shader visible descriptor heap has a null GPU start handle");
                }
                if (!shader_visible && heap->GetGPUDescriptorHandleForHeapStart().ptr != 0) {
                    problems.add("a descriptor heap that is not shader visible reports a GPU handle");
                }
                rig.release(heap);
            }
        }
        HRESULT made = S_OK;
        ID3D12DescriptorHeap* big_hidden = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, false, &made);
        if (big_hidden == nullptr) {
            problems.add("a 1,000,000 descriptor heap that is not shader visible failed: " + Kit::hr(made));
        } else {
            rig.release(big_hidden);
        }
        ID3D12DescriptorHeap* samplers = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true, &made);
        if (samplers == nullptr) {
            problems.add("a shader visible sampler heap of 2048 samplers failed: " + Kit::hr(made));
        } else {
            rig.release(samplers);
        }
        const UINT table = tier >= D3D12_RESOURCE_BINDING_TIER_2 ? 4096 : 128;
        run_table(rig, context, problems, "a descriptor table of " + std::to_string(table) + " SRVs filled by CopyDescriptorsSimple", table, 1000000, 1000000 - table, false, false);
        run_table(rig, context, problems, "a descriptor table of " + std::to_string(table) + " SRVs filled by CopyDescriptors with swapped source ranges", table, 200000, 17, false, true);
        if (tier >= D3D12_RESOURCE_BINDING_TIER_3) {
            run_table(rig, context, problems, "an unbounded descriptor table of 4096 SRVs", 4096, 100000, 99000 - 4096, true, false);
        }
        {
            std::string error;
            D3d12Root root;
            root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0).uav(0);
            ID3D12RootSignature* signature = root.build(rig, error);
            ID3D12PipelineState* pso = signature != nullptr ? D3d12Tools::compute_from(rig, signature, kSamplerShader, error) : nullptr;
            ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(2, 1, DXGI_FORMAT_R8_UNORM), D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* output = D3d12Tools::device_buffer(rig, 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ID3D12DescriptorHeap* views = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);
            ID3D12DescriptorHeap* sampler_heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
            if (pso == nullptr || texture == nullptr || output == nullptr || views == nullptr || sampler_heap == nullptr) {
                problems.add("sampler table setup failed: " + error);
                return {};
            }
            const std::uint8_t texels[2] = {51, 255};
            if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, texels, error)) {
                problems.add("uploading the sampled texture failed: " + error);
                return {};
            }
            const UINT view_increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT sampler_increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            d->CreateShaderResourceView(texture, &srv, D3d12Tools::cpu_at(views, 5, view_increment));
            D3D12_SAMPLER_DESC sampler{};
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            d->CreateSampler(&sampler, D3d12Tools::cpu_at(sampler_heap, 2000, sampler_increment));
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            d->CreateSampler(&sampler, D3d12Tools::cpu_at(sampler_heap, 2001, sampler_increment));
            std::vector<std::uint8_t> back;
            const bool done = rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    ID3D12DescriptorHeap* heaps[] = {views, sampler_heap};
                    list->SetDescriptorHeaps(2, heaps);
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(pso);
                    list->SetComputeRootDescriptorTable(0, D3d12Tools::gpu_at(views, 5, view_increment));
                    list->SetComputeRootDescriptorTable(1, D3d12Tools::gpu_at(sampler_heap, 2000, sampler_increment));
                    list->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
                    list->Dispatch(1, 1, 1);
                },
                error);
            if (!done || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 8, back, error)) {
                problems.add("sampling through a sampler table: " + error);
            } else {
                float got[2];
                std::memcpy(got, back.data(), 8);
                if (std::fabs(got[0] - 0.2f) > 0.005f) {
                    problems.add("the point sampler at heap index 2000 returned " + std::to_string(got[0]) + ", expected 0.2");
                }
                if (std::fabs(got[1] - 0.4f) > 0.02f) {
                    problems.add("the linear sampler at heap index 2001 returned " + std::to_string(got[1]) + ", expected 0.4");
                }
            }
        }
        return {};
    });
}

namespace {

const char* kAllBindings =
    "cbuffer Consts : register(b0) { uint4 k; }\n"
    "cbuffer Direct : register(b1) { uint4 d; }\n"
    "StructuredBuffer<uint> rs : register(t0);\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "StructuredBuffer<uint> tb[2] : register(t1);\n"
    "StructuredBuffer<uint> vol : register(t3);\n"
    "Texture2D<float> tex : register(t4);\n"
    "SamplerState ss : register(s0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() {\n"
    "  o[0] = k.x + k.y + k.z + k.w;\n"
    "  o[1] = d.x * 3u + d.w;\n"
    "  o[2] = rs[1];\n"
    "  o[3] = tb[0][0] + tb[1][1] * 10u;\n"
    "  o[4] = vol[2];\n"
    "  o[5] = asuint(tex.SampleLevel(ss, float2(0.25, 0.5), 0));\n"
    "}\n";

D3D12_STATIC_SAMPLER_DESC point_clamp() {
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    return sampler;
}

std::string compare_versions(const D3D12_ROOT_SIGNATURE_DESC1& a, const D3D12_ROOT_SIGNATURE_DESC1& b) {
    if (a.NumParameters != b.NumParameters || a.NumStaticSamplers != b.NumStaticSamplers) {
        return "parameter or static sampler counts differ";
    }
    for (UINT i = 0; i < a.NumParameters; ++i) {
        const D3D12_ROOT_PARAMETER1& x = a.pParameters[i];
        const D3D12_ROOT_PARAMETER1& y = b.pParameters[i];
        if (x.ParameterType != y.ParameterType || x.ShaderVisibility != y.ShaderVisibility) {
            return "parameter " + std::to_string(i) + " differs in type or visibility";
        }
        if (x.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
            (x.Constants.Num32BitValues != y.Constants.Num32BitValues || x.Constants.ShaderRegister != y.Constants.ShaderRegister)) {
            return "root constants of parameter " + std::to_string(i) + " differ";
        }
        if (x.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            if (x.DescriptorTable.NumDescriptorRanges != y.DescriptorTable.NumDescriptorRanges) {
                return "range counts of parameter " + std::to_string(i) + " differ";
            }
            for (UINT r = 0; r < x.DescriptorTable.NumDescriptorRanges; ++r) {
                const D3D12_DESCRIPTOR_RANGE1& p = x.DescriptorTable.pDescriptorRanges[r];
                const D3D12_DESCRIPTOR_RANGE1& q = y.DescriptorTable.pDescriptorRanges[r];
                if (p.RangeType != q.RangeType || p.NumDescriptors != q.NumDescriptors || p.BaseShaderRegister != q.BaseShaderRegister ||
                    p.RegisterSpace != q.RegisterSpace) {
                    return "range " + std::to_string(r) + " of parameter " + std::to_string(i) + " differs";
                }
            }
        } else if (x.ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
                   (x.Descriptor.ShaderRegister != y.Descriptor.ShaderRegister || x.Descriptor.RegisterSpace != y.Descriptor.RegisterSpace)) {
            return "root descriptor " + std::to_string(i) + " differs";
        }
    }
    return {};
}

}  // namespace

Outcome DescChecks::root_signatures(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        std::string error;
        D3D12_DESCRIPTOR_RANGE1 ranges11[3]{};
        ranges11[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0};
        ranges11[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 2};
        ranges11[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 3};
        D3D12_DESCRIPTOR_RANGE ranges10[3]{};
        ranges10[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0};
        ranges10[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 2};
        ranges10[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, 3};
        const D3D12_STATIC_SAMPLER_DESC sampler = point_clamp();
        D3D12_ROOT_PARAMETER1 params11[5]{};
        params11[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params11[0].Constants = {0, 0, 4};
        params11[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params11[1].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
        params11[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params11[2].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
        params11[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params11[3].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
        params11[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params11[4].DescriptorTable = {3, ranges11};
        D3D12_ROOT_PARAMETER params10[5]{};
        params10[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params10[0].Constants = {0, 0, 4};
        params10[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params10[1].Descriptor = {1, 0};
        params10[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params10[2].Descriptor = {0, 0};
        params10[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params10[3].Descriptor = {0, 0};
        params10[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params10[4].DescriptorTable = {3, ranges10};
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc11{};
        desc11.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc11.Desc_1_1 = {5, params11, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc10{};
        desc10.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        desc10.Desc_1_0 = {5, params10, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        Com<ID3DBlob> blob11;
        Com<ID3DBlob> blob10;
        Com<ID3DBlob> messages;
        if (!Kit::succeeds(problems, "D3D12SerializeVersionedRootSignature(1.1)", ::D3D12SerializeVersionedRootSignature(&desc11, blob11.put(), messages.put())) ||
            !Kit::succeeds(problems, "D3D12SerializeVersionedRootSignature(1.0)", ::D3D12SerializeVersionedRootSignature(&desc10, blob10.put(), messages.put()))) {
            return {};
        }
        Com<ID3D12VersionedRootSignatureDeserializer> deserializer11;
        Com<ID3D12VersionedRootSignatureDeserializer> deserializer10;
        if (!Kit::succeeds(problems, "CreateVersionedRootSignatureDeserializer(1.1 blob)",
                           ::D3D12CreateVersionedRootSignatureDeserializer(blob11->GetBufferPointer(), blob11->GetBufferSize(), __uuidof(ID3D12VersionedRootSignatureDeserializer), deserializer11.put_void())) ||
            !Kit::succeeds(problems, "CreateVersionedRootSignatureDeserializer(1.0 blob)",
                           ::D3D12CreateVersionedRootSignatureDeserializer(blob10->GetBufferPointer(), blob10->GetBufferSize(), __uuidof(ID3D12VersionedRootSignatureDeserializer), deserializer10.put_void()))) {
            return {};
        }
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* original = nullptr;
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* as11 = nullptr;
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* as10 = nullptr;
        if (Kit::succeeds(problems, "GetRootSignatureDescAtVersion(1.1) of a 1.1 blob", deserializer11->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &as11)) && as11 != nullptr) {
            const std::string difference = compare_versions(desc11.Desc_1_1, as11->Desc_1_1);
            if (!difference.empty()) {
                problems.add("the 1.1 round trip: " + difference);
            }
            for (UINT r = 0; r < 3; ++r) {
                if (as11->Desc_1_1.pParameters[4].DescriptorTable.pDescriptorRanges[r].Flags != ranges11[r].Flags) {
                    problems.add("the 1.1 round trip changed the flags of range " + std::to_string(r));
                }
                if (as11->Desc_1_1.pParameters[4].DescriptorTable.pDescriptorRanges[r].OffsetInDescriptorsFromTableStart != ranges11[r].OffsetInDescriptorsFromTableStart) {
                    problems.add("the 1.1 round trip changed the offset of range " + std::to_string(r));
                }
            }
        }
        if (Kit::succeeds(problems, "GetRootSignatureDescAtVersion(1.0) of a 1.1 blob", deserializer11->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &original)) && original != nullptr) {
            if (original->Desc_1_0.NumParameters != 5 || original->Desc_1_0.NumStaticSamplers != 1) {
                problems.add("converting a 1.1 root signature to 1.0 changed the parameter or sampler counts");
            }
        }
        if (Kit::succeeds(problems, "GetRootSignatureDescAtVersion(1.1) of a 1.0 blob", deserializer10->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &as10)) && as10 != nullptr) {
            const std::string difference = compare_versions(desc11.Desc_1_1, as10->Desc_1_1);
            if (!difference.empty()) {
                problems.add("converting a 1.0 root signature to 1.1: " + difference);
            }
        }
        Com<ID3DBlob> again;
        if (as11 != nullptr) {
            Kit::succeeds(problems, "serializing the deserialized description", ::D3D12SerializeVersionedRootSignature(as11, again.put(), messages.put()));
        }
        struct Variant {
            const char* name;
            ID3DBlob* blob;
        };
        const Variant variants[] = {{"version 1.0", blob10.get()}, {"version 1.1", blob11.get()}, {"version 1.1 deserialized and serialized again", again.get()}};
        const std::vector<std::uint8_t> constants_data = Kit::bytes(1, 16);
        ID3D12Resource* cbv = D3d12Tools::upload(rig, 256);
        ID3D12Resource* srv_root = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* table_buffer = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* volatile_buffer = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(2, 1, DXGI_FORMAT_R32_FLOAT), D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* output = D3d12Tools::device_buffer(rig, 6 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, true);
        if (cbv == nullptr || srv_root == nullptr || table_buffer == nullptr || volatile_buffer == nullptr || texture == nullptr || output == nullptr || heap == nullptr) {
            problems.add("creating the resources of the binding test failed");
            return {};
        }
        const std::uint32_t cbv_data[4] = {5, 0, 0, 7};
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        cbv->Map(0, &none, &mapped);
        std::memcpy(mapped, cbv_data, sizeof(cbv_data));
        cbv->Unmap(0, nullptr);
        const std::uint32_t root_srv[4] = {10, 20, 30, 40};
        const std::uint32_t tb[4] = {100, 200, 300, 400};
        const std::uint32_t vol[4] = {7, 8, 9, 11};
        const float texels[2] = {0.75f, 0.25f};
        if (!D3d12Tools::write_buffer(rig, srv_root, D3D12_RESOURCE_STATE_COPY_DEST, 0, root_srv, 16, error) ||
            !D3d12Tools::write_buffer(rig, table_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, tb, 16, error) ||
            !D3d12Tools::write_buffer(rig, volatile_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, vol, 16, error) ||
            !D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, texels, error)) {
            problems.add("uploading the binding test data failed: " + error);
            return {};
        }
        const UINT increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3d12Tools::structured_srv(d, table_buffer, D3d12Tools::cpu_at(heap, 0, increment), 0, 4, 4);
        D3d12Tools::structured_srv(d, table_buffer, D3d12Tools::cpu_at(heap, 1, increment), 1, 3, 4);
        D3d12Tools::structured_srv(d, volatile_buffer, D3d12Tools::cpu_at(heap, 2, increment), 0, 4, 4);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        d->CreateShaderResourceView(texture, &srv, D3d12Tools::cpu_at(heap, 3, increment));
        const std::vector<std::uint32_t> expected = {10, 22, 20, 3100, 9, 0x3F400000u};
        D3d12Tools::Bytes code;
        if (!D3d12Tools::compile(kAllBindings, "main", "cs_5_1", code, error)) {
            problems.add(error);
            return {};
        }
        for (const Variant& variant : variants) {
            if (variant.blob == nullptr) {
                continue;
            }
            ID3D12RootSignature* signature = nullptr;
            D3d12Root helper;
            const HRESULT made = helper.build_from(rig, variant.blob->GetBufferPointer(), variant.blob->GetBufferSize(), signature);
            if (!Kit::succeeds(problems, std::string("CreateRootSignature from a ") + variant.name + " blob", made)) {
                continue;
            }
            ID3D12PipelineState* pso = D3d12Tools::compute_pso(rig, signature, code, error);
            if (pso == nullptr) {
                problems.add(std::string(variant.name) + ": " + error);
                continue;
            }
            std::vector<std::uint8_t> back;
            const bool done = rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3D12_RESOURCE_BARRIER to_read[4] = {
                        D3d12Tools::transition(srv_root, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                        D3d12Tools::transition(table_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                        D3d12Tools::transition(volatile_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                        D3d12Tools::transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
                    list->ResourceBarrier(4, to_read);
                    ID3D12DescriptorHeap* heaps[] = {heap};
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(pso);
                    const std::uint32_t values[4] = {1, 2, 3, 4};
                    list->SetComputeRoot32BitConstants(0, 4, values, 0);
                    list->SetComputeRootConstantBufferView(1, cbv->GetGPUVirtualAddress());
                    list->SetComputeRootShaderResourceView(2, srv_root->GetGPUVirtualAddress());
                    list->SetComputeRootUnorderedAccessView(3, output->GetGPUVirtualAddress());
                    list->SetComputeRootDescriptorTable(4, heap->GetGPUDescriptorHandleForHeapStart());
                    list->Dispatch(1, 1, 1);
                    D3D12_RESOURCE_BARRIER back_to_copy[4] = {
                        D3d12Tools::transition(srv_root, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
                        D3d12Tools::transition(table_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
                        D3d12Tools::transition(volatile_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
                        D3d12Tools::transition(texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
                    list->ResourceBarrier(4, back_to_copy);
                },
                error);
            if (!done || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 24, back, error)) {
                problems.add(std::string(variant.name) + ": " + error);
                continue;
            }
            Kit::compare_words(context, problems, std::string("root signature ") + variant.name, expected, Kit::words(back));
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
