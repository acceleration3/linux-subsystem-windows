#include "sanity/d3d12/pso_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kConstantVs =
    "cbuffer C : register(b0) { float z; float r; float g; float b; }\n"
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p = float2((id << 1) & 2, id & 2);\n"
    "  return float4(p * float2(2, -2) + float2(-1, 1), z, 1);\n"
    "}\n";

const char* kConstantPs =
    "cbuffer C : register(b0) { float z; float r; float g; float b; }\n"
    "float4 main() : SV_Target { return float4(r, g, b, 1); }\n";

const char* kHalfAlphaPs = "float4 main() : SV_Target { return float4(1.0, 0.0, 0.0, 0.5); }\n";

const char* kLogicPs = "float4 main() : SV_Target { return float4(15.0 / 255.0, 15.0 / 255.0, 15.0 / 255.0, 15.0 / 255.0); }\n";

const char* kLayoutVs =
    "struct VI { float2 pos : POSITION; float4 color : COLOR; };\n"
    "struct VO { float4 pos : SV_Position; float4 color : COLOR; };\n"
    "VO main(VI i) { VO o; o.pos = float4(i.pos, 0, 1); o.color = i.color; return o; }\n";

const char* kLayoutPs = "float4 main(float4 pos : SV_Position, float4 color : COLOR) : SV_Target { return color; }\n";

const char* kReduce =
    "StructuredBuffer<uint> s : register(t0);\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "groupshared uint tile[64];\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID, uint3 gt : SV_GroupThreadID, uint3 gid : SV_GroupID) {\n"
    "  tile[gt.x] = s[id.x] * 2654435761u + id.x;\n"
    "  GroupMemoryBarrierWithGroupSync();\n"
    "  for (uint stride = 32; stride > 0; stride >>= 1) {\n"
    "    if (gt.x < stride) { tile[gt.x] += tile[gt.x + stride]; }\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "  }\n"
    "  if (gt.x == 0) { o[gid.x] = tile[0]; }\n"
    "}\n";

void draw_constants(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature, ID3D12PipelineState* state, float z, float r, float g, float b,
                    UINT stencil_ref = 0) {
    const float values[4] = {z, r, g, b};
    list->SetGraphicsRootSignature(signature);
    list->SetPipelineState(state);
    list->SetGraphicsRoot32BitConstants(0, 4, values, 0);
    list->OMSetStencilRef(stencil_ref);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

void expect_image(Problems& problems, const std::string& label, const TexelImage& image, std::uint32_t expected, int tolerance = 0) {
    for (UINT y = 0; y < image.height; ++y) {
        for (UINT x = 0; x < image.width; ++x) {
            const std::uint32_t got = image.pixel(x, y);
            bool close = true;
            for (int shift = 0; shift < 32; shift += 8) {
                if (std::abs(static_cast<int>((got >> shift) & 0xff) - static_cast<int>((expected >> shift) & 0xff)) > tolerance) {
                    close = false;
                }
            }
            if (!close) {
                problems.add(label + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(expected, 8));
                return;
            }
        }
    }
}

}  // namespace

