#include "sanity/d3d12/draw_checks.hpp"

#include "sanity/d3d12/kit.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kQuadVs =
    "cbuffer C : register(b0) { uint base; uint3 pad; }\n"
    "struct VO { float4 pos : SV_Position; nointerpolation uint idx : IDX; };\n"
    "static const uint order[6] = {0, 1, 2, 2, 1, 3};\n"
    "VO main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "#ifdef INDEXED\n"
    "  uint q = (vid >> 2) + iid + base; uint c = vid & 3u;\n"
    "#else\n"
    "  uint q = iid + base; uint c = order[vid % 6u];\n"
    "#endif\n"
    "  float x = float((q & 3u) * 16u + (c & 1u) * 16u);\n"
    "  float y = float((q >> 2) * 16u + (c >> 1) * 16u);\n"
    "  VO o; o.pos = float4(x / 32.0 - 1.0, 1.0 - y / 32.0, 0, 1); o.idx = q; return o;\n"
    "}\n";

const char* kBufferVs =
    "struct VI { float2 p : POSITION; uint idx : IDX; };\n"
    "struct VO { float4 pos : SV_Position; nointerpolation uint idx : IDX; };\n"
    "VO main(VI i) { VO o; o.pos = float4(i.p, 0, 1); o.idx = i.idx; return o; }\n";

const char* kQuadPs =
    "struct VO { float4 pos : SV_Position; nointerpolation uint idx : IDX; };\n"
    "float4 main(VO i) : SV_Target { return float4(float(i.idx * 16u) / 255.0, float(255u - i.idx * 16u) / 255.0, float(i.idx) / 255.0, 1.0); }\n";

const char* kDispatchCs =
    "cbuffer C : register(b0) { uint base; uint3 pad; }\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[base + id.x] = (base + id.x) * 3u + 1u; }\n";

const char* kTriangleVs =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p[3] = {float2(-0.9, 0.8), float2(0.85, -0.3), float2(-0.2, -0.95)};\n"
    "  return float4(p[id], 0, 1);\n"
    "}\n";

const char* kYellowPs = "float4 main() : SV_Target { return float4(1.0, 1.0, 0.0, 1.0); }\n";

constexpr UINT kEdge = 64;

std::vector<std::uint32_t> quad_image(const std::vector<bool>& drawn) {
    std::vector<std::uint32_t> image(kEdge * kEdge, 0);
    for (UINT y = 0; y < kEdge; ++y) {
        for (UINT x = 0; x < kEdge; ++x) {
            const UINT q = (y / 16) * 4 + x / 16;
            if (drawn[q]) {
                image[y * kEdge + x] = Kit::rgba(static_cast<int>(q * 16), static_cast<int>(255 - q * 16), static_cast<int>(q), 255);
            }
        }
    }
    return image;
}

std::vector<bool> first_quads(UINT first, UINT count) {
    std::vector<bool> drawn(16, false);
    for (UINT q = first; q < first + count && q < 16; ++q) {
        drawn[q] = true;
    }
    return drawn;
}

#pragma pack(push, 1)
struct ConstantDraw {
    UINT base;
    D3D12_DRAW_ARGUMENTS draw;
};

struct ConstantBufferDraw {
    D3D12_GPU_VIRTUAL_ADDRESS address;
    D3D12_DRAW_ARGUMENTS draw;
};

struct InputDraw {
    D3D12_VERTEX_BUFFER_VIEW vertices;
    D3D12_INDEX_BUFFER_VIEW indices;
    D3D12_DRAW_INDEXED_ARGUMENTS draw;
};

struct ConstantDispatch {
    UINT base;
    D3D12_DISPATCH_ARGUMENTS dispatch;
};
#pragma pack(pop)

D3D12_INDIRECT_ARGUMENT_DESC argument(D3D12_INDIRECT_ARGUMENT_TYPE type) {
    D3D12_INDIRECT_ARGUMENT_DESC desc{};
    desc.Type = type;
    return desc;
}

