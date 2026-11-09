#pragma once

#include "render/d3d10/rig.hpp"

#include <cmath>
#include <map>
#include <tuple>

namespace lsw::suites::render::d3d10 {
inline const float* palette(unsigned i) {
    static const float colors[8][4] = {{1, 0.2f, 0.2f, 1}, {0.2f, 1, 0.3f, 1}, {0.3f, 0.4f, 1, 1}, {1, 0.9f, 0.2f, 1},
                                       {1, 0.3f, 0.9f, 1}, {0.2f, 0.9f, 1, 1}, {1, 0.6f, 0.2f, 1}, {0.7f, 0.7f, 0.75f, 1}};
    return colors[i & 7];
}

inline Vtx vtx(float x, float y, float z, const float* c, float u = 0, float v = 0) { return {x, y, z, c[0], c[1], c[2], c[3], u, v}; }

inline std::vector<Vtx> ring_points(unsigned count, float radius, float z = 0.5f) {
    std::vector<Vtx> out;
    for (unsigned i = 0; i < count; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        const float r = radius * (0.35f + 0.65f * static_cast<float>(i % 7) / 6.0f);
        out.push_back(vtx(r * std::cos(a), r * std::sin(a), z, palette(i)));
    }
    return out;
}

inline const char* kSceneVs =
    "cbuffer C : register(b0) { float4x4 m; float4 tint; };\n"
    "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n"
    "VO main(VI i) { VO o; o.p = mul(m, float4(i.p, 1)); o.n = i.n; o.uv = i.uv; o.w = i.p; return o; }\n";

inline std::vector<D3D10_INPUT_ELEMENT_DESC> mesh_elements() {
    return {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0}};
}

struct MeshBuffers {
    Com<ID3D10Buffer> vertices;
    Com<ID3D10Buffer> indices;
    UINT count = 0;
};

inline MeshBuffers upload(Rig& r, const MeshData& mesh) {
    MeshBuffers out;
    out.vertices = r.vbuf(mesh.vertices.data(), static_cast<UINT>(mesh.vertices.size() * sizeof(MeshVertex)));
    out.indices = r.ibuf32(mesh.indices);
    out.count = static_cast<UINT>(mesh.indices.size());
    return out;
}

inline void draw_mesh(Rig& r, const MeshBuffers& m, UINT instances = 1) {
    const UINT stride = sizeof(MeshVertex);
    const UINT offset = 0;
    ID3D10Buffer* vb = m.vertices.get();
    r.d->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    r.d->IASetIndexBuffer(m.indices.get(), DXGI_FORMAT_R32_UINT, 0);
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (instances > 1) {
        r.d->DrawIndexedInstanced(m.count, instances, 0, 0, 0);
    } else {
        r.d->DrawIndexed(m.count, 0, 0);
    }
}

inline std::vector<std::uint32_t> adjacency(const MeshData& mesh) {
    auto key = [&](std::uint32_t i) {
        const MeshVertex& v = mesh.vertices[i];
        return std::make_tuple(std::lround(v.p[0] * 2000.0f), std::lround(v.p[1] * 2000.0f), std::lround(v.p[2] * 2000.0f));
    };
    using Key = std::tuple<long, long, long>;
    std::map<std::pair<Key, Key>, std::vector<std::pair<std::uint32_t, std::uint32_t>>> edges;
    const std::size_t tris = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < tris; ++t) {
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t a = mesh.indices[t * 3 + e];
            const std::uint32_t b = mesh.indices[t * 3 + (e + 1) % 3];
            const std::uint32_t c = mesh.indices[t * 3 + (e + 2) % 3];
            Key ka = key(a), kb = key(b);
            edges[{std::min(ka, kb), std::max(ka, kb)}].push_back({static_cast<std::uint32_t>(t), c});
        }
    }
    std::vector<std::uint32_t> out;
    for (std::size_t t = 0; t < tris; ++t) {
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t a = mesh.indices[t * 3 + e];
            const std::uint32_t b = mesh.indices[t * 3 + (e + 1) % 3];
            Key ka = key(a), kb = key(b);
            std::uint32_t adj = mesh.indices[t * 3 + (e + 2) % 3];
            for (const auto& entry : edges[{std::min(ka, kb), std::max(ka, kb)}]) {
                if (entry.first != t) {
                    adj = entry.second;
                }
            }
            out.push_back(a);
            out.push_back(adj);
        }
    }
    return out;
}

inline const char* kScenePs =
    "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target {\n"
    "  float l = saturate(dot(normalize(n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; return float4(l * (0.55 + 0.45 * sin(w * 3.0 + 1.0)), 1); }\n";

struct SceneConstants {
    float m[16];
    float tint[4];
};

class ScenePipe {
public:
    explicit ScenePipe(Rig& r, const char* ps_source = nullptr) {
        vs_ = r.vs(kSceneVs);
        layout_ = r.layout(mesh_elements());
        ps_ = r.ps(ps_source != nullptr ? ps_source : kScenePs);
        cb_ = r.cbuf(sizeof(SceneConstants));
    }

    void bind(Rig& r) {
        r.d->IASetInputLayout(layout_.get());
        r.d->VSSetShader(vs_.get());
        r.d->PSSetShader(ps_.get());
        r.vs_cb(0, cb_.get());
        r.ps_cb(0, cb_.get());
    }

    void draw(Rig& r, const MeshBuffers& mesh, const rr::Mat4& transform, float tint0 = 1, float tint1 = 1, float tint2 = 1, float tint3 = 1,
              UINT instances = 1) {
        SceneConstants c{};
        std::memcpy(c.m, transform.m, 64);
        c.tint[0] = tint0;
        c.tint[1] = tint1;
        c.tint[2] = tint2;
        c.tint[3] = tint3;
        r.d->UpdateSubresource(cb_.get(), 0, nullptr, &c, 0, 0);
        draw_mesh(r, mesh, instances);
    }

    Com<ID3D10VertexShader> vs_;
    Com<ID3D10PixelShader> ps_;
    Com<ID3D10InputLayout> layout_;
    Com<ID3D10Buffer> cb_;
};

inline rr::Mat4 camera(float yaw, float pitch, float distance, float aspect = 1.0f, float fov = 1.0f) {
    return rr::Mat4::perspective_d3d(fov, aspect, 0.5f, 60.0f) * rr::Mat4::translate(0, 0, -distance) * rr::Mat4::rotate_x(pitch) * rr::Mat4::rotate_y(yaw);
}
}  // namespace lsw::suites::render::d3d10
