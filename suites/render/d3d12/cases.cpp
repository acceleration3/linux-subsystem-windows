#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/d3d/hlsl.hpp"
#include "render/d3d12/d3d12.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

bool fail(std::string& error, const std::string& text) {
    error = text;
    return false;
}

const D3D12_INPUT_ELEMENT_DESC kMeshElements[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

class MeshPipeline {
public:
    bool create(Kit& kit, const char* pixel_source, const std::vector<DXGI_FORMAT>& targets, DXGI_FORMAT depth, std::string& error) {
        Kit::Bytes vs;
        Kit::Bytes ps;
        if (!kit.compile(Hlsl::kMeshVs, 'v', vs, error) || !kit.compile(pixel_source, 'p', ps, error)) {
            return false;
        }
        signature_ = D3d12Root().constants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX).flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT).build(kit.rig, error);
        if (signature_ == nullptr) {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = Kit::pipeline_desc(signature_, vs, ps, targets, depth);
        desc.InputLayout = {kMeshElements, 2};
        pso_ = D3d12Tools::graphics_pso(kit.rig, desc, error);
        if (pso_ == nullptr) {
            return false;
        }
        const std::vector<rr::ColoredVertex> vertices = rr::Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = rr::Mesh::cube_indices();
        index_count_ = static_cast<UINT>(indices.size());
        vertex_ = kit.make_upload(vertices.data(), vertices.size() * sizeof(rr::ColoredVertex), error);
        index_ = kit.make_upload(indices.data(), indices.size() * 2, error);
        if (vertex_ == nullptr || index_ == nullptr) {
            return false;
        }
        vbv_ = {vertex_->GetGPUVirtualAddress(), static_cast<UINT>(vertices.size() * sizeof(rr::ColoredVertex)), sizeof(rr::ColoredVertex)};
        ibv_ = {index_->GetGPUVirtualAddress(), static_cast<UINT>(indices.size() * 2), DXGI_FORMAT_R16_UINT};
        return true;
    }

    void bind(ID3D12GraphicsCommandList* list) const {
        list->SetGraphicsRootSignature(signature_);
        list->SetPipelineState(pso_);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &vbv_);
        list->IASetIndexBuffer(&ibv_);
    }

    void draw(ID3D12GraphicsCommandList* list, const rr::Mat4& transform) const {
        list->SetGraphicsRoot32BitConstants(0, 16, transform.m, 0);
        list->DrawIndexedInstanced(index_count_, 1, 0, 0, 0);
    }

    ID3D12RootSignature* signature_ = nullptr;
    ID3D12PipelineState* pso_ = nullptr;
    ID3D12Resource* vertex_ = nullptr;
    ID3D12Resource* index_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};
    UINT index_count_ = 0;
};

void viewport(ID3D12GraphicsCommandList* list, UINT width, UINT height) {
    const D3D12_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &scissor);
}

