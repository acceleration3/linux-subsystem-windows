#include "sanity/shader/dxil_checks.hpp"

#include "core/text.hpp"
#include "sanity/shader/kit.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_frame.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/dxc_compiler.hpp"

#include <cstring>

namespace lsw::suites::sanity::shader {

namespace {

constexpr int kFirstModel = 0x60;
constexpr int kLastModel = 0x68;

std::string model_text(int model) { return "6." + std::to_string(model - 0x60); }

const wchar_t* compute_target(int model) {
    static const wchar_t* names[] = {L"cs_6_0", L"cs_6_1", L"cs_6_2", L"cs_6_3", L"cs_6_4", L"cs_6_5", L"cs_6_6", L"cs_6_7", L"cs_6_8"};
    return names[model - kFirstModel];
}

const wchar_t* vertex_target(int model) {
    static const wchar_t* names[] = {L"vs_6_0", L"vs_6_1", L"vs_6_2", L"vs_6_3", L"vs_6_4", L"vs_6_5", L"vs_6_6", L"vs_6_7", L"vs_6_8"};
    return names[model - kFirstModel];
}

const wchar_t* pixel_target(int model) {
    static const wchar_t* names[] = {L"ps_6_0", L"ps_6_1", L"ps_6_2", L"ps_6_3", L"ps_6_4", L"ps_6_5", L"ps_6_6", L"ps_6_7", L"ps_6_8"};
    return names[model - kFirstModel];
}

const char* kComputeBody =
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(128, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint v = id.x * 3u + 1u;\n"
    "  uint r = v ^ (v >> 3);\n"
    "#if WAVE\n"
    "  uint lanes = WaveGetLaneCount(); uint sum = WaveActiveSum(1u); uint index = WaveGetLaneIndex(); uint prefix = WavePrefixSum(1u);\n"
    "  r += (sum == lanes ? 0u : 100000u) + (prefix == index ? 0u : 200000u);\n"
    "#endif\n"
    "  o[id.x] = r;\n"
    "}\n";

const char* kVertex = "float4 main(uint id : SV_VertexID) : SV_Position {\n  float2 p = float2((id << 1) & 2, id & 2);\n  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n}\n";

const char* kPixel =
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "  uint v = uint(p.x) * 7u + uint(p.y) * 13u;\n"
    "  return float4(float(v & 255u) / 255.0, float((v >> 8) & 255u) / 255.0, 0.25, 1.0);\n"
    "}\n";

}  // namespace

Outcome DxilChecks::sm6(Context& context) {
    if (!Kit::enabled("d3d12")) {
        return Context::skip("D3D12 excluded by LSW_SUITES_SKIP_APIS");
    }
    D3d12Rig rig;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int highest = 0;
    for (int model = kLastModel; model >= kFirstModel && highest == 0; --model) {
        D3D12_FEATURE_DATA_SHADER_MODEL data{};
        data.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(model);
        const HRESULT result = rig.device()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data));
        if (SUCCEEDED(result)) {
            highest = static_cast<int>(data.HighestShaderModel);
        } else if (result != E_INVALIDARG) {
            problems.add("CheckFeatureSupport(SHADER_MODEL) with " + model_text(model) + " returned " + D3d12Tools::hr(result) + ", expected S_OK or E_INVALIDARG");
        }
    }
    for (int model = kFirstModel; model <= kLastModel; ++model) {
        D3D12_FEATURE_DATA_SHADER_MODEL data{};
        data.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(model);
        const HRESULT result = rig.device()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data));
        if (FAILED(result)) {
            if (result != E_INVALIDARG) {
                problems.add("CheckFeatureSupport(SHADER_MODEL) with " + model_text(model) + " returned " + D3d12Tools::hr(result));
            } else if (model <= highest) {
                problems.add("CheckFeatureSupport refuses shader model " + model_text(model) + " with E_INVALIDARG although " + model_text(highest) + " is reported");
            }
            continue;
        }
        const int got = static_cast<int>(data.HighestShaderModel);
        if (got > model || (model >= highest && got != highest)) {
            problems.add("CheckFeatureSupport asked about " + model_text(model) + " reports " + model_text(got) + " while the highest supported is " + model_text(highest));
        }
    }
    {
        D3D12_FEATURE_DATA_SHADER_MODEL data{};
        data.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x7F);
        const HRESULT result = rig.device()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data));
        if (result != E_INVALIDARG) {
            problems.add("CheckFeatureSupport with an unknown shader model 0x7F returned " + D3d12Tools::hr(result) + ", expected E_INVALIDARG");
        }
    }
    context.measure("dxil.highest_shader_model", model_text(std::max(highest, kFirstModel)));
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (highest < kFirstModel) {
        return Context::skip("the device reports no shader model 6.x");
    }
    DxcCompiler dxc;
    if (!dxc.open(error)) {
        return Context::skip("DXIL cannot be produced: " + error);
    }
    HMODULE validator = ::LoadLibraryW(L"dxil.dll");
    if (validator == nullptr) {
        return Context::skip("dxil.dll, the DXIL validator that signs compiled shaders, cannot be loaded");
    }
    ::FreeLibrary(validator);
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    rig.device()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
    const bool waves = options1.WaveOps != FALSE && options1.WaveLaneCountMax <= 128;
    D3d12Root compute_root;
    compute_root.uav(0);
    ID3D12RootSignature* compute_signature = compute_root.build(rig, error);
    D3d12Root graphics_root;
    ID3D12RootSignature* graphics_signature = graphics_root.build(rig, error);
    if (compute_signature == nullptr || graphics_signature == nullptr) {
        return Outcome::fail("root signatures: " + error);
    }
    const int tested_limit = std::min(highest, 0x66);
    for (int model = kFirstModel; model <= tested_limit; ++model) {
        const std::string name = "shader model " + model_text(model);
        std::vector<std::uint8_t> code;
        std::string log;
        const std::string source = std::string("#define WAVE ") + (waves ? "1" : "0") + "\n" + kComputeBody;
        if (!dxc.compile(source, L"main", compute_target(model), code, log)) {
            problems.add(name + " compute shader does not compile although the device reports it: " + log);
            continue;
        }
        ID3D12PipelineState* state = D3d12Tools::compute_pso(rig, compute_signature, code, error);
        ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, 1024 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (state == nullptr || buffer == nullptr) {
            problems.add(name + " compute pipeline: " + error);
            continue;
        }
        std::vector<std::uint32_t> words;
        const bool ran = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                list->SetComputeRootSignature(compute_signature);
                list->SetPipelineState(state);
                list->SetComputeRootUnorderedAccessView(0, buffer->GetGPUVirtualAddress());
                list->Dispatch(8, 1, 1);
            },
            error);
        if (!ran || !D3d12Tools::read_words(rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1024, words, error)) {
            problems.add(name + " compute dispatch: " + error);
            continue;
        }
        for (std::uint32_t i = 0; i < 1024; ++i) {
            const std::uint32_t v = i * 3u + 1u;
            if (words[i] != (v ^ (v >> 3))) {
                problems.add(name + " compute shader (" + (waves ? "with wave intrinsics" : "without wave intrinsics") + ") wrote 0x" + Text::hex(words[i], 8) + " to element " +
                             std::to_string(i) + ", expected 0x" + Text::hex(v ^ (v >> 3), 8));
                break;
            }
        }
        std::vector<std::uint8_t> vs;
        std::vector<std::uint8_t> ps;
        if (!dxc.compile(kVertex, L"main", vertex_target(model), vs, log) || !dxc.compile(kPixel, L"main", pixel_target(model), ps, log)) {
            problems.add(name + " graphics shaders do not compile: " + log);
            continue;
        }
        D3d12Frame frame;
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(graphics_signature, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
        ID3D12PipelineState* graphics = D3d12Tools::graphics_pso(rig, desc, error);
        if (graphics == nullptr || !frame.open(rig, 32, 32, error)) {
            problems.add(name + " graphics pipeline: " + error);
            continue;
        }
        TexelImage image;
        if (!frame.run(
                [&](ID3D12GraphicsCommandList* list) {
                    list->SetGraphicsRootSignature(graphics_signature);
                    list->SetPipelineState(graphics);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                },
                error) ||
            !frame.read(image, error)) {
            problems.add(name + " graphics draw: " + error);
            continue;
        }
        for (UINT y = 0; y < 32; ++y) {
            for (UINT x = 0; x < 32; ++x) {
                const std::uint32_t v = x * 7u + y * 13u;
                const int want_red = static_cast<int>(v & 255u);
                const int want_green = static_cast<int>((v >> 8) & 255u);
                const std::uint32_t pixel = image.pixel(x, y);
                if (std::abs(static_cast<int>(pixel & 255u) - want_red) > 1 || std::abs(static_cast<int>((pixel >> 8) & 255u) - want_green) > 1 ||
                    std::abs(static_cast<int>((pixel >> 16) & 255u) - 64) > 1) {
                    problems.add(name + " graphics shaders: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(pixel, 8) + ", expected red " +
                                 std::to_string(want_red) + " green " + std::to_string(want_green) + " blue 64");
                    x = 32;
                    y = 32;
                }
            }
        }
    }
    if (highest < kLastModel) {
        std::vector<std::uint8_t> code;
        std::string log;
        if (dxc.compile(std::string("#define WAVE 0\n") + kComputeBody, L"main", compute_target(highest + 1), code, log)) {
            Com<ID3D12PipelineState> state;
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = compute_signature;
            desc.CS = D3d12Tools::bytecode(code);
            const HRESULT result = rig.device()->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
            if (SUCCEEDED(result)) {
                problems.add("a pipeline with a shader model " + model_text(highest + 1) + " compute shader was created although " + model_text(highest) + " is the highest reported");
            }
            if (rig.removed_reason() != S_OK) {
                problems.add("the device was removed after the pipeline with an unsupported shader model");
            }
        } else {
            context.note("the DXIL compiler does not know " + model_text(highest + 1) + ", the refusal of an unsupported model was checked through CheckFeatureSupport only");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::shader
