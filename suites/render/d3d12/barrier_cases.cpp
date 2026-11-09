#include "render/d3d12/compute.hpp"

#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

const char* kCopyFx =
    "Texture2D<float4> src : register(t0); RWTexture2D<float4> dst : register(u0);\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float4 a = src[id.xy]; float4 b = src[min(id.xy + uint2(2, 1), uint2(127, 127))]; dst[id.xy] = float4(1.0 - (a.rgb + b.rgb) * 0.5, 1); }\n";

ID3D12Resource* uav_target(Kit& kit, UINT size, std::string& error) {
    TextureSpec spec;
    spec.width = size;
    spec.height = size;
    spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return TextureFactory::create(kit, spec, false, error);
}

void register_transitions() {
    Cases::single(rr::CaseSpec("barrier", "transition_chain_rt_srv_uav_copy").tolerance(rr::Tolerance::blend(2)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        Cs cs;
        if (!stage.open(kit, error) || !cs.open(kit, kCopyFx, error)) {
            return false;
        }
        ID3D12Resource* target = uav_target(kit, 128, error);
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (target == nullptr || state == nullptr) {
            return false;
        }
        cs.binder.srv_texture(0, stage.canvas.color.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
        cs.binder.uav_texture(0, target, DXGI_FORMAT_R8G8B8A8_UNORM);
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
                stage.canvas.color.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                D3d12Tools::move(list, target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cs.bind(list);
                list->Dispatch(16, 16, 1);
            },
            error);
        rr::Image image;
        if (!ok || !kit.read_image(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("barrier", "split_barrier_begin_end_around_other_work").tolerance(rr::Tolerance::blend(2)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        Surface other;
        if (!stage.open(kit, error) || !other.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
            return false;
        }
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
                D3D12_RESOURCE_BARRIER begin = D3d12Tools::transition(stage.canvas.color.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                begin.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
                list->ResourceBarrier(1, &begin);
                other.bind_target(list);
                const float clear_other[4] = {0.2f, 0.1f, 0.05f, 1};
                list->ClearRenderTargetView(other.view, clear_other, 0, nullptr);
                stage.canvas.set_viewport(list);
                stage.canvas.binder.bind_graphics(list);
                list->SetPipelineState(state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0, 0), 0.3f, 0.9f, 0.5f);
                D3D12_RESOURCE_BARRIER end = begin;
                end.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
                list->ResourceBarrier(1, &end);
                stage.canvas.color.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
            },
            error);
        rr::Image a;
        rr::Image b;
        if (!ok || !kit.read_image(stage.canvas.color.resource, stage.canvas.color.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, a, error) ||
            !kit.read_image(other.resource, other.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, b, error)) {
            return false;
        }
        out.add("a", std::move(a));
        out.add("b", std::move(b));
        return true;
    });
    Cases::single(rr::CaseSpec("barrier", "uav_barriers_between_dependent_dispatches").tolerance(rr::Tolerance::exact()).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Cs cs;
        if (!cs.open(kit,
                     "StructuredBuffer<uint> in_buf : register(t0); RWStructuredBuffer<uint> out_buf : register(u0);\n"
                     "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { out_buf[id.x] = in_buf[id.x] * 3 + in_buf[(id.x + 1) % 256] + id.x; }\n", error)) {
            return false;
        }
        HRESULT result = S_OK;
        ID3D12Resource* buffers[2];
        std::vector<std::uint32_t> seed(256);
        for (UINT k = 0; k < 256; ++k) {
            seed[k] = k;
        }
        for (auto& buffer : buffers) {
            buffer = D3d12Tools::device_buffer(kit.rig, 1024, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            if (buffer == nullptr || !D3d12Tools::write_buffer(kit.rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, seed.data(), 1024, error)) {
                return fail_with(error, error.empty() ? "buffer: " + D3d12Tools::hr(result) : error);
            }
        }
        const UINT passes = 6;
        std::vector<Binder> binders(passes);
        std::vector<ID3D12Resource*> sources;
        for (UINT p = 0; p < passes; ++p) {
            if (!binders[p].create(kit, error)) {
                return false;
            }
            binders[p].srv_buffer(0, buffers[p % 2], DXGI_FORMAT_UNKNOWN, 0, 256, 4);
            binders[p].uav_buffer(0, buffers[(p + 1) % 2], DXGI_FORMAT_UNKNOWN, 0, 256, 4);
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                for (UINT p = 0; p < passes; ++p) {
                    ID3D12Resource* in = buffers[p % 2];
                    ID3D12Resource* output = buffers[(p + 1) % 2];
                    D3d12Tools::move(list, in, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    binders[p].bind_compute(list);
                    list->SetPipelineState(cs.pso);
                    list->Dispatch(4, 1, 1);
                    D3d12Tools::move(list, in, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    const D3D12_RESOURCE_BARRIER barrier = D3d12Tools::uav_barrier(output);
                    list->ResourceBarrier(1, &barrier);
                }
            },
            error);
        std::vector<std::uint32_t> words;
        if (!ok || !D3d12Tools::read_words(kit.rig, buffers[passes % 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 256, words, error)) {
            return false;
        }
        rr::Image image(8, 8, rr::PixelKind::Rgba32Uint);
        std::memcpy(image.bytes().data(), words.data(), 1024);
        out.add("", std::move(image));
        return true;
    });
}

void register_promotion() {
    Cases::single(rr::CaseSpec("barrier", "buffer_implicit_promotion_and_decay_across_lists").tolerance(rr::Tolerance::blend(2)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        const MeshData mesh = Shapes::torus(24, 12, 0.85f, 0.3f);
        std::vector<std::uint16_t> indices(mesh.indices.begin(), mesh.indices.end());
        const UINT64 vertex_bytes = mesh.vertices.size() * sizeof(Vertex);
        const UINT64 index_bytes = indices.size() * 2;
        ID3D12Resource* vertex_upload = kit.make_upload(mesh.vertices.data(), vertex_bytes, error);
        ID3D12Resource* index_upload = kit.make_upload(indices.data(), index_bytes, error);
        HRESULT result = S_OK;
        ID3D12Resource* vb = D3d12Tools::committed(kit.rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, D3d12Tools::buffer_desc(vertex_bytes), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
        ID3D12Resource* ib = D3d12Tools::committed(kit.rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, D3d12Tools::buffer_desc(index_bytes), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (vertex_upload == nullptr || index_upload == nullptr || vb == nullptr || ib == nullptr || state == nullptr) {
            return fail_with(error, error.empty() ? "buffer: " + D3d12Tools::hr(result) : error);
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                list->CopyBufferRegion(vb, 0, vertex_upload, 0, vertex_bytes);
                list->CopyBufferRegion(ib, 0, index_upload, 0, index_bytes);
            }, error)) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                const D3D12_VERTEX_BUFFER_VIEW vbv{vb->GetGPUVirtualAddress(), static_cast<UINT>(vertex_bytes), sizeof(Vertex)};
                const D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), static_cast<UINT>(index_bytes), DXGI_FORMAT_R16_UINT};
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &vbv);
                list->IASetIndexBuffer(&ibv);
                const float color[4] = {0.9f, 0.6f, 0.2f, 1};
                set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(0.9f), color);
                list->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(rr::CaseSpec("barrier", "texture_simultaneous_access_promotion").tolerance(rr::Tolerance::blend(2)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.width = 64;
        spec.height = 64;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        HRESULT result = S_OK;
        const D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
        ID3D12Resource* texture = D3d12Tools::committed(kit.rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, desc, D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
        if (texture == nullptr) {
            return fail_with(error, "simultaneous-access texture: " + D3d12Tools::hr(result));
        }
        const Texels::Bytes pixels = Texels::content(3, 64, 64);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 row_size = 0;
        UINT64 total = 0;
        kit.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
        std::vector<std::uint8_t> staged(total, 0);
        for (UINT y = 0; y < rows; ++y) {
            std::memcpy(staged.data() + y * footprint.Footprint.RowPitch, pixels.data() + y * 256, 256);
        }
        ID3D12Resource* upload = kit.make_upload(staged.data(), total, error);
        if (upload == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = texture;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = upload;
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = footprint;
                list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }, error)) {
            return false;
        }
        const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const float tint[4] = {1, 1, 1, 1};
        const float scale[4] = {1, 1, 0, 0};
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        stage.canvas.binder.srv_texture(0, texture, DXGI_FORMAT_R8G8B8A8_UNORM);
        stage.canvas.binder.sampler(0, sampler);
        Pso builder = stage.pso(Hlsl12::kTexPs);
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_y(0.5f), tint, scale);
                stage.sphere.bind(list);
                stage.sphere.draw(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(rr::CaseSpec("barrier", "per_subresource_transition_mip_target").tolerance(rr::Tolerance::blend(2)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        TextureSpec spec;
        spec.width = 128;
        spec.height = 128;
        spec.mips = 3;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
        HRESULT result = S_OK;
        ID3D12DescriptorHeap* rtv_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &result);
        if (texture == nullptr || rtv_heap == nullptr) {
            return false;
        }
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = 1;
        kit.device()->CreateRenderTargetView(texture, &rtv, rtv_heap->GetCPUDescriptorHandleForHeapStart());
        stage.canvas.binder.srv_texture(0, texture, DXGI_FORMAT_R8G8B8A8_UNORM, 3);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1);
        stage.canvas.binder.srv(0, texture, &srv);
        Pso mesh_pso = stage.pso();
        Pso sample_pso = stage.pso(Hlsl12::kTexPs);
        ID3D12PipelineState* draw_state = mesh_pso.build(error);
        ID3D12PipelineState* sample_state = sample_pso.build(error);
        if (draw_state == nullptr || sample_state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 1);
                const D3D12_CPU_DESCRIPTOR_HANDLE view = rtv_heap->GetCPUDescriptorHandleForHeapStart();
                list->OMSetRenderTargets(1, &view, FALSE, nullptr);
                const float clear[4] = {0.1f, 0.1f, 0.2f, 1};
                list->ClearRenderTargetView(view, clear, 0, nullptr);
                const D3D12_VIEWPORT vp = viewport_of(64, 64);
                const D3D12_RECT scissor = rect_of(64, 64);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                stage.canvas.binder.bind_graphics(list);
                list->SetPipelineState(draw_state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 1);
                const float background[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, background);
                list->SetPipelineState(sample_state);
                const float tint[4] = {1, 1, 1, 1};
                const float scale[4] = {1, 1, 0, 0};
                set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.4f), tint, scale);
                stage.plane.bind(list);
                stage.plane.draw(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

void register_depth_readonly() {
    Cases::single(rr::CaseSpec("barrier", "read_only_depth_dsv_while_sampling_depth").tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        stage.canvas.binder.srv_texture(0, stage.canvas.depth.resource, DXGI_FORMAT_R32_FLOAT);
        Pso opaque = stage.pso();
        const char* soft =
            "Texture2D<float> depth : register(t0);\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "float4 main(VsOut i) : SV_Target { float d = depth.Load(int3(i.pos.xy, 0)); float fade = saturate((d - i.pos.z) * 60.0); return float4(p0.rgb, fade * 0.9); }\n";
        Pso particle = stage.pso(soft);
        particle.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        D3D12_RENDER_TARGET_BLEND_DESC& blend = particle.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ID3D12PipelineState* opaque_state = opaque.build(error);
        ID3D12PipelineState* particle_state = particle.build(error);
        if (opaque_state == nullptr || particle_state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(opaque_state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0, 0), 0.9f, 0.5f, 0.2f);
                stage.canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                stage.canvas.color.bind_target(list, &stage.canvas.depth, true);
                list->SetPipelineState(particle_state);
                stage.mesh(list, stage.plane, rr::Mat4::rotate_y(0.9f) * rr::Mat4::translate(0, 0, -0.2f), 0.3f, 0.9f, 0.6f);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

bool run_pass_case(Kit& kit, int variant, rr::CaptureSet& out, std::string& error) {
    const UINT samples = variant == 2 ? 4 : 1;
    Stage stage;
    Surface resolved;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 128, samples) || !resolved.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
        return false;
    }
    Pso builder = stage.pso();
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    bool supported = true;
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            ID3D12GraphicsCommandList4* list4 = nullptr;
            if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&list4)))) {
                supported = false;
                return;
            }
            Canvas& canvas = stage.canvas;
            canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            canvas.color.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (variant == 2) {
                resolved.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            }
            D3D12_RENDER_PASS_RENDER_TARGET_DESC rt{};
            rt.cpuDescriptor = canvas.color.view;
            rt.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            rt.BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
            std::memcpy(rt.BeginningAccess.Clear.ClearValue.Color, clear, sizeof clear);
            rt.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
            D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS subresource{};
            if (variant == 2) {
                subresource.SrcRect = {0, 0, 0, 0};
                rt.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
                rt.EndingAccess.Resolve.pSrcResource = canvas.color.resource;
                rt.EndingAccess.Resolve.pDstResource = resolved.resource;
                rt.EndingAccess.Resolve.SubresourceCount = 1;
                rt.EndingAccess.Resolve.pSubresourceParameters = &subresource;
                rt.EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                rt.EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
                rt.EndingAccess.Resolve.PreserveResolveSource = FALSE;
            }
            D3D12_RENDER_PASS_DEPTH_STENCIL_DESC ds{};
            ds.cpuDescriptor = canvas.depth.view;
            ds.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            ds.DepthBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            ds.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 1.0f;
            ds.DepthEndingAccess.Type = variant == 3 ? D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD : D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
            ds.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
            ds.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
            list4->BeginRenderPass(1, &rt, &ds, D3D12_RENDER_PASS_FLAG_NONE);
            canvas.set_viewport(list);
            canvas.binder.bind_graphics(list);
            stage.backdrop(list);
            list->SetPipelineState(state);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.25f, 0, 0), 0.9f, 0.5f, 0.2f);
            list4->EndRenderPass();
            if (variant == 1) {
                D3D12_RENDER_PASS_RENDER_TARGET_DESC second{};
                second.cpuDescriptor = canvas.color.view;
                second.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                second.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
                D3D12_RENDER_PASS_DEPTH_STENCIL_DESC second_ds{};
                second_ds.cpuDescriptor = canvas.depth.view;
                second_ds.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                second_ds.DepthEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
                second_ds.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
                second_ds.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
                list4->BeginRenderPass(1, &second, &second_ds, D3D12_RENDER_PASS_FLAG_NONE);
                canvas.set_viewport(list);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f) * rr::Mat4::translate(0.2f, 0, 0), 0.2f, 0.8f, 0.5f);
                list4->EndRenderPass();
            }
            list4->Release();
        },
        error);
    if (!ok) {
        return false;
    }
    if (!supported) {
        return fail_with(error, "ID3D12GraphicsCommandList4 is not available");
    }
    rr::Image image;
    const bool read = variant == 2 ? kit.read_image(resolved.resource, resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)
                                   : kit.read_image(stage.canvas.color.resource, stage.canvas.color.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error);
    if (!read) {
        return false;
    }
    out.add("", std::move(image));
    return true;
}