Outcome PsoChecks::graphics_compute(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Root constants_root;
        constants_root.constants(4, 0);
        ID3D12RootSignature* constants_signature = constants_root.build(rig, error);
        if (constants_signature == nullptr) {
            problems.add(error);
            return {};
        }
        D3d12Tools::Bytes vs;
        D3d12Tools::Bytes ps;
        if (!D3d12Tools::compile(kConstantVs, "main", "vs_5_1", vs, error) || !D3d12Tools::compile(kConstantPs, "main", "ps_5_1", ps, error)) {
            problems.add(error);
            return {};
        }
        auto make = [&](const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak, DXGI_FORMAT dsv) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(constants_signature, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
            desc.DSVFormat = dsv;
            tweak(desc);
            return D3d12Tools::graphics_pso(rig, desc, error);
        };
        ID3D12PipelineState* depth_less = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        }, DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* depth_greater = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
        }, DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* depth_no_write = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        }, DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* plain = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC&) {}, DXGI_FORMAT_UNKNOWN);
        ID3D12PipelineState* mask = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_ALPHA;
        }, DXGI_FORMAT_UNKNOWN);
        ID3D12PipelineState* cull_back = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; }, DXGI_FORMAT_UNKNOWN);
        ID3D12PipelineState* cull_front = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; }, DXGI_FORMAT_UNKNOWN);
        ID3D12PipelineState* ccw_back = make([](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            desc.RasterizerState.FrontCounterClockwise = TRUE;
        }, DXGI_FORMAT_UNKNOWN);
        if (depth_less == nullptr || depth_greater == nullptr || depth_no_write == nullptr || plain == nullptr || mask == nullptr || cull_back == nullptr ||
            cull_front == nullptr || ccw_back == nullptr) {
            problems.add("creating the graphics pipelines failed: " + error);
            return {};
        }
        const std::uint32_t red = Kit::rgba(255, 0, 0, 255);
        const std::uint32_t green = Kit::rgba(0, 255, 0, 255);
        const std::uint32_t clear_black = Kit::rgba(0, 0, 0, 0);
        struct DepthScene {
            const char* name;
            ID3D12PipelineState* state;
            float clear_depth;
            float first_z;
            float second_z;
            bool first_red;
            std::uint32_t expected;
        };
        const DepthScene depth_scenes[] = {
            {"depth LESS, far then near", depth_less, 1.0f, 0.6f, 0.3f, false, red},
            {"depth LESS, near then far", depth_less, 1.0f, 0.3f, 0.6f, true, red},
            {"depth GREATER, near then far", depth_greater, 0.0f, 0.3f, 0.6f, true, green},
            {"depth GREATER, far then near", depth_greater, 0.0f, 0.6f, 0.3f, false, green},
            {"depth LESS without depth writes, near then far", depth_no_write, 1.0f, 0.3f, 0.6f, true, green},
        };
        for (const DepthScene& scene : depth_scenes) {
            D3d12Frame frame;
            TexelImage image;
            const float first_r = scene.first_red ? 1.0f : 0.0f;
            const float first_g = 1.0f - first_r;
            if (!frame.open(rig, 32, 32, error, DXGI_FORMAT_R8G8B8A8_UNORM, true) ||
                !frame.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        frame.clear(list, 0, 0, 0, 0);
                        frame.clear_depth(list, scene.clear_depth);
                        draw_constants(list, constants_signature, scene.state, scene.first_z, first_r, first_g, 0.0f);
                        draw_constants(list, constants_signature, scene.state, scene.second_z, first_g, first_r, 0.0f);
                    },
                    error) ||
                !frame.read(image, error)) {
                problems.add(std::string(scene.name) + ": " + error);
                continue;
            }
            expect_image(problems, scene.name, image, scene.expected);
        }
        struct Simple {
            const char* name;
            ID3D12PipelineState* state;
            std::uint32_t expected;
        };
        const Simple simple_scenes[] = {
            {"colour write mask red and alpha", mask, Kit::rgba(255, 0, 0, 255)},
            {"back face culling of a clockwise triangle", cull_back, Kit::rgba(255, 255, 255, 255)},
            {"front face culling of a clockwise triangle", cull_front, clear_black},
            {"counter-clockwise front faces with back culling", ccw_back, clear_black},
            {"plain draw", plain, Kit::rgba(255, 255, 255, 255)},
        };
        for (const Simple& scene : simple_scenes) {
            D3d12Frame frame;
            TexelImage image;
            if (!frame.open(rig, 32, 32, error) ||
                !frame.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        frame.clear(list, 0, 0, 0, 0);
                        draw_constants(list, constants_signature, scene.state, 0.5f, 1.0f, 1.0f, 1.0f);
                    },
                    error) ||
                !frame.read(image, error)) {
                problems.add(std::string(scene.name) + ": " + error);
                continue;
            }
            expect_image(problems, scene.name, image, scene.expected);
        }
        {
            D3d12Root empty;
            ID3D12RootSignature* signature = empty.build(rig, error);
            ID3D12PipelineState* blend = signature != nullptr ? Kit::pipeline(rig, signature, kHalfAlphaPs, DXGI_FORMAT_R8G8B8A8_UNORM, error, [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
                desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            }) : nullptr;
            D3d12Frame frame;
            TexelImage image;
            if (blend == nullptr || !frame.open(rig, 32, 32, error) ||
                !frame.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        frame.clear(list, 0.2f, 0.4f, 0.6f, 1.0f);
                        Kit::draw_fullscreen(list, signature, blend);
                    },
                    error) ||
                !frame.read(image, error)) {
                problems.add("alpha blending: " + error);
            } else {
                expect_image(problems, "alpha blending", image, Kit::rgba(153, 51, 77, 128), 1);
            }
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            static_cast<void>(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS, options));
            std::string logic_error;
            ID3D12PipelineState* logic = signature != nullptr ? Kit::pipeline(rig, signature, kLogicPs, DXGI_FORMAT_R8G8B8A8_UNORM, logic_error, [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
                desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
                desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_XOR;
                desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
            }) : nullptr;
            if (!options.OutputMergerLogicOp) {
                if (logic != nullptr) {
                    problems.add("OutputMergerLogicOp is not reported but a pipeline with a logic op was created");
                }
            } else if (logic == nullptr) {
                problems.add("OutputMergerLogicOp is reported but creating a logic op pipeline failed: " + logic_error);
            } else {
                D3d12Frame logic_frame;
                TexelImage logic_image;
                if (!logic_frame.open(rig, 32, 32, error) ||
                    !logic_frame.run(
                        [&](ID3D12GraphicsCommandList* list) {
                            logic_frame.clear(list, 0.0f, 1.0f, 0.0f, 1.0f);
                            Kit::draw_fullscreen(list, signature, logic);
                        },
                        error) ||
                    !logic_frame.read(logic_image, error)) {
                    problems.add("logic op XOR: " + error);
                } else {
                    expect_image(problems, "logic op XOR", logic_image, Kit::rgba(0x0f, 0xf0, 0x0f, 0xf0));
                }
            }
        }
        {
            D3d12Root empty;
            ID3D12RootSignature* signature = empty.flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT).build(rig, error);
            const D3D12_INPUT_ELEMENT_DESC elements[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };
            ID3D12PipelineState* layout = signature != nullptr ? Kit::pipeline_with(rig, signature, kLayoutVs, kLayoutPs, DXGI_FORMAT_R8G8B8A8_UNORM, error, [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
                desc.InputLayout = {elements, 2};
            }) : nullptr;
            const float vertices[3][6] = {{-1, 1, 0.2f, 0.6f, 1.0f, 1.0f}, {3, 1, 0.2f, 0.6f, 1.0f, 1.0f}, {-1, -3, 0.2f, 0.6f, 1.0f, 1.0f}};
            ID3D12Resource* vertex_buffer = D3d12Tools::device_buffer(rig, sizeof(vertices), D3D12_RESOURCE_STATE_COPY_DEST);
            D3d12Frame frame;
            TexelImage image;
            if (layout == nullptr || vertex_buffer == nullptr || !D3d12Tools::write_buffer(rig, vertex_buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, vertices, sizeof(vertices), error) ||
                !frame.open(rig, 32, 32, error) ||
                !frame.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, vertex_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                        list->SetGraphicsRootSignature(signature);
                        list->SetPipelineState(layout);
                        const D3D12_VERTEX_BUFFER_VIEW view{vertex_buffer->GetGPUVirtualAddress(), sizeof(vertices), 24};
                        list->IASetVertexBuffers(0, 1, &view);
                        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        list->DrawInstanced(3, 1, 0, 0);
                    },
                    error) ||
                !frame.read(image, error)) {
                problems.add("input layout: " + error);
            } else {
                expect_image(problems, "input layout with a vertex buffer", image, Kit::solid_color(), 1);
            }
        }
        {
            D3d12Root compute_root;
            compute_root.srv(0).uav(0);
            ID3D12RootSignature* compute_signature = compute_root.build(rig, error);
            ID3D12PipelineState* reduce = compute_signature != nullptr ? D3d12Tools::compute_from(rig, compute_signature, kReduce, error) : nullptr;
            const UINT groups = 128;
            const UINT count = groups * 64;
            const std::vector<std::uint8_t> raw = Kit::bytes(1234, count * 4ull);
            ID3D12Resource* source = D3d12Tools::device_buffer(rig, count * 4ull, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* output = D3d12Tools::device_buffer(rig, groups * 4ull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            std::vector<std::uint8_t> back;
            if (reduce == nullptr || source == nullptr || output == nullptr ||
                !D3d12Tools::write_buffer(rig, source, D3D12_RESOURCE_STATE_COPY_DEST, 0, raw.data(), raw.size(), error) ||
                !rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        list->SetComputeRootSignature(compute_signature);
                        list->SetPipelineState(reduce);
                        list->SetComputeRootShaderResourceView(0, source->GetGPUVirtualAddress());
                        list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
                        list->Dispatch(groups, 1, 1);
                    },
                    error) ||
                !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, groups * 4ull, back, error)) {
                problems.add("compute pipeline: " + error);
            } else {
                const std::vector<std::uint32_t> input = Kit::words(raw);
                std::vector<std::uint32_t> expected(groups);
                for (UINT g = 0; g < groups; ++g) {
                    std::uint32_t sum = 0;
                    for (UINT t = 0; t < 64; ++t) {
                        sum += input[g * 64 + t] * 2654435761u + (g * 64 + t);
                    }
                    expected[g] = sum;
                }
                Kit::compare_words(context, problems, "group-shared reduction", expected, Kit::words(back));
            }
        }
        return {};
    });
}

