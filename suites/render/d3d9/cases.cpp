#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/d3d9/d3d9.hpp"
#include "support/d3d9_tools.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::render::d3d9 {

namespace {

constexpr const char* kMeshVs =
    "float4x4 transform : register(c0);\n"
    "struct VsIn { float3 pos : POSITION; float3 col : COLOR0; };\n"
    "struct VsOut { float4 pos : POSITION; float3 col : TEXCOORD0; float depth : TEXCOORD1; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = mul(transform, float4(i.pos, 1.0)); o.col = i.col; o.depth = o.pos.z / o.pos.w; return o; }\n";
constexpr const char* kMeshPs =
    "struct VsOut { float4 pos : POSITION; float3 col : TEXCOORD0; float depth : TEXCOORD1; };\n"
    "float4 main(VsOut i) : COLOR { return float4(i.col, 1.0); }\n";
constexpr const char* kMrtPs =
    "struct VsOut { float4 pos : POSITION; float3 col : TEXCOORD0; float depth : TEXCOORD1; };\n"
    "struct PsOut { float4 c0 : COLOR0; float4 c1 : COLOR1; };\n"
    "PsOut main(VsOut i) { PsOut o; o.c0 = float4(i.col, 1.0); o.c1 = float4(i.depth, 0.0, 0.0, 1.0); return o; }\n";
constexpr const char* kTexVs =
    "struct VsIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "struct VsOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos, 0.0, 1.0); o.uv = i.uv; return o; }\n";
constexpr const char* kTexPs =
    "float4 tint : register(c0);\n"
    "sampler2D tex : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(tex, uv) * tint; }\n";


bool check(HRESULT result, const char* what, std::string& error) {
    if (SUCCEEDED(result)) {
        return true;
    }
    error = std::string(what) + ": " + D3d9Tools::hr(result);
    return false;
}

Com<IDirect3DVertexDeclaration9> declaration(Kit& kit, const D3DVERTEXELEMENT9* elements, std::string& error) {
    Com<IDirect3DVertexDeclaration9> out;
    if (!check(kit.device()->CreateVertexDeclaration(elements, out.put()), "CreateVertexDeclaration", error)) {
        return {};
    }
    return out;
}

const D3DVERTEXELEMENT9 kMeshElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    D3DDECL_END(),
};

class MeshPipeline {
public:
    bool create(Kit& kit, const char* pixel_source, std::string& error) {
        vs_ = kit.vertex_shader(kMeshVs, error);
        ps_ = kit.pixel_shader(pixel_source, error);
        layout_ = declaration(kit, kMeshElements, error);
        if (!vs_ || !ps_ || !layout_) {
            return false;
        }
        const std::vector<rr::ColoredVertex> vertices = rr::Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = rr::Mesh::cube_indices();
        vertex_count_ = static_cast<UINT>(vertices.size());
        triangle_count_ = static_cast<UINT>(indices.size() / 3);
        IDirect3DDevice9* d = kit.device();
        void* data = nullptr;
        if (!check(d->CreateVertexBuffer(static_cast<UINT>(vertices.size() * sizeof(rr::ColoredVertex)), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                         vertex_.put(), nullptr),
                   "CreateVertexBuffer", error) ||
            !check(vertex_->Lock(0, 0, &data, 0), "Lock", error)) {
            return false;
        }
        std::memcpy(data, vertices.data(), vertices.size() * sizeof(rr::ColoredVertex));
        vertex_->Unlock();
        if (!check(d->CreateIndexBuffer(static_cast<UINT>(indices.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, index_.put(),
                                        nullptr),
                   "CreateIndexBuffer", error) ||
            !check(index_->Lock(0, 0, &data, 0), "Lock", error)) {
            return false;
        }
        std::memcpy(data, indices.data(), indices.size() * 2);
        index_->Unlock();
        return true;
    }

    void bind(Kit& kit, bool depth) {
        IDirect3DDevice9* d = kit.device();
        d->SetVertexDeclaration(layout_.get());
        d->SetVertexShader(vs_.get());
        d->SetPixelShader(ps_.get());
        d->SetStreamSource(0, vertex_.get(), 0, sizeof(rr::ColoredVertex));
        d->SetIndices(index_.get());
        d->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, depth ? TRUE : FALSE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }

    bool draw(Kit& kit, const rr::Mat4& transform, std::string& error) {
        return check(kit.device()->SetVertexShaderConstantF(0, transform.m, 4), "SetVertexShaderConstantF", error) &&
               check(kit.device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertex_count_, 0, triangle_count_), "DrawIndexedPrimitive", error);
    }

    Com<IDirect3DVertexShader9> vs_;
    Com<IDirect3DPixelShader9> ps_;
    Com<IDirect3DVertexDeclaration9> layout_;
    Com<IDirect3DVertexBuffer9> vertex_;
    Com<IDirect3DIndexBuffer9> index_;
    UINT vertex_count_ = 0;
    UINT triangle_count_ = 0;
};