void register_render_passes() {
    const char* names[] = {"render_pass_clear_preserve", "render_pass_two_passes_preserve", "render_pass_msaa_resolve_at_end", "render_pass_depth_discard_at_end"};
    for (int variant = 0; variant < 4; ++variant) {
        Cases::single(
            rr::CaseSpec("barrier", names[variant]).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("renderpass"),
            [variant](Kit& kit, rr::CaptureSet& out, std::string& error) { return run_pass_case(kit, variant, out, error); },
            [](Kit& kit) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
                const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS5, options);
                return SUCCEEDED(result) ? std::string() : std::string("render passes (OPTIONS5)");
            });
    }
}

void register_frames() {
    Cases::frames(rr::CaseSpec("barrier", "feedback_trail_ping_pong_targets").frames(10, {0, 4, 9}).tolerance(rr::Tolerance::blend(3)).tag("barrier"), [](Cases::Steps& steps) {
        struct State {
            Stage stage;
            Surface other;
            ID3D12PipelineState* draw = nullptr;
            ID3D12PipelineState* fade = nullptr;
            Surface* surfaces[2] = {nullptr, nullptr};
        };
        auto state = std::make_shared<State>();
        steps.on_setup = [state](Kit& kit, std::string& error) {
            if (!state->stage.open(kit, error) || !state->other.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                return false;
            }
            state->surfaces[0] = &state->stage.canvas.color;
            state->surfaces[1] = &state->other;
            state->stage.canvas.binder.srv_texture(0, state->surfaces[0]->resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            state->stage.canvas.binder.srv_texture(1, state->surfaces[1]->resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            Pso mesh_pso = state->stage.pso();
            const char* fade_ps =
                "Texture2D<float4> a : register(t0); Texture2D<float4> b : register(t1);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float4 c = p1.x < 0.5 ? a.Load(int3(pos.xy, 0)) : b.Load(int3(pos.xy, 0)); return float4(c.rgb * 0.9, 1); }\n";
            Pso fade_pso(kit, state->stage.canvas.binder.signature);
            fade_pso.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(fade_ps)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            state->draw = mesh_pso.build(error);
            state->fade = fade_pso.build(error);
            if (state->draw == nullptr || state->fade == nullptr) {
                return false;
            }
            return kit.run([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.02f, 0.02f, 0.05f, 1};
                for (Surface* surface : state->surfaces) {
                    surface->bind_target(list);
                    list->ClearRenderTargetView(surface->view, clear, 0, nullptr);
                }
            }, error);
        };
        steps.on_frame = [state](Kit& kit, unsigned frame, std::string& error) {
            Surface* previous = state->surfaces[frame % 2];
            Surface* current = state->surfaces[(frame + 1) % 2];
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                previous->to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                current->bind_target(list);
                state->stage.canvas.set_viewport(list);
                state->stage.canvas.binder.bind_graphics(list);
                list->SetPipelineState(state->fade);
                const float fade_params[4] = {1, (frame % 2) != 0 ? 1.0f : 0.0f, 0, 0};
                const float zero[4] = {0, 0, 0, 0};
                set_cbv_root(list, rr::Mat4::identity(), zero, (frame % 2) == 0 ? zero : fade_params);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->DrawInstanced(3, 1, 0, 0);
                list->SetPipelineState(state->draw);
                const float angle = static_cast<float>(frame) * 0.6f;
                state->stage.mesh(list, state->stage.sphere, rr::Mat4::translate(0.55f * std::cos(angle), 0.55f * std::sin(angle), 0) * rr::Mat4::scale(0.3f, 0.3f, 0.3f),
                                  0.9f - 0.07f * static_cast<float>(frame), 0.4f + 0.05f * static_cast<float>(frame), 0.3f);
            }, error);
        };
        steps.on_capture = [state](Kit& kit, unsigned frame, rr::CaptureSet& out, std::string& error) {
            Surface* current = state->surfaces[(frame + 1) % 2];
            rr::Image image;
            if (!kit.read_image(current->resource, current->state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        };
    });
}

const struct Registrar {
    Registrar() {
        register_transitions();
        register_promotion();
        register_depth_readonly();
        register_render_passes();
        register_frames();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