D3D12_INDIRECT_ARGUMENT_DESC root_argument(D3D12_INDIRECT_ARGUMENT_TYPE type, UINT index) {
    D3D12_INDIRECT_ARGUMENT_DESC desc{};
    desc.Type = type;
    if (type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT) {
        desc.Constant.RootParameterIndex = index;
        desc.Constant.DestOffsetIn32BitValues = 0;
        desc.Constant.Num32BitValuesToSet = 1;
    } else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW) {
        desc.ConstantBufferView.RootParameterIndex = index;
    } else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW) {
        desc.ShaderResourceView.RootParameterIndex = index;
    } else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW) {
        desc.UnorderedAccessView.RootParameterIndex = index;
    }
    return desc;
}

ID3D12CommandSignature* make_signature(D3d12Rig& rig, Problems& problems, const std::string& label, UINT stride,
                                       const std::vector<D3D12_INDIRECT_ARGUMENT_DESC>& arguments, ID3D12RootSignature* root) {
    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = stride;
    desc.NumArgumentDescs = static_cast<UINT>(arguments.size());
    desc.pArgumentDescs = arguments.data();
    Com<ID3D12CommandSignature> signature;
    const HRESULT result = rig.device()->CreateCommandSignature(&desc, root, __uuidof(ID3D12CommandSignature), signature.put_void());
    if (FAILED(result)) {
        problems.add("CreateCommandSignature for " + label + " returned " + Kit::hr(result) + "; ExecuteIndirect supports it per the D3D12 documentation");
        return nullptr;
    }
    return rig.hold(std::move(signature));
}

}  // namespace

