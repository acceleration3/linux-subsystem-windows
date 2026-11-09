#pragma once

#include "render/d3d11/rig.hpp"

#include <cmath>

namespace lsw::suites::render::d3d11 {

struct Vtx {
    float x, y, r, g, b;
};

struct Hlsl2d {
    static constexpr const char* kVs =
        "struct VI { float2 p : POSITION; float3 c : COLOR; };\n"
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "VO main(VI i) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }\n";
    static constexpr const char* kPs =
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "float4 main(VO i) : SV_Target { return float4(i.c, 1); }\n";
    static constexpr const char* kVo = "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n";
};

inline void hue(float t, float& r, float& g, float& b) {
    t = t - std::floor(t);
    r = std::fmin(std::fmax(std::fabs(t * 6.0f - 3.0f) - 1.0f, 0.0f), 1.0f);
    g = std::fmin(std::fmax(2.0f - std::fabs(t * 6.0f - 2.0f), 0.0f), 1.0f);
    b = std::fmin(std::fmax(2.0f - std::fabs(t * 6.0f - 4.0f), 0.0f), 1.0f);
}

inline Vtx vtx(float x, float y, float t) {
    Vtx v{x, y, 0, 0, 0};
    hue(t, v.r, v.g, v.b);
    return v;
}

inline std::vector<D3D11_INPUT_ELEMENT_DESC> layout2d() {
    return {elem("POSITION", DXGI_FORMAT_R32G32_FLOAT, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 8)};
}

class Basic {
public:
    bool make(Rig& r, const char* ps_source = nullptr, const char* vs_source = nullptr, std::vector<D3D11_INPUT_ELEMENT_DESC> elements = {}) {
        Bytes code;
        vs_ = r.vs(vs_source != nullptr ? vs_source : Hlsl2d::kVs, &code);
        ps_ = r.ps(ps_source != nullptr ? ps_source : Hlsl2d::kPs);
        if (!vs_ || !ps_) {
            return false;
        }
        il_ = r.layout(elements.empty() ? layout2d() : elements, code);
        return static_cast<bool>(il_);
    }

    void bind(Rig& r, D3D11_PRIMITIVE_TOPOLOGY topology) {
        r.ctx->IASetInputLayout(il_.get());
        r.ctx->IASetPrimitiveTopology(topology);
        r.ctx->VSSetShader(vs_.get(), nullptr, 0);
        r.ctx->PSSetShader(ps_.get(), nullptr, 0);
    }

    void vertices(Rig& r, ID3D11Buffer* buffer, UINT stride = sizeof(Vtx), UINT offset = 0, UINT slot = 0) {
        r.ctx->IASetVertexBuffers(slot, 1, &buffer, &stride, &offset);
    }

    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11InputLayout> il_;
};

inline std::vector<Vtx> ring_fan(unsigned n, float radius, float cx = 0, float cy = 0, float phase = 0) {
    std::vector<Vtx> out;
    for (unsigned i = 0; i < n; ++i) {
        const float a0 = phase + 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        const float a1 = phase + 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(n);
        out.push_back(vtx(cx, cy, 0.0f));
        out.push_back(vtx(cx + radius * std::cos(a0), cy + radius * std::sin(a0), static_cast<float>(i) / static_cast<float>(n)));
        out.push_back(vtx(cx + radius * std::cos(a1), cy + radius * std::sin(a1), static_cast<float>(i + 1) / static_cast<float>(n)));
    }
    return out;
}

inline std::vector<Vtx> grid_mesh(unsigned n, float extent, std::vector<std::uint32_t>& indices) {
    std::vector<Vtx> out;
    for (unsigned y = 0; y <= n; ++y) {
        for (unsigned x = 0; x <= n; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(n);
            const float fy = static_cast<float>(y) / static_cast<float>(n);
            const float wob = 0.04f * std::sin(fx * 9.0f) * std::cos(fy * 7.0f);
            out.push_back(vtx((fx * 2 - 1) * extent + wob, (fy * 2 - 1) * extent - wob, fx * 0.7f + fy * 0.3f));
        }
    }
    for (unsigned y = 0; y < n; ++y) {
        for (unsigned x = 0; x < n; ++x) {
            const std::uint32_t a = y * (n + 1) + x;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + n + 1;
            const std::uint32_t d = c + 1;
            for (std::uint32_t v : {a, c, b, b, c, d}) {
                indices.push_back(v);
            }
        }
    }
    return out;
}

}  // namespace lsw::suites::render::d3d11

