#include "render/d3d12/textures.hpp"

#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

struct Frame128 {
    Surface color;
    Geometry sphere;
    Geometry torus;

    bool open(Kit& kit, std::string& error) {
        return color.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error) && sphere.create(kit, Shapes::sphere(20, 28, 0.8f), true, error) &&
               torus.create(kit, Shapes::torus(28, 14, 0.9f, 0.3f), true, error);
    }

    void begin(ID3D12GraphicsCommandList* list) {
        color.bind_target(list);
        const float clear[4] = {0.06f, 0.07f, 0.12f, 1.0f};
        list->ClearRenderTargetView(color.view, clear, 0, nullptr);
        const D3D12_VIEWPORT vp = viewport_of(128, 128);
        const D3D12_RECT scissor = rect_of(128, 128);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &scissor);
    }

    bool finish(Kit& kit, rr::CaptureSet& out, std::string& error) {
        rr::Image image;
        if (!kit.read_image(color.resource, color.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    }
};

std::string lit_vs(const std::string& decl, const char* mvp, const char* color) {
    return decl + "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                  "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
                  "VsOut main(VsIn i) { VsOut o; o.pos = mul(" + std::string(mvp) + ", float4(i.pos, 1));\n"
                  "  float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2; float4 c = " + color + "; o.col = float4(c.rgb * l, c.a); return o; }\n";
}

const char* kColorPs =
    "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "float4 main(VsOut i) : SV_Target { return i.col; }\n";

rr::Mat4 view_matrix(float yaw = 0.5f) {
    return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f) * rr::Mat4::rotate_y(yaw) * rr::Mat4::rotate_x(0.4f);
}

ID3D12PipelineState* make_pso(Kit& kit, ID3D12RootSignature* signature, const std::string& vs, const std::string& ps, std::string& error,
                              const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak = {}) {
    Pso builder(kit, signature);
    builder.vs(vs).ps(ps).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
    if (tweak) {
        tweak(builder.desc);
    }
    return builder.build(error);
}

template <class Body>
bool draw_frame(Kit& kit, rr::CaptureSet& out, std::string& error, Body body) {
    Frame128 frame;
    if (!frame.open(kit, error)) {
        return false;
    }
    bool prepared = true;
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            frame.begin(list);
            prepared = body(frame, list);
        },
        error);
    return ok && prepared && frame.finish(kit, out, error);
}

ID3D12Resource* constants_buffer(Kit& kit, const void* data, UINT64 size, std::string& error) { return kit.make_upload(data, (size + 255) / 256 * 256, error); }

struct Block {
    float mvp[16];
    float color[4];
};