class IndexedCubeDepth : public D3d12SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Target target;
        Depth depth;
        MeshPipeline mesh;
        if (!kit.make_target(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error) || !kit.make_depth(depth, 128, 128, error) ||
            !mesh.create(kit, Hlsl::kMeshPs, {DXGI_FORMAT_R8G8B8A8_UNORM}, DXGI_FORMAT_D32_FLOAT, error)) {
            return false;
        }
        const rr::Mat4 projection = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f);
        const rr::Mat4 view = rr::Mat4::translate(0, 0, -6.0f);
        const rr::Mat4 first = projection * view * rr::Mat4::translate(-0.6f, 0.0f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f);
        const rr::Mat4 second = projection * view * rr::Mat4::translate(0.7f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f) *
                                rr::Mat4::scale(0.9f, 0.9f, 0.9f);
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.1f, 0.1f, 0.15f, 1.0f};
                list->OMSetRenderTargets(1, &target.view, FALSE, &depth.view);
                list->ClearRenderTargetView(target.view, clear, 0, nullptr);
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                viewport(list, 128, 128);
                mesh.bind(list);
                mesh.draw(list, first);
                mesh.draw(list, second);
            },
            error);
        rr::Image color;
        if (!ok || !kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d12Register<IndexedCubeDepth> kIndexedCubeDepth{rr::CaseSpec("geometry", "indexed_cube_depth").tolerance(rr::Tolerance::blend(2))};
const D3d12Register<IndexedCubeDepth> kIndexedCubeDepthDxc{
    rr::CaseSpec("shader", "indexed_cube_depth_sm6").variant(1).tolerance(rr::Tolerance::blend(2))};

class MrtDepthCapture : public D3d12SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Target first;
        Target second;
        Depth depth;
        MeshPipeline mesh;
        if (!kit.make_target(first, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error) ||
            !kit.make_target(second, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, error) || !kit.make_depth(depth, 128, 128, error) ||
            !mesh.create(kit, Hlsl::kMrtPs, {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT}, DXGI_FORMAT_D32_FLOAT, error)) {
            return false;
        }
        const rr::Mat4 transform = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -5.0f) * rr::Mat4::rotate_y(0.7f) *
                                   rr::Mat4::rotate_x(-0.5f);
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0, 0, 0, 1};
                const D3D12_CPU_DESCRIPTOR_HANDLE views[2] = {first.view, second.view};
                list->OMSetRenderTargets(2, views, FALSE, &depth.view);
                list->ClearRenderTargetView(first.view, clear, 0, nullptr);
                list->ClearRenderTargetView(second.view, clear, 0, nullptr);
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                viewport(list, 128, 128);
                mesh.bind(list);
                mesh.draw(list, transform);
            },
            error);
        rr::Image c0;
        rr::Image c1;
        rr::Image d;
        if (!ok || !kit.read_color(first, c0, error) || !kit.read_color(second, c1, error) || !kit.read_depth(depth, d, error)) {
            return false;
        }
        out.add("color0", std::move(c0));
        out.add("color1", std::move(c1));
        out.add("depth", std::move(d));
        return true;
    }
};