Outcome PsoChecks::library_cache(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        Com<ID3D12Device1> device1 = rig.as<ID3D12Device1>();
        if (!device1) {
            return "ID3D12Device1 (pipeline libraries) is not exposed by this runtime";
        }
        std::string error;
        D3d12Root empty;
        ID3D12RootSignature* signature = empty.build(rig, error);
        D3d12Root compute_root;
        compute_root.uav(0);
        ID3D12RootSignature* compute_signature = compute_root.build(rig, error);
        D3d12Tools::Bytes vs;
        D3d12Tools::Bytes ps;
        D3d12Tools::Bytes cs;
        if (signature == nullptr || compute_signature == nullptr || !D3d12Tools::compile(Kit::vs_source(), "main", "vs_5_1", vs, error) ||
            !D3d12Tools::compile(Kit::kSolidPs, "main", "ps_5_1", ps, error) ||
            !D3d12Tools::compile("RWStructuredBuffer<uint> o : register(u0);\n[numthreads(64, 1, 1)]\nvoid main(uint3 id : SV_DispatchThreadID) { o[id.x] = id.x ^ 0xA5A5u; }\n",
                                 "main", "cs_5_1", cs, error)) {
            problems.add("setup failed: " + error);
            return {};
        }
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = D3d12Tools::graphics_desc(signature, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
        compute.pRootSignature = compute_signature;
        compute.CS = D3d12Tools::bytecode(cs);
        ID3D12PipelineState* graphics_pso = D3d12Tools::graphics_pso(rig, graphics, error);
        ID3D12PipelineState* compute_pso = D3d12Tools::compute_pso(rig, compute_signature, cs, error);
        if (graphics_pso == nullptr || compute_pso == nullptr) {
            problems.add("creating the pipelines failed: " + error);
            return {};
        }
        auto verify_graphics = [&](ID3D12PipelineState* state, const std::string& label) {
            D3d12Frame frame;
            TexelImage image;
            std::string local;
            if (!frame.open(rig, 16, 16, local) ||
                !frame.run([&](ID3D12GraphicsCommandList* list) { Kit::draw_fullscreen(list, signature, state); }, local) || !frame.read(image, local)) {
                problems.add(label + ": " + local);
                return;
            }
            std::string where;
            if (!Kit::uniform(image, Kit::solid_color(), where)) {
                problems.add(label + ": " + where);
            }
        };
        auto verify_compute = [&](ID3D12PipelineState* state, const std::string& label) {
            ID3D12Resource* output = D3d12Tools::device_buffer(rig, 256, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            std::vector<std::uint8_t> back;
            std::string local;
            if (output == nullptr ||
                !rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->SetComputeRootSignature(compute_signature);
                        list->SetPipelineState(state);
                        list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
                        list->Dispatch(1, 1, 1);
                    },
                    local) ||
                !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 256, back, local)) {
                problems.add(label + ": " + local);
                return;
            }
            std::vector<std::uint32_t> expected(64);
            for (std::uint32_t i = 0; i < 64; ++i) {
                expected[i] = i ^ 0xA5A5u;
            }
            Kit::compare_words(context, problems, label, expected, Kit::words(back));
        };
        std::vector<std::uint8_t> blob;
        {
            Com<ID3D12PipelineLibrary> library;
            if (!Kit::succeeds(problems, "CreatePipelineLibrary(empty)", device1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), library.put_void()))) {
                return {};
            }
            Kit::succeeds(problems, "StorePipeline(graphics)", library->StorePipeline(L"graphics", graphics_pso));
            Kit::succeeds(problems, "StorePipeline(compute)", library->StorePipeline(L"compute", compute_pso));
            const HRESULT duplicate = library->StorePipeline(L"graphics", graphics_pso);
            context.measure("d3d12.duplicate_store", Kit::hr(duplicate));
            if (FAILED(duplicate)) {
                Kit::expect(problems, "StorePipeline with a name that exists", duplicate, {E_INVALIDARG});
            }
            const SIZE_T size = library->GetSerializedSize();
            if (size == 0) {
                problems.add("GetSerializedSize of a library with two pipelines is 0");
                return {};
            }
            blob.resize(size);
            Kit::succeeds(problems, "Serialize", library->Serialize(blob.data(), size));
        }
        {
            Com<ID3D12PipelineLibrary> library;
            if (Kit::succeeds(problems, "CreatePipelineLibrary(blob)", device1->CreatePipelineLibrary(blob.data(), blob.size(), __uuidof(ID3D12PipelineLibrary), library.put_void()))) {
                Com<ID3D12PipelineState> loaded_graphics;
                Com<ID3D12PipelineState> loaded_compute;
                if (Kit::succeeds(problems, "LoadGraphicsPipeline", library->LoadGraphicsPipeline(L"graphics", &graphics, __uuidof(ID3D12PipelineState), loaded_graphics.put_void()))) {
                    verify_graphics(rig.hold(std::move(loaded_graphics)), "a graphics pipeline loaded from the library");
                }
                if (Kit::succeeds(problems, "LoadComputePipeline", library->LoadComputePipeline(L"compute", &compute, __uuidof(ID3D12PipelineState), loaded_compute.put_void()))) {
                    verify_compute(rig.hold(std::move(loaded_compute)), "a compute pipeline loaded from the library");
                }
                Com<ID3D12PipelineState> missing;
                const HRESULT absent = library->LoadGraphicsPipeline(L"absent", &graphics, __uuidof(ID3D12PipelineState), missing.put_void());
                if (SUCCEEDED(absent)) {
                    problems.add("LoadGraphicsPipeline found a name that was never stored");
                } else {
                    Kit::expect(problems, "LoadGraphicsPipeline for a name that was never stored", absent, {E_INVALIDARG});
                }
            }
        }
        {
            std::vector<std::vector<std::uint8_t>> broken;
            broken.push_back(std::vector<std::uint8_t>(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(blob.size() / 2)));
            std::vector<std::uint8_t> head = blob;
            for (std::size_t i = 0; i < std::min<std::size_t>(32, head.size()); ++i) {
                head[i] = 0xff;
            }
            broken.push_back(head);
            broken.push_back(std::vector<std::uint8_t>(64, 0x5a));
            for (std::size_t i = 0; i < broken.size(); ++i) {
                Com<ID3D12PipelineLibrary> library;
                const HRESULT result = device1->CreatePipelineLibrary(broken[i].data(), broken[i].size(), __uuidof(ID3D12PipelineLibrary), library.put_void());
                context.measure("d3d12.broken_blob_" + std::to_string(i), Kit::hr(result));
                if (FAILED(result)) {
                    Kit::expect(problems, "CreatePipelineLibrary from a damaged blob #" + std::to_string(i), result, {E_INVALIDARG, D3d12Tools::kDriverVersionMismatch, D3d12Tools::kAdapterNotFound});
                }
            }
        }
        {
            Com<ID3DBlob> cached;
            if (Kit::succeeds(problems, "GetCachedBlob", graphics_pso->GetCachedBlob(cached.put())) && cached->GetBufferSize() != 0) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC with_cache = graphics;
                with_cache.CachedPSO = {cached->GetBufferPointer(), cached->GetBufferSize()};
                Com<ID3D12PipelineState> created;
                const HRESULT result = rig.device()->CreateGraphicsPipelineState(&with_cache, __uuidof(ID3D12PipelineState), created.put_void());
                if (SUCCEEDED(result)) {
                    verify_graphics(rig.hold(std::move(created)), "a pipeline created with its own cached blob");
                } else {
                    Kit::expect(problems, "CreateGraphicsPipelineState with the pipeline's own cached blob", result, {D3d12Tools::kDriverVersionMismatch, D3d12Tools::kAdapterNotFound});
                }
            }
            std::vector<std::uint8_t> garbage(256, 0xc3);
            D3D12_GRAPHICS_PIPELINE_STATE_DESC with_garbage = graphics;
            with_garbage.CachedPSO = {garbage.data(), garbage.size()};
            Com<ID3D12PipelineState> created;
            const HRESULT result = rig.device()->CreateGraphicsPipelineState(&with_garbage, __uuidof(ID3D12PipelineState), created.put_void());
            if (SUCCEEDED(result)) {
                verify_graphics(rig.hold(std::move(created)), "a pipeline created with a garbage cached blob (ignored)");
            } else {
                Kit::expect(problems, "CreateGraphicsPipelineState with a garbage cached blob", result,
                            {E_INVALIDARG, D3d12Tools::kDriverVersionMismatch, D3d12Tools::kAdapterNotFound});
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