void register_root_descriptors() {
    Cases::single(rr::CaseSpec("pso", "root_cbv_descriptor").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.cbv(0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        ID3D12PipelineState* pso = make_pso(kit, signature, lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; float4 color; };\n", "mvp", "color"), kColorPs, error);
        Block blocks[2];
        const rr::Mat4 a = view_matrix(0.4f) * rr::Mat4::translate(-0.3f, 0, 0);
        const rr::Mat4 b = view_matrix(0.9f) * rr::Mat4::translate(0.4f, 0.1f, 0) * rr::Mat4::scale(0.7f, 0.7f, 0.7f);
        std::memcpy(blocks[0].mvp, a.m, 64);
        std::memcpy(blocks[1].mvp, b.m, 64);
        const float c0[4] = {0.9f, 0.5f, 0.2f, 1}, c1[4] = {0.2f, 0.7f, 0.9f, 1};
        std::memcpy(blocks[0].color, c0, 16);
        std::memcpy(blocks[1].color, c1, 16);
        std::vector<std::uint8_t> padded(512, 0);
        std::memcpy(padded.data(), &blocks[0], sizeof(Block));
        std::memcpy(padded.data() + 256, &blocks[1], sizeof(Block));
        ID3D12Resource* buffer = constants_buffer(kit, padded.data(), 512, error);
        if (pso == nullptr || buffer == nullptr) {
            return false;
        }
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetGraphicsRootConstantBufferView(0, buffer->GetGPUVirtualAddress());
            frame.torus.bind(list);
            frame.torus.draw(list);
            list->SetGraphicsRootConstantBufferView(0, buffer->GetGPUVirtualAddress() + 256);
            frame.sphere.bind(list);
            frame.sphere.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "root_srv_descriptor_structured").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.constants(16, 0).srv(0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; };\nStructuredBuffer<float4> palette : register(t0);\n", "mvp", "palette[(uint)(i.uv.x * 7.99)]");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        float colors[32];
        for (int k = 0; k < 8; ++k) {
            colors[k * 4] = 0.2f + 0.1f * static_cast<float>(k);
            colors[k * 4 + 1] = 0.9f - 0.1f * static_cast<float>(k);
            colors[k * 4 + 2] = 0.3f + 0.05f * static_cast<float>(k * 3 % 7);
            colors[k * 4 + 3] = 1;
        }
        ID3D12Resource* buffer = kit.make_upload(colors, sizeof colors, error);
        if (pso == nullptr || buffer == nullptr) {
            return false;
        }
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetGraphicsRootShaderResourceView(1, buffer->GetGPUVirtualAddress());
            const rr::Mat4 m = view_matrix() * rr::Mat4::rotate_x(0.5f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.torus.bind(list);
            frame.torus.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "root_uav_descriptor_compute_then_draw").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root compute_root;
        compute_root.uav(0);
        ID3D12RootSignature* compute_signature = compute_root.build(kit.rig, error);
        if (compute_signature == nullptr) {
            return false;
        }
        Kit::Bytes code;
        if (!kit.compile_as("RWStructuredBuffer<float4> palette : register(u0);\n[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { float t = id.x / 7.0; palette[id.x] = float4(t, 1.0 - t, 0.4 + 0.3 * sin(t * 6.0), 1); }\n", "main", "cs", code, error)) {
            return false;
        }
        ID3D12PipelineState* compute = D3d12Tools::compute_pso(kit.rig, compute_signature, code, error);
        HRESULT result = S_OK;
        ID3D12Resource* buffer = D3d12Tools::device_buffer(kit.rig, 128, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
        D3d12Root root;
        root.constants(16, 0).srv(0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (compute == nullptr || buffer == nullptr || signature == nullptr) {
            return fail_with(error, error.empty() ? "buffer: " + D3d12Tools::hr(result) : error);
        }
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; };\nStructuredBuffer<float4> palette : register(t0);\n", "mvp", "palette[(uint)(i.uv.x * 7.99)]");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        if (pso == nullptr) {
            return false;
        }
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetComputeRootSignature(compute_signature);
            list->SetPipelineState(compute);
            list->SetComputeRootUnorderedAccessView(0, buffer->GetGPUVirtualAddress());
            list->Dispatch(1, 1, 1);
            D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetGraphicsRootShaderResourceView(1, buffer->GetGPUVirtualAddress());
            const rr::Mat4 m = view_matrix();
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.sphere.bind(list);
            frame.sphere.draw(list);
            return true;
        });
    });
    for (int order = 0; order < 2; ++order) {
        Cases::single(rr::CaseSpec("pso", order == 0 ? "root_parameter_order_constants_last" : "root_parameter_order_table_first").tolerance(rr::Tolerance::blend(2)).tag("root"),
                      [order](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          D3d12Root root;
                          if (order == 0) {
                              root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0).cbv(1).constants(20, 0);
                          } else {
                              root.constants(20, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0).cbv(1);
                          }
                          root.flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
                          ID3D12RootSignature* signature = root.build(kit.rig, error);
                          if (signature == nullptr) {
                              return false;
                          }
                          const std::string vs =
                              "cbuffer C : register(b0) { column_major float4x4 mvp; float4 tint; };\ncbuffer U : register(b1) { float4 extra; };\nTexture2D<float4> tex : register(t0);\n"
                              "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\nstruct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
                              "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
                              "  float4 t = tex.Load(int3((int2)(i.uv * 7.99), 0)); o.col = float4(t.rgb * tint.rgb * extra.rgb * l, 1); return o; }\n";
                          ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
                          TextureSpec spec;
                          spec.width = 8;
                          spec.height = 8;
                          spec.content = 0;
                          ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
                          const float extra[4] = {1.0f, 0.9f, 0.8f, 1.0f};
                          ID3D12Resource* cbv = constants_buffer(kit, extra, 16, error);
                          HRESULT result = S_OK;
                          ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
                          if (pso == nullptr || texture == nullptr || cbv == nullptr || heap == nullptr) {
                              return false;
                          }
                          const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
                          kit.device()->CreateShaderResourceView(texture, &srv, heap->GetCPUDescriptorHandleForHeapStart());
                          return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
                              list->SetGraphicsRootSignature(signature);
                              list->SetPipelineState(pso);
                              list->SetDescriptorHeaps(1, &heap);
                              const UINT table = order == 0 ? 0 : 1;
                              const UINT cbv_index = order == 0 ? 1 : 2;
                              const UINT constants = order == 0 ? 2 : 0;
                              list->SetGraphicsRootDescriptorTable(table, heap->GetGPUDescriptorHandleForHeapStart());
                              list->SetGraphicsRootConstantBufferView(cbv_index, cbv->GetGPUVirtualAddress());
                              const rr::Mat4 m = view_matrix(0.8f);
                              const float tint[4] = {1, 1, 1, 1};
                              list->SetGraphicsRoot32BitConstants(constants, 16, m.m, 0);
                              list->SetGraphicsRoot32BitConstants(constants, 4, tint, 16);
                              frame.sphere.bind(list);
                              frame.sphere.draw(list);
                              return true;
                          });
                      });
    }
}

