#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/d3d/hlsl.hpp"
#include "render/d3d10/d3d10.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::render::d3d10 {

namespace {

bool fail(std::string& error, const std::string& text) {
    error = text;
    return false;
}

class MeshPipeline {
public:
    bool create(Kit& kit, const char* pixel_source, std::string& error) {
        std::vector<std::uint8_t> code;
        vs_ = kit.vertex_shader(Hlsl::kMeshVs, code, error);
        ps_ = kit.pixel_shader(pixel_source, error);
        if (!vs_ || !ps_) {
            return false;
        }
        const D3D10_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
        };
        HRESULT result = kit.device()->CreateInputLayout(elements, 2, code.data(), code.size(), layout_.put());
        if (FAILED(result)) {
            return fail(error, "CreateInputLayout: " + D3d10Tools::hr(result));
        }
        const std::vector<rr::ColoredVertex> vertices = rr::Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = rr::Mesh::cube_indices();
        index_count_ = static_cast<UINT>(indices.size());
        vertex_ = kit.buffer(static_cast<UINT>(vertices.size() * sizeof(rr::ColoredVertex)), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0,
                             vertices.data(), error);
        index_ = kit.buffer(static_cast<UINT>(indices.size() * 2), D3D10_USAGE_DEFAULT, D3D10_BIND_INDEX_BUFFER, 0, indices.data(), error);
        constants_ = kit.buffer(64, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, nullptr, error);
        D3D10_RASTERIZER_DESC raster{};
        raster.FillMode = D3D10_FILL_SOLID;
        raster.CullMode = D3D10_CULL_BACK;
        raster.FrontCounterClockwise = TRUE;
        raster.DepthClipEnable = TRUE;
        D3D10_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D10_COMPARISON_LESS;
        if (!vertex_ || !index_ || !constants_ || FAILED(kit.device()->CreateRasterizerState(&raster, raster_.put())) ||
            FAILED(kit.device()->CreateDepthStencilState(&depth, depth_.put()))) {
            return error.empty() ? fail(error, "pipeline state creation failed") : false;
        }
        return true;
    }

    void bind(Kit& kit) {
        ID3D10Device* c = kit.device();
        const UINT stride = sizeof(rr::ColoredVertex);
        const UINT offset = 0;
        ID3D10Buffer* vb = vertex_.get();
        ID3D10Buffer* cb = constants_.get();
        c->IASetInputLayout(layout_.get());
        c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IASetIndexBuffer(index_.get(), DXGI_FORMAT_R16_UINT, 0);
        c->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs_.get());
        c->VSSetConstantBuffers(0, 1, &cb);
        c->PSSetShader(ps_.get());
        c->RSSetState(raster_.get());
        c->OMSetDepthStencilState(depth_.get(), 0);
    }

    void draw(Kit& kit, const rr::Mat4& transform) {
        kit.device()->UpdateSubresource(constants_.get(), 0, nullptr, transform.m, 0, 0);
        kit.device()->DrawIndexed(index_count_, 0, 0);
    }

    Com<ID3D10VertexShader> vs_;
    Com<ID3D10PixelShader> ps_;
    Com<ID3D10InputLayout> layout_;
    Com<ID3D10Buffer> vertex_;
    Com<ID3D10Buffer> index_;
    Com<ID3D10Buffer> constants_;
    Com<ID3D10RasterizerState> raster_;
    Com<ID3D10DepthStencilState> depth_;
    UINT index_count_ = 0;
};