Outcome DrawChecks::draw_variants(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Root constants_root;
        constants_root.constants(4, 0);
        ID3D12RootSignature* constants_signature = constants_root.build(rig, error);
        D3d12Root cbv_root;
        cbv_root.cbv(0);
        ID3D12RootSignature* cbv_signature = cbv_root.build(rig, error);
        D3d12Root input_root;
        input_root.flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* input_signature = input_root.build(rig, error);
        if (constants_signature == nullptr || cbv_signature == nullptr || input_signature == nullptr) {
            problems.add(error);
            return {};
        }
        const D3D_SHADER_MACRO indexed_macros[] = {{"INDEXED", "1"}, {nullptr, nullptr}};
        D3d12Tools::Bytes vs_plain;
        D3d12Tools::Bytes vs_indexed;
        D3d12Tools::Bytes vs_buffer;
        D3d12Tools::Bytes ps;
        if (!D3d12Tools::compile(kQuadVs, "main", "vs_5_1", vs_plain, error) || !D3d12Tools::compile(kQuadVs, "main", "vs_5_1", vs_indexed, error, indexed_macros) ||
            !D3d12Tools::compile(kBufferVs, "main", "vs_5_1", vs_buffer, error) || !D3d12Tools::compile(kQuadPs, "main", "ps_5_1", ps, error)) {
            problems.add(error);
            return {};
        }
        auto make = [&](ID3D12RootSignature* root, const D3d12Tools::Bytes& vs, const std::vector<D3D12_INPUT_ELEMENT_DESC>& layout) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(root, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
            desc.InputLayout = {layout.empty() ? nullptr : layout.data(), static_cast<UINT>(layout.size())};
            return D3d12Tools::graphics_pso(rig, desc, error);
        };
        const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"IDX", 0, DXGI_FORMAT_R32_UINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        ID3D12PipelineState* plain = make(constants_signature, vs_plain, {});
        ID3D12PipelineState* indexed = make(constants_signature, vs_indexed, {});
        ID3D12PipelineState* cbv_plain = make(cbv_signature, vs_plain, {});
        ID3D12PipelineState* from_buffers = make(input_signature, vs_buffer, layout);
        if (plain == nullptr || indexed == nullptr || cbv_plain == nullptr || from_buffers == nullptr) {
            problems.add("creating the pipelines failed: " + error);
            return {};
        }
        const std::uint16_t index_data[8] = {0, 1, 2, 2, 1, 3, 0, 0};
        std::vector<D3D12_DRAW_INDEXED_ARGUMENTS> indexed_arguments(16);
        std::vector<ConstantDraw> constant_arguments(16);
        std::vector<ConstantBufferDraw> cbv_arguments(16);
        std::vector<InputDraw> input_arguments(16);
        std::vector<float> vertex_data;
        for (UINT q = 0; q < 16; ++q) {
            indexed_arguments[q] = {6, 1, 0, static_cast<INT>(4 * q), 0};
            constant_arguments[q] = {q, {6, 1, 0, 0}};
            for (UINT c = 0; c < 4; ++c) {
                const float x = static_cast<float>((q & 3u) * 16u + (c & 1u) * 16u) / 32.0f - 1.0f;
                const float y = 1.0f - static_cast<float>((q >> 2) * 16u + (c >> 1) * 16u) / 32.0f;
                vertex_data.push_back(x);
                vertex_data.push_back(y);
                std::uint32_t index_bits = q;
                float index_as_float = 0;
                std::memcpy(&index_as_float, &index_bits, 4);
                vertex_data.push_back(index_as_float);
            }
        }
        const UINT64 vertex_bytes = vertex_data.size() * 4;
        const std::uint32_t counts[4] = {10, 4, 0, 0};
        const std::vector<D3D12_DRAW_ARGUMENTS> instanced_arguments = {{6, 16, 0, 0}};
        ID3D12Resource* index_buffer = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* vertex_buffer = D3d12Tools::device_buffer(rig, vertex_bytes, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* indexed_buffer = D3d12Tools::device_buffer(rig, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* instanced_buffer = D3d12Tools::device_buffer(rig, sizeof(D3D12_DRAW_ARGUMENTS), D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* constant_buffer = D3d12Tools::device_buffer(rig, sizeof(ConstantDraw) * 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* count_buffer = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* cbv_buffer = D3d12Tools::upload(rig, 16 * 256);
        ID3D12Resource* cbv_args = D3d12Tools::device_buffer(rig, sizeof(ConstantBufferDraw) * 16, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* input_args = D3d12Tools::device_buffer(rig, sizeof(InputDraw) * 16, D3D12_RESOURCE_STATE_COPY_DEST);
        if (index_buffer == nullptr || vertex_buffer == nullptr || indexed_buffer == nullptr || instanced_buffer == nullptr || constant_buffer == nullptr ||
            count_buffer == nullptr || cbv_buffer == nullptr || cbv_args == nullptr || input_args == nullptr) {
            problems.add("creating the argument buffers failed");
            return {};
        }
        {
            void* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            cbv_buffer->Map(0, &none, &mapped);
            std::memset(mapped, 0, 16 * 256);
            for (UINT q = 0; q < 16; ++q) {
                std::memcpy(static_cast<std::uint8_t*>(mapped) + q * 256, &q, 4);
                cbv_arguments[q] = {cbv_buffer->GetGPUVirtualAddress() + q * 256, {6, 1, 0, 0}};
            }
            cbv_buffer->Unmap(0, nullptr);
            for (UINT q = 0; q < 16; ++q) {
                input_arguments[q].vertices = {vertex_buffer->GetGPUVirtualAddress() + q * 48, 48, 12};
                input_arguments[q].indices = {index_buffer->GetGPUVirtualAddress(), 12, DXGI_FORMAT_R16_UINT};
                input_arguments[q].draw = {6, 1, 0, 0, 0};
            }
        }
        if (!D3d12Tools::write_buffer(rig, index_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, index_data, 16, error) ||
            !D3d12Tools::write_buffer(rig, vertex_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, vertex_data.data(), vertex_bytes, error) ||
            !D3d12Tools::write_buffer(rig, indexed_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, indexed_arguments.data(), indexed_arguments.size() * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), error) ||
            !D3d12Tools::write_buffer(rig, instanced_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, instanced_arguments.data(), sizeof(D3D12_DRAW_ARGUMENTS), error) ||
            !D3d12Tools::write_buffer(rig, constant_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, constant_arguments.data(), constant_arguments.size() * sizeof(ConstantDraw), error) ||
            !D3d12Tools::write_buffer(rig, count_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, counts, 16, error) ||
            !D3d12Tools::write_buffer(rig, cbv_args, D3D12_RESOURCE_STATE_COPY_DEST, 0, cbv_arguments.data(), cbv_arguments.size() * sizeof(ConstantBufferDraw), error) ||
            !D3d12Tools::write_buffer(rig, input_args, D3D12_RESOURCE_STATE_COPY_DEST, 0, input_arguments.data(), input_arguments.size() * sizeof(InputDraw), error)) {
            problems.add("uploading the draw data failed: " + error);
            return {};
        }
        const bool prepared = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                D3D12_RESOURCE_BARRIER barriers[8] = {
                    D3d12Tools::transition(index_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER),
                    D3d12Tools::transition(vertex_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
                    D3d12Tools::transition(indexed_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    D3d12Tools::transition(instanced_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    D3d12Tools::transition(constant_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    D3d12Tools::transition(count_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    D3d12Tools::transition(cbv_args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    D3d12Tools::transition(input_args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)};
                list->ResourceBarrier(8, barriers);
            },
            error);
        if (!prepared) {
            problems.add("transitioning the argument buffers failed: " + error);
            return {};
        }
        const D3D12_INDEX_BUFFER_VIEW index_view{index_buffer->GetGPUVirtualAddress(), 12, DXGI_FORMAT_R16_UINT};
        auto scene = [&](const std::string& label, const std::vector<bool>& drawn, const std::function<void(ID3D12GraphicsCommandList*)>& body) {
            D3d12Frame frame;
            TexelImage image;
            std::string local;
            if (!frame.open(rig, kEdge, kEdge, local) ||
                !frame.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        frame.clear(list, 0, 0, 0, 0);
                        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        body(list);
                    },
                    local) ||
                !frame.read(image, local)) {
                problems.add(label + ": " + local);
                return;
            }
            Kit::compare_words(context, problems, label, quad_image(drawn), Kit::image_words(image));
        };
        const std::vector<bool> all = first_quads(0, 16);
        const auto plain_state = [&](ID3D12GraphicsCommandList* list, ID3D12PipelineState* state, UINT base) {
            const UINT values[4] = {base, 0, 0, 0};
            list->SetGraphicsRootSignature(constants_signature);
            list->SetPipelineState(state);
            list->SetGraphicsRoot32BitConstants(0, 4, values, 0);
        };
        scene("DrawInstanced of 16 instances", all, [&](ID3D12GraphicsCommandList* list) {
            plain_state(list, plain, 0);
            list->DrawInstanced(6, 16, 0, 0);
        });
        scene("DrawIndexedInstanced of 16 instances", all, [&](ID3D12GraphicsCommandList* list) {
            plain_state(list, indexed, 0);
            list->IASetIndexBuffer(&index_view);
            list->DrawIndexedInstanced(6, 16, 0, 0, 0);
        });
        scene("sixteen DrawIndexedInstanced calls with base vertices", all, [&](ID3D12GraphicsCommandList* list) {
            plain_state(list, indexed, 0);
            list->IASetIndexBuffer(&index_view);
            for (UINT q = 0; q < 16; ++q) {
                list->DrawIndexedInstanced(6, 1, 0, static_cast<INT>(4 * q), 0);
            }
        });
        scene("sixteen DrawInstanced calls with root constants", all, [&](ID3D12GraphicsCommandList* list) {
            for (UINT q = 0; q < 16; ++q) {
                plain_state(list, plain, q);
                list->DrawInstanced(6, 1, 0, 0);
            }
        });
        ID3D12CommandSignature* draw_signature = make_signature(rig, problems, "DRAW", sizeof(D3D12_DRAW_ARGUMENTS), {argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW)}, nullptr);
        ID3D12CommandSignature* indexed_signature =
            make_signature(rig, problems, "DRAW_INDEXED", sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), {argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED)}, nullptr);
        if (draw_signature != nullptr) {
            scene("ExecuteIndirect DRAW with 16 instances", all, [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, plain, 0);
                list->ExecuteIndirect(draw_signature, 1, instanced_buffer, 0, nullptr, 0);
            });
            scene("ExecuteIndirect DRAW with a count of 0", first_quads(0, 0), [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, plain, 0);
                list->ExecuteIndirect(draw_signature, 1, instanced_buffer, 0, count_buffer, 8);
            });
        }
        if (indexed_signature != nullptr) {
            scene("ExecuteIndirect DRAW_INDEXED with 16 commands", all, [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, indexed, 0);
                list->IASetIndexBuffer(&index_view);
                list->ExecuteIndirect(indexed_signature, 16, indexed_buffer, 0, nullptr, 0);
            });
            scene("ExecuteIndirect DRAW_INDEXED with a count buffer of 10", first_quads(0, 10), [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, indexed, 0);
                list->IASetIndexBuffer(&index_view);
                list->ExecuteIndirect(indexed_signature, 16, indexed_buffer, 0, count_buffer, 0);
            });
            scene("ExecuteIndirect DRAW_INDEXED with a count buffer offset (4 commands)", first_quads(0, 4), [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, indexed, 0);
                list->IASetIndexBuffer(&index_view);
                list->ExecuteIndirect(indexed_signature, 8, indexed_buffer, 0, count_buffer, 4);
            });
            scene("ExecuteIndirect DRAW_INDEXED from an argument buffer offset", first_quads(5, 3), [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, indexed, 0);
                list->IASetIndexBuffer(&index_view);
                list->ExecuteIndirect(indexed_signature, 3, indexed_buffer, 5 * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), nullptr, 0);
            });
        }
        ID3D12CommandSignature* constant_signature = make_signature(
            rig, problems, "CONSTANT followed by DRAW", sizeof(ConstantDraw), {root_argument(D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0), argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW)},
            constants_signature);
        if (constant_signature != nullptr) {
            scene("ExecuteIndirect CONSTANT and DRAW", all, [&](ID3D12GraphicsCommandList* list) {
                plain_state(list, plain, 0);
                list->ExecuteIndirect(constant_signature, 16, constant_buffer, 0, nullptr, 0);
            });
        }
        ID3D12CommandSignature* cbv_signature_indirect =
            make_signature(rig, problems, "CONSTANT_BUFFER_VIEW followed by DRAW", sizeof(ConstantBufferDraw),
                           {root_argument(D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, 0), argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW)}, cbv_signature);
        if (cbv_signature_indirect != nullptr) {
            scene("ExecuteIndirect CONSTANT_BUFFER_VIEW and DRAW", all, [&](ID3D12GraphicsCommandList* list) {
                list->SetGraphicsRootSignature(cbv_signature);
                list->SetPipelineState(cbv_plain);
                list->ExecuteIndirect(cbv_signature_indirect, 16, cbv_args, 0, nullptr, 0);
            });
        }
        D3D12_INDIRECT_ARGUMENT_DESC vertex_argument = argument(D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW);
        vertex_argument.VertexBuffer.Slot = 0;
        ID3D12CommandSignature* input_signature_indirect =
            make_signature(rig, problems, "VERTEX_BUFFER_VIEW, INDEX_BUFFER_VIEW and DRAW_INDEXED", sizeof(InputDraw),
                           {vertex_argument, argument(D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW), argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED)}, nullptr);
        if (input_signature_indirect != nullptr) {
            scene("ExecuteIndirect with vertex and index buffer views", all, [&](ID3D12GraphicsCommandList* list) {
                list->SetGraphicsRootSignature(input_signature);
                list->SetPipelineState(from_buffers);
                list->ExecuteIndirect(input_signature_indirect, 16, input_args, 0, nullptr, 0);
            });
        }
        D3d12Root srv_root;
        srv_root.srv(0);
        ID3D12RootSignature* srv_signature = srv_root.build(rig, error);
        D3d12Root uav_root;
        uav_root.constants(4, 0).uav(0);
        ID3D12RootSignature* uav_signature = uav_root.build(rig, error);
        if (srv_signature != nullptr) {
            static_cast<void>(make_signature(rig, problems, "SHADER_RESOURCE_VIEW followed by DRAW", 8 + sizeof(D3D12_DRAW_ARGUMENTS),
                                             {root_argument(D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW, 0), argument(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW)}, srv_signature));
        }
        ID3D12PipelineState* dispatch_pso = uav_signature != nullptr ? D3d12Tools::compute_from(rig, uav_signature, kDispatchCs, error) : nullptr;
        if (dispatch_pso == nullptr) {
            problems.add("creating the dispatch pipeline failed: " + error);
            return {};
        }
        ID3D12CommandSignature* dispatch_signature = make_signature(rig, problems, "DISPATCH", sizeof(D3D12_DISPATCH_ARGUMENTS), {argument(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH)}, nullptr);
        ID3D12CommandSignature* constant_dispatch = make_signature(
            rig, problems, "CONSTANT followed by DISPATCH", sizeof(ConstantDispatch),
            {root_argument(D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, 0), argument(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH)}, uav_signature);
        std::vector<ConstantDispatch> dispatch_arguments(4);
        for (UINT k = 0; k < 4; ++k) {
            dispatch_arguments[k] = {k * 64, {1, 1, 1}};
        }
        const D3D12_DISPATCH_ARGUMENTS two_groups{2, 1, 1};
        const std::uint32_t dispatch_counts[2] = {3, 0};
        ID3D12Resource* dispatch_buffer = D3d12Tools::device_buffer(rig, sizeof(ConstantDispatch) * 4, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* plain_dispatch = D3d12Tools::device_buffer(rig, sizeof(two_groups), D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* dispatch_count = D3d12Tools::device_buffer(rig, 8, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* output = D3d12Tools::device_buffer(rig, 256 * 4, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (dispatch_buffer == nullptr || plain_dispatch == nullptr || dispatch_count == nullptr || output == nullptr ||
            !D3d12Tools::write_buffer(rig, dispatch_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, dispatch_arguments.data(), sizeof(ConstantDispatch) * 4, error) ||
            !D3d12Tools::write_buffer(rig, plain_dispatch, D3D12_RESOURCE_STATE_COPY_DEST, 0, &two_groups, sizeof(two_groups), error) ||
            !D3d12Tools::write_buffer(rig, dispatch_count, D3D12_RESOURCE_STATE_COPY_DEST, 0, dispatch_counts, 8, error)) {
            problems.add("creating the dispatch buffers failed: " + error);
            return {};
        }
        auto dispatch_scene = [&](const std::string& label, ID3D12CommandSignature* signature, ID3D12Resource* arguments, ID3D12Resource* count, UINT max_count,
                                  UINT written) {
            if (signature == nullptr) {
                return;
            }
            std::vector<std::uint8_t> back;
            std::string local;
            const bool done = rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    D3d12Tools::move(list, arguments, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    D3d12Tools::move(list, dispatch_count, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    list->SetComputeRootSignature(uav_signature);
                    list->SetPipelineState(dispatch_pso);
                    const UINT zero[4] = {0, 0, 0, 0};
                    list->SetComputeRoot32BitConstants(0, 4, zero, 0);
                    list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
                    list->ExecuteIndirect(signature, max_count, arguments, 0, count, 0);
                    D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    D3d12Tools::move(list, arguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                    D3d12Tools::move(list, dispatch_count, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                },
                local);
            if (!done || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_COPY_SOURCE, 0, 256 * 4, back, local)) {
                problems.add(label + ": " + local);
                return;
            }
            std::vector<std::uint32_t> expected(256, 0);
            for (UINT i = 0; i < written; ++i) {
                expected[i] = i * 3u + 1u;
            }
            Kit::compare_words(context, problems, label, expected, Kit::words(back));
            const std::vector<std::uint8_t> zeros(1024, 0);
            if (!D3d12Tools::write_buffer(rig, output, D3D12_RESOURCE_STATE_COPY_SOURCE, 0, zeros.data(), zeros.size(), local) ||
                !rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST); }, local)) {
                problems.add(label + ": resetting the output failed: " + local);
            }
        };
        dispatch_scene("ExecuteIndirect DISPATCH", dispatch_signature, plain_dispatch, nullptr, 1, 128);
        dispatch_scene("ExecuteIndirect CONSTANT and DISPATCH with a count buffer of 3", constant_dispatch, dispatch_buffer, dispatch_count, 4, 192);
        return {};
    });
}