void register_tables() {
    Cases::single(rr::CaseSpec("pso", "table_unbounded_srv_array_sm6").variant(1).tag("x64-only").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.constants(20, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; uint4 pick; };\nTexture2D<float4> textures[] : register(t0);\n", "mvp",
                                      "textures[NonUniformResourceIndex(pick.x + (uint)(i.uv.x * 3.99))].Load(int3(5, 5, 0))");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        HRESULT result = S_OK;
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, true, &result);
        if (pso == nullptr || heap == nullptr) {
            return false;
        }
        const UINT increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT k = 0; k < 8; ++k) {
            TextureSpec spec;
            spec.width = 8;
            spec.height = 8;
            spec.content = k;
            ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
            if (texture == nullptr) {
                return false;
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
            kit.device()->CreateShaderResourceView(texture, &srv, D3d12Tools::cpu_at(heap, k, increment));
        }
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetDescriptorHeaps(1, &heap);
            list->SetGraphicsRootDescriptorTable(1, D3d12Tools::gpu_at(heap, 0, increment));
            const rr::Mat4 m = view_matrix(0.8f);
            const UINT pick[4] = {2, 0, 0, 0};
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            list->SetGraphicsRoot32BitConstants(0, 4, pick, 16);
            frame.sphere.bind(list);
            frame.sphere.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "table_multiple_register_spaces").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.constants(16, 0, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1).table(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 2).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        const std::string vs = lit_vs("cbuffer C : register(b0, space0) { column_major float4x4 mvp; };\ncbuffer Tint : register(b0, space2) { float4 tint; };\nTexture2D<float4> a : register(t0, space0);\nTexture2D<float4> b : register(t0, space1);\n", "mvp",
                                      "(i.uv.x < 0.5 ? a.Load(int3(3, 3, 0)) : b.Load(int3(3, 3, 0))) * tint");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        HRESULT result = S_OK;
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true, &result);
        TextureSpec spec;
        spec.width = 8;
        spec.height = 8;
        ID3D12Resource* ta = TextureFactory::create(kit, spec, true, error);
        spec.content = 1;
        ID3D12Resource* tb = TextureFactory::create(kit, spec, true, error);
        const float tint[4] = {1.0f, 0.8f, 0.9f, 1.0f};
        ID3D12Resource* cbv = constants_buffer(kit, tint, 16, error);
        if (pso == nullptr || heap == nullptr || ta == nullptr || tb == nullptr || cbv == nullptr) {
            return false;
        }
        const UINT increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
        kit.device()->CreateShaderResourceView(ta, &srv, D3d12Tools::cpu_at(heap, 0, increment));
        kit.device()->CreateShaderResourceView(tb, &srv, D3d12Tools::cpu_at(heap, 1, increment));
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{cbv->GetGPUVirtualAddress(), 256};
        kit.device()->CreateConstantBufferView(&cbv_desc, D3d12Tools::cpu_at(heap, 2, increment));
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetDescriptorHeaps(1, &heap);
            list->SetGraphicsRootDescriptorTable(1, D3d12Tools::gpu_at(heap, 0, increment));
            list->SetGraphicsRootDescriptorTable(2, D3d12Tools::gpu_at(heap, 1, increment));
            list->SetGraphicsRootDescriptorTable(3, D3d12Tools::gpu_at(heap, 2, increment));
            const rr::Mat4 m = view_matrix(0.8f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.torus.bind(list);
            frame.torus.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "table_offset_into_heap_and_null_descriptors").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.constants(16, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; };\nTexture2D<float4> a : register(t0);\nTexture2D<float4> b : register(t1);\n", "mvp",
                                      "a.Load(int3(2, 2, 0)) + float4(0.2, 0.2, 0.2, 0) + b.Load(int3(1, 1, 0)) * 0.7");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        HRESULT result = S_OK;
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, true, &result);
        TextureSpec spec;
        spec.width = 8;
        spec.height = 8;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (pso == nullptr || heap == nullptr || texture == nullptr) {
            return false;
        }
        const UINT increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
        kit.device()->CreateShaderResourceView(texture, &srv, D3d12Tools::cpu_at(heap, 5, increment));
        kit.device()->CreateShaderResourceView(nullptr, &srv, D3d12Tools::cpu_at(heap, 6, increment));
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetDescriptorHeaps(1, &heap);
            list->SetGraphicsRootDescriptorTable(1, D3d12Tools::gpu_at(heap, 5, increment));
            const rr::Mat4 m = view_matrix(0.8f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.sphere.bind(list);
            frame.sphere.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "descriptor_copy_staging_to_shader_visible").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3d12Root root;
        root.constants(16, 0).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ID3D12RootSignature* signature = root.build(kit.rig, error);
        if (signature == nullptr) {
            return false;
        }
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; };\nTexture2D<float4> t0 : register(t0);\nTexture2D<float4> t1 : register(t1);\nTexture2D<float4> t2 : register(t2);\n", "mvp",
                                      "(t0.Load(int3(2, 2, 0)) * i.uv.x + t1.Load(int3(2, 2, 0)) * (1 - i.uv.x)) * 0.6 + t2.Load(int3(4, 4, 0)) * 0.4");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        HRESULT result = S_OK;
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true, &result);
        ID3D12DescriptorHeap* staging = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, false, &result);
        if (pso == nullptr || heap == nullptr || staging == nullptr) {
            return false;
        }
        const UINT increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT k = 0; k < 3; ++k) {
            TextureSpec spec;
            spec.width = 8;
            spec.height = 8;
            spec.content = k + 1;
            ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
            if (texture == nullptr) {
                return false;
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
            kit.device()->CreateShaderResourceView(texture, &srv, D3d12Tools::cpu_at(staging, k * 2 + 1, increment));
        }
        D3D12_CPU_DESCRIPTOR_HANDLE sources[3] = {D3d12Tools::cpu_at(staging, 1, increment), D3d12Tools::cpu_at(staging, 3, increment), D3d12Tools::cpu_at(staging, 5, increment)};
        const UINT source_sizes[3] = {1, 1, 1};
        const D3D12_CPU_DESCRIPTOR_HANDLE destination = heap->GetCPUDescriptorHandleForHeapStart();
        const UINT destination_size = 3;
        kit.device()->CopyDescriptors(1, &destination, &destination_size, 3, sources, source_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetDescriptorHeaps(1, &heap);
            list->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
            const rr::Mat4 m = view_matrix(0.8f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.torus.bind(list);
            frame.torus.draw(list);
            return true;
        });
    });
}