class IndexedCubeDepth : public D3d10SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Target target;
        Depth depth;
        MeshPipeline mesh;
        if (!kit.make_target(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 1, error) ||
            !kit.make_depth(depth, 128, 128, DXGI_FORMAT_D32_FLOAT, 1, error) || !mesh.create(kit, Hlsl::kMeshPs, error)) {
            return false;
        }
        ID3D10Device* c = kit.device();
        const float clear[4] = {0.1f, 0.1f, 0.15f, 1.0f};
        ID3D10RenderTargetView* rtv = target.view.get();
        c->OMSetRenderTargets(1, &rtv, depth.view.get());
        c->ClearRenderTargetView(rtv, clear);
        c->ClearDepthStencilView(depth.view.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
        kit.set_viewport(128, 128);
        mesh.bind(kit);
        const rr::Mat4 projection = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f);
        const rr::Mat4 view = rr::Mat4::translate(0, 0, -6.0f);
        mesh.draw(kit, projection * view * rr::Mat4::translate(-0.6f, 0.0f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f));
        mesh.draw(kit, projection * view * rr::Mat4::translate(0.7f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f) *
                           rr::Mat4::scale(0.9f, 0.9f, 0.9f));
        rr::Image color;
        if (!kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d10Register<IndexedCubeDepth> kIndexedCubeDepth{rr::CaseSpec("geometry", "indexed_cube_depth").tolerance(rr::Tolerance::blend(2))};

class MrtDepthCapture : public D3d10SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Target first;
        Target second;
        Depth depth;
        MeshPipeline mesh;
        if (!kit.make_target(first, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 1, error) ||
            !kit.make_target(second, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, error) ||
            !kit.make_depth(depth, 128, 128, DXGI_FORMAT_D32_FLOAT, 1, error) || !mesh.create(kit, Hlsl::kMrtPs, error)) {
            return false;
        }
        ID3D10Device* c = kit.device();
        const float clear[4] = {0, 0, 0, 1};
        ID3D10RenderTargetView* views[2] = {first.view.get(), second.view.get()};
        c->OMSetRenderTargets(2, views, depth.view.get());
        c->ClearRenderTargetView(views[0], clear);
        c->ClearRenderTargetView(views[1], clear);
        c->ClearDepthStencilView(depth.view.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
        kit.set_viewport(128, 128);
        mesh.bind(kit);
        mesh.draw(kit, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -5.0f) * rr::Mat4::rotate_y(0.7f) *
                           rr::Mat4::rotate_x(-0.5f));
        rr::Image c0;
        rr::Image c1;
        rr::Image d;
        if (!kit.read_color(first, c0, error) || !kit.read_color(second, c1, error) || !kit.read_depth(depth, d, error)) {
            return false;
        }
        out.add("color0", std::move(c0));
        out.add("color1", std::move(c1));
        out.add("depth", std::move(d));
        return true;
    }
};