namespace lsw::suites::render::d3d11 {
bool run_topology_public(Rig& r, Cap& out, const std::vector<Vtx>& verts, D3D11_PRIMITIVE_TOPOLOGY topology, const char* gs_source, UINT count = 0);
bool run_patch_public(Rig& r, Cap& out, unsigned n, bool quad, const char* partition, const char* topology, float outer, float inner, bool wire);
}

namespace lsw::suites::render::d3d11 {

struct Hlsl3d {
    static constexpr const char* kVs =
        "cbuffer Frame : register(b0) { column_major float4x4 transform; };\n"
        "struct VsIn { float3 pos : POSITION; float3 col : COLOR; };\n"
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "VsOut main(VsIn i) { VsOut o; o.pos = mul(transform, float4(i.pos, 1.0)); o.col = i.col; return o; }\n";
    static constexpr const char* kPs =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i) : SV_Target { return float4(i.col, 1.0); }\n";
    static constexpr const char* kFsVsZ =
        "cbuffer Z : register(b0) { float4 zc; };\n"
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VO main(uint id : SV_VertexID) { VO o; float2 t = float2((id << 1) & 2, id & 2);\n"
        "  o.p = float4(t * float2(2, -2) + float2(-1, 1), zc.x, 1); o.uv = t; return o; }\n";
};

class CubePipe {
public:
    bool make(Rig& r, const char* ps_source = nullptr, D3D11_CULL_MODE cull = D3D11_CULL_BACK) {
        Bytes code;
        vs_ = r.vs(Hlsl3d::kVs, &code);
        ps_ = r.ps(ps_source != nullptr ? ps_source : Hlsl3d::kPs);
        if (!vs_ || !ps_) return false;
        il_ = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 12)}, code);
        const std::vector<rr::ColoredVertex> vertices = rr::Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = rr::Mesh::cube_indices();
        count_ = static_cast<UINT>(indices.size());
        vb_ = r.vbuf(vertices);
        ib_ = r.ibuf(indices);
        cb_ = r.buf(64, D3D11_BIND_CONSTANT_BUFFER);
        D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, cull);
        rd.FrontCounterClockwise = TRUE;
        rs_ = r.rast(rd);
        return il_ && vb_ && ib_ && cb_ && rs_;
    }

    void bind(Rig& r) {
        const UINT stride = sizeof(rr::ColoredVertex);
        const UINT offset = 0;
        ID3D11Buffer* vb = vb_.get();
        ID3D11Buffer* cb = cb_.get();
        r.ctx->IASetInputLayout(il_.get());
        r.ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        r.ctx->IASetIndexBuffer(ib_.get(), DXGI_FORMAT_R16_UINT, 0);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.ctx->VSSetShader(vs_.get(), nullptr, 0);
        r.ctx->VSSetConstantBuffers(0, 1, &cb);
        r.ctx->PSSetShader(ps_.get(), nullptr, 0);
        r.ctx->RSSetState(rs_.get());
    }

    void draw(Rig& r, const rr::Mat4& m) {
        r.ctx->UpdateSubresource(cb_.get(), 0, nullptr, m.m, 0, 0);
        r.ctx->DrawIndexed(count_, 0, 0);
    }

    static rr::Mat4 camera() { return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -6.0f); }

    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11InputLayout> il_;
    Com<ID3D11Buffer> vb_, ib_, cb_;
    Com<ID3D11RasterizerState> rs_;
    UINT count_ = 0;
};

}  // namespace lsw::suites::render::d3d11