Outcome DrawChecks::msaa_resolve(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    D3d12Oracle oracle;
    if (!oracle.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = 4;
    if (FAILED(oracle.av.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) || levels.NumQualityLevels == 0) {
        return Context::skip("4x multisampling is not reported for R8G8B8A8_UNORM");
    }
    Problems problems;
    const bool programmable_flag = [&] {
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
        return SUCCEEDED(D3d12Tools::feature(oracle.av.device(), D3D12_FEATURE_D3D12_OPTIONS2, options)) &&
               options.ProgrammableSamplePositionsTier != D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED;
    }();
    const D3D12_SAMPLE_POSITION positions[4] = {{-2, -2}, {2, -2}, {-2, 2}, {2, 2}};
    auto render = [&](D3d12Rig& rig, SceneImage& out, std::string& scene_error, bool custom_positions) -> bool {
        D3d12Root empty;
        ID3D12RootSignature* signature = empty.build(rig, scene_error);
        ID3D12PipelineState* state = signature != nullptr ? Kit::pipeline_with(rig, signature, kTriangleVs, kYellowPs, DXGI_FORMAT_R8G8B8A8_UNORM, scene_error,
                                                                                [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { desc.SampleDesc.Count = 4; }) : nullptr;
        ID3D12Resource* target = D3d12Tools::texture(rig, D3d12Tools::texture_desc(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 1, 4),
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* resolved = D3d12Tools::texture(rig, D3d12Tools::texture_desc(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM), D3D12_RESOURCE_STATE_RESOLVE_DEST);
        ID3D12DescriptorHeap* rtv = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
        if (state == nullptr || target == nullptr || resolved == nullptr || rtv == nullptr) {
            scene_error = "setup failed: " + scene_error;
            return false;
        }
        rig.device()->CreateRenderTargetView(target, nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        const float blue[4] = {0, 0, 1, 1};
        const D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv->GetCPUDescriptorHandleForHeapStart();
        bool list1_available = true;
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    Com<ID3D12GraphicsCommandList1> list1;
                    list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), list1.put_void());
                    if (custom_positions) {
                        if (!list1) {
                            list1_available = false;
                            return;
                        }
                        D3D12_SAMPLE_POSITION copy[4];
                        std::memcpy(copy, positions, sizeof(copy));
                        list1->SetSamplePositions(4, 1, copy);
                    }
                    list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
                    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(kEdge), static_cast<float>(kEdge), 0, 1};
                    const D3D12_RECT scissor{0, 0, static_cast<LONG>(kEdge), static_cast<LONG>(kEdge)};
                    list->RSSetViewports(1, &viewport);
                    list->RSSetScissorRects(1, &scissor);
                    list->ClearRenderTargetView(handle, blue, 0, nullptr);
                    list->SetGraphicsRootSignature(signature);
                    list->SetPipelineState(state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                    D3d12Tools::move(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                    list->ResolveSubresource(resolved, 0, target, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                    if (custom_positions && list1) {
                        list1->SetSamplePositions(0, 0, nullptr);
                    }
                },
                scene_error) ||
            !list1_available) {
            scene_error = "rendering the multisampled triangle failed: " + scene_error;
            return false;
        }
        TexelImage image;
        if (!D3d12Tools::read_texture(rig, resolved, D3D12_RESOURCE_STATE_RESOLVE_DEST, 0, image, scene_error)) {
            return false;
        }
        out.width = kEdge;
        out.height = kEdge;
        out.rgba = true;
        out.data = std::move(image.data);
        return true;
    };
    oracle.compare(context, problems, "4x multisampled triangle resolve", [&](D3d12Rig& rig, SceneImage& out, std::string& scene_error) { return render(rig, out, scene_error, false); },
                   Tolerance::bytes(2));
    if (!programmable_flag) {
        context.note("programmable sample positions are not reported; only the default positions are compared");
        return problems.outcome();
    }
    SceneImage custom;
    if (!render(oracle.av, custom, error, true)) {
        problems.add("programmable sample positions: " + error);
        return problems.outcome();
    }
    const double vertices[3][2] = {{-0.9, 0.8}, {0.85, -0.3}, {-0.2, -0.95}};
    double px[3];
    double py[3];
    for (int i = 0; i < 3; ++i) {
        px[i] = (vertices[i][0] * 0.5 + 0.5) * kEdge;
        py[i] = (0.5 - vertices[i][1] * 0.5) * kEdge;
    }
    auto edge = [&](int a, int b, double x, double y) { return (px[b] - px[a]) * (y - py[a]) - (py[b] - py[a]) * (x - px[a]); };
    const double area = edge(0, 1, px[2], py[2]);
    int checked = 0;
    for (UINT y = 0; y < kEdge && problems.empty(); ++y) {
        for (UINT x = 0; x < kEdge; ++x) {
            int covered = 0;
            bool ambiguous = false;
            for (const D3D12_SAMPLE_POSITION& position : positions) {
                const double sx = x + 0.5 + position.X / 16.0;
                const double sy = y + 0.5 + position.Y / 16.0;
                const double e0 = edge(0, 1, sx, sy) * (area < 0 ? -1 : 1);
                const double e1 = edge(1, 2, sx, sy) * (area < 0 ? -1 : 1);
                const double e2 = edge(2, 0, sx, sy) * (area < 0 ? -1 : 1);
                if (std::fabs(e0) < 1e-3 || std::fabs(e1) < 1e-3 || std::fabs(e2) < 1e-3) {
                    ambiguous = true;
                }
                if (e0 > 0 && e1 > 0 && e2 > 0) {
                    ++covered;
                }
            }
            if (ambiguous) {
                continue;
            }
            ++checked;
            const double fraction = covered / 4.0;
            const int red = static_cast<int>(std::lround(255.0 * fraction));
            const int blue = static_cast<int>(std::lround(255.0 * (1.0 - fraction)));
            const std::uint8_t* pixel = custom.data.data() + (static_cast<std::size_t>(y) * kEdge + x) * 4;
            if (std::abs(pixel[0] - red) > 1 || std::abs(pixel[1] - red) > 1 || std::abs(pixel[2] - blue) > 1 || pixel[3] != 255) {
                problems.add("pixel (" + std::to_string(x) + "," + std::to_string(y) + ") with programmable positions is (" + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) +
                             "," + std::to_string(pixel[2]) + "," + std::to_string(pixel[3]) + "), analytic coverage " + std::to_string(covered) + "/4 gives red " + std::to_string(red));
                break;
            }
        }
    }
    context.measure("d3d12.msaa_pixels_checked", checked);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d12