const D3d10Register<MrtDepthCapture> kMrtDepthCapture{rr::CaseSpec("targets", "mrt_depth_capture").variant(1).tolerance(rr::Tolerance::blend(2))};

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class MippedQuadBlend : public D3d10SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        constexpr UINT kSize = 64;
        const unsigned levels = rr::Pattern::level_count(kSize, kSize);
        const std::vector<rr::Pattern::Bytes> chain = rr::Pattern::mip_chain(
            rr::Pattern::checker(kSize, kSize, 4, rr::Pattern::pack(240, 240, 240), rr::Pattern::pack(220, 40, 40)), kSize, kSize, levels);
        std::vector<D3D10_SUBRESOURCE_DATA> data(levels);
        for (unsigned i = 0; i < levels; ++i) {
            data[i].pSysMem = chain[i].data();
            data[i].SysMemPitch = std::max(1u, kSize >> i) * 4;
        }
        HRESULT result = S_OK;
        Com<ID3D10Texture2D> texture = D3d10Tools::texture(
            kit.dev, D3d10Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, levels),
            data.data(), &result);
        if (!texture) {
            return fail(error, "texture creation: " + D3d10Tools::hr(result));
        }
        Com<ID3D10ShaderResourceView> srv;
        if (texture) {
            kit.device()->CreateShaderResourceView(texture.get(), nullptr, srv.put());
        }
        D3D10_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxLOD = D3D10_FLOAT32_MAX;
        sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
        Com<ID3D10SamplerState> sampler;
        D3D10_BLEND_DESC blend_desc{};
        blend_desc.BlendEnable[0] = TRUE;
        blend_desc.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        blend_desc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
        blend_desc.SrcBlendAlpha = D3D10_BLEND_ONE;
        blend_desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
        blend_desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
        Com<ID3D10BlendState> blend;
        D3D10_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D10_FILL_SOLID;
        raster_desc.CullMode = D3D10_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        Com<ID3D10RasterizerState> raster;
        if (!srv || FAILED(kit.device()->CreateSamplerState(&sampler_desc, sampler.put())) ||
            FAILED(kit.device()->CreateBlendState(&blend_desc, blend.put())) || FAILED(kit.device()->CreateRasterizerState(&raster_desc, raster.put()))) {
            return fail(error, "state creation failed");
        }
        std::vector<std::uint8_t> code;
        Com<ID3D10VertexShader> vs = kit.vertex_shader(Hlsl::kTexVs, code, error);
        Com<ID3D10PixelShader> ps = kit.pixel_shader(Hlsl::kTexPs, error);
        if (!vs || !ps) {
            return false;
        }
        const D3D10_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D10_INPUT_PER_VERTEX_DATA, 0},
        };
        Com<ID3D10InputLayout> layout;
        if (FAILED(kit.device()->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()))) {
            return fail(error, "CreateInputLayout failed");
        }
        const QuadVertex quads[8] = {
            {-0.9f, 0.9f, 0.0f, 0.0f}, {0.5f, 0.9f, 1.0f, 0.0f}, {-0.9f, -0.5f, 0.0f, 1.0f}, {0.5f, -0.5f, 1.0f, 1.0f},
            {-0.4f, 0.4f, 0.0f, 0.0f}, {0.9f, 0.4f, 6.0f, 0.0f}, {-0.4f, -0.9f, 0.0f, 6.0f}, {0.9f, -0.9f, 6.0f, 6.0f},
        };
        Com<ID3D10Buffer> vertices = kit.buffer(sizeof quads, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, quads, error);
        Com<ID3D10Buffer> tint = kit.buffer(16, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, nullptr, error);
        Target target;
        if (!vertices || !tint || !kit.make_target(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 1, error)) {
            return false;
        }
        ID3D10Device* c = kit.device();
        ID3D10RenderTargetView* rtv = target.view.get();
        const float clear[4] = {0.15f, 0.3f, 0.55f, 1.0f};
        c->OMSetRenderTargets(1, &rtv, nullptr);
        c->ClearRenderTargetView(rtv, clear);
        kit.set_viewport(128, 128);
        const UINT stride = sizeof(QuadVertex);
        const UINT offset = 0;
        ID3D10Buffer* vb = vertices.get();
        ID3D10Buffer* cb = tint.get();
        ID3D10ShaderResourceView* view = srv.get();
        ID3D10SamplerState* smp = sampler.get();
        c->IASetInputLayout(layout.get());
        c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        c->VSSetShader(vs.get());
        c->PSSetShader(ps.get());
        c->PSSetConstantBuffers(0, 1, &cb);
        c->PSSetShaderResources(0, 1, &view);
        c->PSSetSamplers(0, 1, &smp);
        c->RSSetState(raster.get());
        const float factor[4] = {1, 1, 1, 1};
        c->OMSetBlendState(blend.get(), factor, 0xffffffffu);
        const float opaque[4] = {1, 1, 1, 1};
        const float ghost[4] = {1, 1, 1, 0.5f};
        c->UpdateSubresource(tint.get(), 0, nullptr, opaque, 0, 0);
        c->Draw(4, 0);
        c->UpdateSubresource(tint.get(), 0, nullptr, ghost, 0, 0);
        c->Draw(4, 4);
        rr::Image color;
        if (!kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d10Register<MippedQuadBlend> kMippedQuadBlend{rr::CaseSpec("texture", "mipped_quad_blend").tolerance(rr::Tolerance::filter(4, 0.002))};

constexpr UINT kRingSlots = 4;
constexpr UINT kStampVertices = 12;

class RingAccumulate : public D3d10Scene {
public:
    bool setup(Kit& kit, std::string& error) override {
        if (!mesh_.create(kit, Hlsl::kMeshPs, error) || !kit.make_target(target_, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 1, error)) {
            return false;
        }
        slot_bytes_ = kStampVertices * sizeof(rr::ColoredVertex);
        ring_ = kit.buffer(static_cast<UINT>(slot_bytes_ * kRingSlots), D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, nullptr, error);
        constants_ = kit.buffer(64, D3D10_USAGE_DYNAMIC, D3D10_BIND_CONSTANT_BUFFER, D3D10_CPU_ACCESS_WRITE, nullptr, error);
        return ring_ && constants_;
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        ID3D10Device* c = kit.device();
        const UINT slot = index % kRingSlots;
        void* mapped = nullptr;
        HRESULT result = ring_->Map(slot == 0 ? D3D10_MAP_WRITE_DISCARD : D3D10_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(result)) {
            return fail(error, "Map of the vertex ring: " + D3d10Tools::hr(result));
        }
        rr::Mesh::stamp(reinterpret_cast<rr::ColoredVertex*>(static_cast<std::uint8_t*>(mapped) + slot * slot_bytes_), index);
        ring_->Unmap();
        const rr::Mat4 transform = rr::Mat4::rotate_z(0.05f * static_cast<float>(index));
        result = constants_->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result)) {
            return fail(error, "Map of the constants: " + D3d10Tools::hr(result));
        }
        std::memcpy(mapped, transform.m, 64);
        constants_->Unmap();
        ID3D10RenderTargetView* rtv = target_.view.get();
        c->OMSetRenderTargets(1, &rtv, nullptr);
        if (index == 0) {
            const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
            c->ClearRenderTargetView(rtv, clear);
        }
        kit.set_viewport(128, 128);
        mesh_.bind(kit);
        const UINT stride = sizeof(rr::ColoredVertex);
        const UINT offset = static_cast<UINT>(slot * slot_bytes_);
        ID3D10Buffer* vb = ring_.get();
        ID3D10Buffer* cb = constants_.get();
        c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->VSSetConstantBuffers(0, 1, &cb);
        c->OMSetDepthStencilState(nullptr, 0);
        c->Draw(kStampVertices, 0);
        return true;
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
    Com<ID3D10Buffer> ring_;
    Com<ID3D10Buffer> constants_;
    std::size_t slot_bytes_ = 0;
};

const D3d10Register<RingAccumulate> kRingAccumulate{
    rr::CaseSpec("frames", "ring_accumulate").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::blend(2))};

}  // namespace

}  // namespace lsw::suites::render::d3d10