const D3d12Register<MrtDepthCapture> kMrtDepthCapture{rr::CaseSpec("targets", "mrt_depth_capture").tolerance(rr::Tolerance::blend(2))};

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class MippedQuadBlend : public D3d12SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        constexpr UINT kSize = 64;
        const unsigned levels = rr::Pattern::level_count(kSize, kSize);
        const std::vector<rr::Pattern::Bytes> chain = rr::Pattern::mip_chain(
            rr::Pattern::checker(kSize, kSize, 4, rr::Pattern::pack(240, 240, 240), rr::Pattern::pack(220, 40, 40)), kSize, kSize, levels);
        HRESULT result = S_OK;
        ID3D12Resource* texture = D3d12Tools::texture(kit.rig, D3d12Tools::texture_desc(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, levels),
                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, &result);
        if (texture == nullptr) {
            return fail(error, "texture creation: " + D3d12Tools::hr(result));
        }
        for (unsigned level = 0; level < levels; ++level) {
            if (!D3d12Tools::write_texture(kit.rig, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, level, chain[level].data(), error)) {
                return false;
            }
        }
        ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
        if (heap == nullptr) {
            return fail(error, "SRV heap creation: " + D3d12Tools::hr(result));
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = levels;
        kit.device()->CreateShaderResourceView(texture, &srv, heap->GetCPUDescriptorHandleForHeapStart());
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD = FLT_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ID3D12RootSignature* signature = D3d12Root()
                                             .constants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL)
                                             .table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL)
                                             .sampler(sampler)
                                             .flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)
                                             .build(kit.rig, error);
        Kit::Bytes vs;
        Kit::Bytes ps;
        if (signature == nullptr || !kit.compile(Hlsl::kTexVs, 'v', vs, error) || !kit.compile(Hlsl::kTexPs, 'p', ps, error)) {
            return false;
        }
        static const D3D12_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = Kit::pipeline_desc(signature, vs, ps, {DXGI_FORMAT_R8G8B8A8_UNORM}, DXGI_FORMAT_UNKNOWN);
        desc.InputLayout = {elements, 2};
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        ID3D12PipelineState* pso = D3d12Tools::graphics_pso(kit.rig, desc, error);
        const QuadVertex quads[8] = {
            {-0.9f, 0.9f, 0.0f, 0.0f}, {0.5f, 0.9f, 1.0f, 0.0f}, {-0.9f, -0.5f, 0.0f, 1.0f}, {0.5f, -0.5f, 1.0f, 1.0f},
            {-0.4f, 0.4f, 0.0f, 0.0f}, {0.9f, 0.4f, 6.0f, 0.0f}, {-0.4f, -0.9f, 0.0f, 6.0f}, {0.9f, -0.9f, 6.0f, 6.0f},
        };
        ID3D12Resource* vertices = kit.make_upload(quads, sizeof quads, error);
        Target target;
        if (pso == nullptr || vertices == nullptr || !kit.make_target(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
            return false;
        }
        const D3D12_VERTEX_BUFFER_VIEW vbv{vertices->GetGPUVirtualAddress(), sizeof quads, sizeof(QuadVertex)};
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.15f, 0.3f, 0.55f, 1.0f};
                list->OMSetRenderTargets(1, &target.view, FALSE, nullptr);
                list->ClearRenderTargetView(target.view, clear, 0, nullptr);
                viewport(list, 128, 128);
                list->SetGraphicsRootSignature(signature);
                list->SetPipelineState(pso);
                list->SetDescriptorHeaps(1, &heap);
                list->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                list->IASetVertexBuffers(0, 1, &vbv);
                const float opaque[4] = {1, 1, 1, 1};
                const float ghost[4] = {1, 1, 1, 0.5f};
                list->SetGraphicsRoot32BitConstants(0, 4, opaque, 0);
                list->DrawInstanced(4, 1, 0, 0);
                list->SetGraphicsRoot32BitConstants(0, 4, ghost, 0);
                list->DrawInstanced(4, 1, 4, 0);
            },
            error);
        rr::Image color;
        if (!ok || !kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d12Register<MippedQuadBlend> kMippedQuadBlend{rr::CaseSpec("texture", "mipped_quad_blend").tolerance(rr::Tolerance::filter(4, 0.002))};

constexpr UINT kRingSlots = 4;
constexpr UINT kStampVertices = 12;

class RingAccumulate : public D3d12Scene {
public:
    bool setup(Kit& kit, std::string& error) override {
        if (!mesh_.create(kit, Hlsl::kMeshPs, {DXGI_FORMAT_R8G8B8A8_UNORM}, DXGI_FORMAT_UNKNOWN, error) ||
            !kit.make_target(target_, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
            return false;
        }
        slot_bytes_ = kStampVertices * sizeof(rr::ColoredVertex);
        ring_ = kit.make_upload(nullptr, slot_bytes_ * kRingSlots, error);
        if (ring_ == nullptr) {
            return false;
        }
        const D3D12_RANGE none{0, 0};
        const HRESULT result = ring_->Map(0, &none, &mapped_);
        if (FAILED(result)) {
            return fail(error, "Map of the vertex ring: " + D3d12Tools::hr(result));
        }
        return true;
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        const UINT slot = index % kRingSlots;
        rr::Mesh::stamp(reinterpret_cast<rr::ColoredVertex*>(static_cast<std::uint8_t*>(mapped_) + slot * slot_bytes_), index);
        const rr::Mat4 transform = rr::Mat4::rotate_z(0.05f * static_cast<float>(index));
        const D3D12_VERTEX_BUFFER_VIEW vbv{ring_->GetGPUVirtualAddress() + slot * slot_bytes_, static_cast<UINT>(slot_bytes_), sizeof(rr::ColoredVertex)};
        return kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                list->OMSetRenderTargets(1, &target_.view, FALSE, nullptr);
                if (index == 0) {
                    const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
                    list->ClearRenderTargetView(target_.view, clear, 0, nullptr);
                }
                viewport(list, 128, 128);
                mesh_.bind(list);
                list->IASetVertexBuffers(0, 1, &vbv);
                list->SetGraphicsRoot32BitConstants(0, 16, transform.m, 0);
                list->DrawInstanced(kStampVertices, 1, 0, 0);
            },
            error);
    }

    bool capture(Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) override {
        rr::Image color;
        if (!kit.read_color(target_, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }

private:
    MeshPipeline mesh_;
    Target target_;
    ID3D12Resource* ring_ = nullptr;
    void* mapped_ = nullptr;
    std::size_t slot_bytes_ = 0;
};

const D3d12Register<RingAccumulate> kRingAccumulate{
    rr::CaseSpec("frames", "ring_accumulate").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::blend(2))};

}  // namespace

}  // namespace lsw::suites::render::d3d12