class IndexedCubeDepth : public D3d9SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Surface target;
        Surface depth;
        MeshPipeline mesh;
        if (!kit.make_target(target, 128, 128, D3DFMT_A8R8G8B8, error) || !kit.make_depth(depth, 128, 128, error) ||
            !mesh.create(kit, kMeshPs, error)) {
            return false;
        }
        IDirect3DDevice9* d = kit.device();
        if (!check(d->SetRenderTarget(0, target.surface.get()), "SetRenderTarget", error) ||
            !check(d->SetDepthStencilSurface(depth.surface.get()), "SetDepthStencilSurface", error) ||
            !check(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_COLORVALUE(0.1f, 0.1f, 0.15f, 1.0f), 1.0f, 0), "Clear", error) ||
            !kit.begin(error)) {
            return false;
        }
        mesh.bind(kit, true);
        const rr::Mat4 projection = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f);
        const rr::Mat4 view = rr::Mat4::translate(0, 0, -6.0f);
        if (!mesh.draw(kit, projection * view * rr::Mat4::translate(-0.6f, 0.0f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f), error) ||
            !mesh.draw(kit, projection * view * rr::Mat4::translate(0.7f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f) *
                                rr::Mat4::scale(0.9f, 0.9f, 0.9f), error)) {
            return false;
        }
        rr::Image color;
        if (!kit.end(error) || !kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d9Register<IndexedCubeDepth> kIndexedCubeDepth{rr::CaseSpec("geometry", "indexed_cube_depth").tolerance(rr::Tolerance::blend(2))};

class MrtDepthCapture : public D3d9SingleScene {
public:
    std::string unsupported(Kit& kit) override {
        D3DCAPS9 caps{};
        if (FAILED(kit.device()->GetDeviceCaps(&caps)) || caps.NumSimultaneousRTs < 2) {
            return "two simultaneous render targets";
        }
        return {};
    }

    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Surface first;
        Surface second;
        Surface depth;
        MeshPipeline mesh;
        if (!kit.make_target(first, 128, 128, D3DFMT_A8R8G8B8, error) || !kit.make_target(second, 128, 128, D3DFMT_R32F, error) ||
            !kit.make_depth(depth, 128, 128, error) || !mesh.create(kit, kMrtPs, error)) {
            return false;
        }
        IDirect3DDevice9* d = kit.device();
        d->SetRenderTarget(0, first.surface.get());
        d->SetRenderTarget(1, second.surface.get());
        d->SetDepthStencilSurface(depth.surface.get());
        if (!check(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_COLORVALUE(0, 0, 0, 1), 1.0f, 0), "Clear", error) ||
            !kit.begin(error)) {
            return false;
        }
        mesh.bind(kit, true);
        if (!mesh.draw(kit, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -5.0f) * rr::Mat4::rotate_y(0.7f) *
                                rr::Mat4::rotate_x(-0.5f), error)) {
            return false;
        }
        rr::Image c0;
        rr::Image c1;
        if (!kit.end(error) || !kit.read_color(first, c0, error) || !kit.read_color(second, c1, error)) {
            return false;
        }
        d->SetRenderTarget(1, nullptr);
        out.add("color", std::move(c0));
        out.add("depth", std::move(c1));
        return true;
    }
};