void register_static_samplers_and_versions() {
    struct Border {
        const char* name;
        D3D12_STATIC_BORDER_COLOR color;
    };
    const Border borders[] = {{"transparent_black", D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK}, {"opaque_black", D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK}, {"opaque_white", D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE}};
    for (const Border& border : borders) {
        Cases::single(rr::CaseSpec("pso", std::string("static_sampler_border_") + border.name).tolerance(sampler_tolerance()).tag("root"), [border](Kit& kit, rr::CaptureSet& out, std::string& error) {
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.BorderColor = border.color;
            sampler.MaxLOD = 1000.0f;
            sampler.ShaderRegister = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            D3d12Root root;
            root.constants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX).table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL).sampler(sampler).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            ID3D12RootSignature* signature = root.build(kit.rig, error);
            if (signature == nullptr) {
                return false;
            }
            const std::string vs =
                "cbuffer C : register(b0) { column_major float4x4 mvp; };\nstruct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\nVsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.uv = i.uv * 2.2 - 0.6; return o; }\n";
            const std::string ps =
                "Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\nstruct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { return tex.Sample(smp, i.uv); }\n";
            ID3D12PipelineState* pso = make_pso(kit, signature, vs, ps, error);
            TextureSpec spec;
            spec.width = 32;
            spec.height = 32;
            spec.content = 0;
            ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
            HRESULT result = S_OK;
            ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
            if (pso == nullptr || texture == nullptr || heap == nullptr) {
                return false;
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
            kit.device()->CreateShaderResourceView(texture, &srv, heap->GetCPUDescriptorHandleForHeapStart());
            return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
                list->SetGraphicsRootSignature(signature);
                list->SetPipelineState(pso);
                list->SetDescriptorHeaps(1, &heap);
                list->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
                const rr::Mat4 m = view_matrix(0.0f) * rr::Mat4::scale(1.2f, 1.2f, 1.2f);
                list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
                frame.sphere.bind(list);
                frame.sphere.draw(list);
                return true;
            });
        });
    }
    Cases::single(rr::CaseSpec("pso", "root_signature_version_1_1_data_static").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        D3D12_DESCRIPTOR_RANGE1 range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
        D3D12_ROOT_PARAMETER1 parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 16;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = 2;
        desc.Desc_1_1.pParameters = parameters;
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Com<ID3DBlob> blob;
        Com<ID3DBlob> messages;
        HRESULT result = ::D3D12SerializeVersionedRootSignature(&desc, blob.put(), messages.put());
        if (FAILED(result)) {
            return fail_with(error, "D3D12SerializeVersionedRootSignature: " + D3d12Tools::hr(result));
        }
        Com<ID3D12RootSignature> created;
        result = kit.device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), created.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreateRootSignature (1.1): " + D3d12Tools::hr(result));
        }
        ID3D12RootSignature* signature = kit.rig.hold(std::move(created));
        const std::string vs = lit_vs("cbuffer C : register(b0) { column_major float4x4 mvp; };\nTexture2D<float4> tex : register(t0);\n", "mvp", "tex.Load(int3((int2)(i.uv * 15.99), 0))");
        ID3D12PipelineState* pso = make_pso(kit, signature, vs, kColorPs, error);
        TextureSpec spec;
        spec.width = 16;
        spec.height = 16;
        spec.content = 0;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
        if (pso == nullptr || texture == nullptr || heap == nullptr) {
            return false;
        }
        const D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
        kit.device()->CreateShaderResourceView(texture, &srv, heap->GetCPUDescriptorHandleForHeapStart());
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetDescriptorHeaps(1, &heap);
            list->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
            const rr::Mat4 m = view_matrix(0.6f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.sphere.bind(list);
            frame.sphere.draw(list);
            return true;
        });
    });
    Cases::single(rr::CaseSpec("pso", "root_signature_embedded_in_shader_hlsl").tolerance(rr::Tolerance::blend(2)).tag("root"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const std::string rs = "#define RS \"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), RootConstants(num32BitConstants=16, b0)\"\n";
        const std::string vs = rs + "cbuffer C : register(b0) { column_major float4x4 mvp; };\nstruct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                                    "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n[RootSignature(RS)] VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1));\n"
                                    "  float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2; o.col = float4(float3(0.9, 0.6, 0.3) * l, 1); return o; }\n";
        Kit::Bytes vs_code;
        Kit::Bytes ps_code;
        if (!kit.compile(vs, 'v', vs_code, error) || !kit.compile(kColorPs, 'p', ps_code, error)) {
            return false;
        }
        Com<ID3D12RootSignature> created;
        const HRESULT result = kit.device()->CreateRootSignature(0, vs_code.data(), vs_code.size(), __uuidof(ID3D12RootSignature), created.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreateRootSignature from the shader blob: " + D3d12Tools::hr(result));
        }
        ID3D12RootSignature* signature = kit.rig.hold(std::move(created));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = Kit::pipeline_desc(signature, vs_code, ps_code, {DXGI_FORMAT_R8G8B8A8_UNORM}, DXGI_FORMAT_UNKNOWN);
        const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
        desc.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ID3D12PipelineState* pso = D3d12Tools::graphics_pso(kit.rig, desc, error);
        if (pso == nullptr) {
            return false;
        }
        return draw_frame(kit, out, error, [&](Frame128& frame, ID3D12GraphicsCommandList* list) {
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            const rr::Mat4 m = view_matrix(0.6f);
            list->SetGraphicsRoot32BitConstants(0, 16, m.m, 0);
            frame.torus.bind(list);
            frame.torus.draw(list);
            return true;
        });
    });
}

const struct Registrar {
    Registrar() {
        register_root_descriptors();
        register_tables();
        register_static_samplers_and_versions();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
