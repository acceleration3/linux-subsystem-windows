#pragma once

#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

struct LitVertex {
    float p[3];
    float n[3];
    float uv[2];
};

struct LitMesh {
    std::vector<LitVertex> vertices;
    std::vector<std::uint32_t> indices;

    static LitMesh cube() {
        LitMesh m;
        static const float normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int f = 0; f < 6; ++f) {
            const float* n = normals[f];
            float u[3], v[3];
            const int a = f / 2;
            const int b = (a + 1) % 3;
            const int c = (a + 2) % 3;
            for (int k = 0; k < 3; ++k) u[k] = v[k] = 0;
            u[b] = 1;
            v[c] = 1;
            const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
            for (int q = 0; q < 4; ++q) {
                const float su = (q & 1) ? 1.0f : -1.0f;
                const float sv = (q & 2) ? 1.0f : -1.0f;
                LitVertex lv{};
                for (int k = 0; k < 3; ++k) {
                    lv.p[k] = n[k] + u[k] * su + v[k] * sv;
                    lv.n[k] = n[k];
                }
                lv.uv[0] = (su + 1) * 0.5f;
                lv.uv[1] = (sv + 1) * 0.5f;
                m.vertices.push_back(lv);
            }
            const bool flip = (f % 2 == 0) != (a == 1);
            const std::uint32_t order[6] = {0, 1, 2, 1, 3, 2};
            const std::uint32_t order_flip[6] = {0, 2, 1, 1, 2, 3};
            for (int k = 0; k < 6; ++k) m.indices.push_back(base + (flip ? order_flip[k] : order[k]));
        }
        return m;
    }

    static LitMesh sphere(unsigned segments, unsigned rings) {
        LitMesh m;
        for (unsigned y = 0; y <= rings; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(rings);
            const float phi = v * 3.14159265f;
            for (unsigned x = 0; x <= segments; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(segments);
                const float theta = u * 6.2831853f;
                LitVertex lv{};
                lv.n[0] = std::sin(phi) * std::cos(theta);
                lv.n[1] = std::cos(phi);
                lv.n[2] = std::sin(phi) * std::sin(theta);
                for (int k = 0; k < 3; ++k) lv.p[k] = lv.n[k];
                lv.uv[0] = u;
                lv.uv[1] = v;
                m.vertices.push_back(lv);
            }
        }
        for (unsigned y = 0; y < rings; ++y)
            for (unsigned x = 0; x < segments; ++x) {
                const std::uint32_t a = y * (segments + 1) + x;
                const std::uint32_t b = a + segments + 1;
                for (std::uint32_t i : {a, a + 1, b, a + 1, b + 1, b}) m.indices.push_back(i);
            }
        return m;
    }

    static LitMesh plane(float extent, float y = 0.0f) {
        LitMesh m;
        for (int q = 0; q < 4; ++q) {
            LitVertex lv{};
            lv.p[0] = (q & 1) ? extent : -extent;
            lv.p[1] = y;
            lv.p[2] = (q & 2) ? extent : -extent;
            lv.n[1] = 1;
            lv.uv[0] = (q & 1) ? 1.0f : 0.0f;
            lv.uv[1] = (q & 2) ? 1.0f : 0.0f;
            m.vertices.push_back(lv);
        }
        m.indices = {0, 2, 1, 1, 2, 3};
        return m;
    }
};

struct GpuMesh {
    Com<ID3D11Buffer> vb, ib;
    UINT count = 0;
    bool make(Rig& r, const LitMesh& m) {
        vb = r.vbuf(m.vertices);
        ib = r.ibuf(m.indices);
        count = static_cast<UINT>(m.indices.size());
        return vb && ib;
    }
    void bind(Rig& r) const {
        const UINT stride = sizeof(LitVertex), offset = 0;
        ID3D11Buffer* b = vb.get();
        r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
        r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R32_UINT, 0);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    void draw(Rig& r, UINT instances = 1) const { r.ctx->DrawIndexedInstanced(count, instances, 0, 0, 0); }
};

inline std::vector<D3D11_INPUT_ELEMENT_DESC> lit_layout() {
    return {elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT, 12), elem("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, 24)};
}

struct ObjectConstants {
    rr::Mat4 view_proj;
    rr::Mat4 model;
    float color[4];
    float extra[4];
};

inline rr::Mat4 trs(float x, float y, float z, float sx, float sy, float sz, float ry = 0.0f, float rx = 0.0f) {
    return rr::Mat4::translate(x, y, z) * rr::Mat4::rotate_y(ry) * rr::Mat4::rotate_x(rx) * rr::Mat4::scale(sx, sy, sz);
}

struct Hlsl3 {
    static constexpr const char* kLitVs =
        "cbuffer Obj : register(b0) { column_major float4x4 vp; column_major float4x4 model; float4 color; float4 extra; };\n"
        "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct VO { float4 pos : SV_Position; float3 w : WORLD; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
        "VO main(VI i) { VO o; float4 w = mul(model, float4(i.p, 1)); o.w = w.xyz; o.n = normalize(mul((float3x3)model, i.n)); o.pos = mul(vp, w); o.uv = i.uv; return o; }\n";
    static constexpr const char* kLitIn =
        "cbuffer Obj : register(b0) { column_major float4x4 vp; column_major float4x4 model; float4 color; float4 extra; };\n"
        "struct VO { float4 pos : SV_Position; float3 w : WORLD; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n";
};

struct LitPipe {
    Com<ID3D11VertexShader> vs;
    Com<ID3D11InputLayout> il;
    Com<ID3D11Buffer> cb;
    bool make(Rig& r) {
        Bytes code;
        vs = r.vs(Hlsl3::kLitVs, &code);
        if (!vs) return false;
        il = r.layout(lit_layout(), code);
        cb = r.buf(sizeof(ObjectConstants), D3D11_BIND_CONSTANT_BUFFER);
        return il && cb;
    }
    void bind(Rig& r) {
        ID3D11Buffer* b = cb.get();
        r.ctx->IASetInputLayout(il.get());
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->VSSetConstantBuffers(0, 1, &b);
        r.ctx->PSSetConstantBuffers(0, 1, &b);
    }
    void set(Rig& r, const rr::Mat4& vp, const rr::Mat4& model, float cr, float cg, float cb_, float e0 = 0, float e1 = 0, float e2 = 0, float e3 = 0) {
        ObjectConstants c{vp, model, {cr, cg, cb_, 1}, {e0, e1, e2, e3}};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, &c, 0, 0);
    }
};

struct Light {
    static rr::Mat4 ortho(float half, float zn, float zf) {
        rr::Mat4 m;
        for (float& v : m.m) v = 0;
        m.m[0] = 1.0f / half;
        m.m[5] = 1.0f / half;
        m.m[10] = 1.0f / (zn - zf);
        m.m[14] = zn / (zn - zf);
        m.m[15] = 1.0f;
        return m;
    }
};

}  // namespace lsw::suites::render::d3d11