const D3d9Register<MrtDepthCapture> kMrtDepthCapture{rr::CaseSpec("targets", "mrt_depth_capture").tolerance(rr::Tolerance::blend(2))};

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class MippedQuadBlend : public D3d9SingleScene {
public:
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        constexpr UINT kSize = 64;
        const unsigned levels = rr::Pattern::level_count(kSize, kSize);
        const std::vector<rr::Pattern::Bytes> chain = rr::Pattern::mip_chain(
            rr::Pattern::checker(kSize, kSize, 4, rr::Pattern::pack(240, 240, 240), rr::Pattern::pack(220, 40, 40)), kSize, kSize, levels);
        IDirect3DDevice9* d = kit.device();
        Com<IDirect3DTexture9> texture;
        if (!check(d->CreateTexture(kSize, kSize, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture.put(), nullptr), "CreateTexture", error)) {
            return false;
        }
        for (unsigned level = 0; level < levels; ++level) {
            D3DLOCKED_RECT locked{};
            if (!check(texture->LockRect(level, &locked, nullptr, 0), "LockRect", error)) {
                return false;
            }
            const UINT size = std::max(1u, kSize >> level);
            for (UINT y = 0; y < size; ++y) {
                std::uint8_t* row = static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
                for (UINT x = 0; x < size; ++x) {
                    const std::uint8_t* src = chain[level].data() + (static_cast<std::size_t>(y) * size + x) * 4;
                    row[x * 4 + 0] = src[2];
                    row[x * 4 + 1] = src[1];
                    row[x * 4 + 2] = src[0];
                    row[x * 4 + 3] = src[3];
                }
            }
            texture->UnlockRect(level);
        }
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END(),
        };
        Com<IDirect3DVertexDeclaration9> layout = declaration(kit, elements, error);
        Com<IDirect3DVertexShader9> vs = kit.vertex_shader(kTexVs, error);
        Com<IDirect3DPixelShader9> ps = kit.pixel_shader(kTexPs, error);
        if (!layout || !vs || !ps) {
            return false;
        }
        const QuadVertex quads[8] = {
            {-0.9f, 0.9f, 0.0f, 0.0f}, {0.5f, 0.9f, 1.0f, 0.0f}, {-0.9f, -0.5f, 0.0f, 1.0f}, {0.5f, -0.5f, 1.0f, 1.0f},
            {-0.4f, 0.4f, 0.0f, 0.0f}, {0.9f, 0.4f, 6.0f, 0.0f}, {-0.4f, -0.9f, 0.0f, 6.0f}, {0.9f, -0.9f, 6.0f, 6.0f},
        };
        Com<IDirect3DVertexBuffer9> vertices;
        void* data = nullptr;
        if (!check(d->CreateVertexBuffer(sizeof quads, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, vertices.put(), nullptr), "CreateVertexBuffer", error) ||
            !check(vertices->Lock(0, 0, &data, 0), "Lock", error)) {
            return false;
        }
        std::memcpy(data, quads, sizeof quads);
        vertices->Unlock();
        Surface target;
        if (!kit.make_target(target, 128, 128, D3DFMT_A8R8G8B8, error)) {
            return false;
        }
        d->SetRenderTarget(0, target.surface.get());
        d->SetDepthStencilSurface(nullptr);
        if (!check(d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(0.15f, 0.3f, 0.55f, 1.0f), 1.0f, 0), "Clear", error) || !kit.begin(error)) {
            return false;
        }
        d->SetVertexDeclaration(layout.get());
        d->SetVertexShader(vs.get());
        d->SetPixelShader(ps.get());
        d->SetStreamSource(0, vertices.get(), 0, sizeof(QuadVertex));
        d->SetTexture(0, texture.get());
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
        const float opaque[4] = {1, 1, 1, 1};
        const float ghost[4] = {1, 1, 1, 0.5f};
        d->SetPixelShaderConstantF(0, opaque, 1);
        d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
        d->SetPixelShaderConstantF(0, ghost, 1);
        d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4, 2);
        rr::Image color;
        if (!kit.end(error) || !kit.read_color(target, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const D3d9Register<MippedQuadBlend> kMippedQuadBlend{rr::CaseSpec("texture", "mipped_quad_blend").tolerance(rr::Tolerance::filter(4, 0.002))};

constexpr UINT kRingSlots = 4;
constexpr UINT kStampVertices = 12;

class RingAccumulate : public D3d9Scene {
public:
    bool setup(Kit& kit, std::string& error) override {
        if (!mesh_.create(kit, kMeshPs, error) || !kit.make_target(target_, 128, 128, D3DFMT_A8R8G8B8, error)) {
            return false;
        }
        slot_bytes_ = kStampVertices * sizeof(rr::ColoredVertex);
        return check(kit.device()->CreateVertexBuffer(static_cast<UINT>(slot_bytes_ * kRingSlots), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                      D3DPOOL_DEFAULT, ring_.put(), nullptr),
                     "CreateVertexBuffer", error);
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        IDirect3DDevice9* d = kit.device();
        const UINT slot = index % kRingSlots;
        void* data = nullptr;
        if (!check(ring_->Lock(static_cast<UINT>(slot * slot_bytes_), static_cast<UINT>(slot_bytes_), &data,
                               slot == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE),
                   "Lock of the vertex ring", error)) {
            return false;
        }
        rr::Mesh::stamp(static_cast<rr::ColoredVertex*>(data), index);
        ring_->Unlock();
        d->SetRenderTarget(0, target_.surface.get());
        d->SetDepthStencilSurface(nullptr);
        if (index == 0 && !check(d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(0.05f, 0.05f, 0.08f, 1.0f), 1.0f, 0), "Clear", error)) {
            return false;
        }
        if (!kit.begin(error)) {
            return false;
        }
        mesh_.bind(kit, false);
        d->SetStreamSource(0, ring_.get(), static_cast<UINT>(slot * slot_bytes_), sizeof(rr::ColoredVertex));
        const rr::Mat4 transform = rr::Mat4::rotate_z(0.05f * static_cast<float>(index));
        d->SetVertexShaderConstantF(0, transform.m, 4);
        d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, kStampVertices / 3);
        return kit.end(error);
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
    Surface target_;
    Com<IDirect3DVertexBuffer9> ring_;
    std::size_t slot_bytes_ = 0;
};

const D3d9Register<RingAccumulate> kRingAccumulate{
    rr::CaseSpec("frames", "ring_accumulate").frames(8, {0, 3, 7}).variant(1).tolerance(rr::Tolerance::blend(2))};

}  // namespace

}  // namespace lsw::suites::render::d3d9
