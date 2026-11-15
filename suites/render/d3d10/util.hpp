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

#define PS_HEAD "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"

inline std::vector<Vtx> full_uv(float s = 1.0f, float ox = 0.0f, float oy = 0.0f) { return Geo::quad_uv(-1, -1, 1, 1, 0.5f, ox, oy, ox + s, oy + s); }

inline void paint(Rig& r, rr::CaptureSet& o, const std::string& ps_source, const std::vector<ID3D10ShaderResourceView*>& views,
                  const std::vector<ID3D10SamplerState*>& samplers, const std::vector<Vtx>& quad) {
    Com<ID3D10PixelShader> ps = r.ps(ps_source);
    if (!r.ok()) return;
    r.d->PSSetShader(ps.get());
    for (std::size_t i = 0; i < views.size(); ++i) r.ps_srv(static_cast<UINT>(i), views[i]);
    for (std::size_t i = 0; i < samplers.size(); ++i) r.ps_smp(static_cast<UINT>(i), samplers[i]);
    r.draw_std(quad);
    r.cap_main(o);
}

inline rr::Pattern::Bytes tile_pattern(unsigned w, unsigned h, unsigned seed) {
    rr::Pattern::Bytes bytes = rr::Pattern::checker(w, h, std::max(1u, w / 8), rr::Pattern::pack(240, 230, 60 + seed * 20), rr::Pattern::pack(30 + seed * 25, 90, 200));
    const rr::Pattern::Bytes grad = rr::Pattern::gradient(w, h);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        bytes[i] = static_cast<std::uint8_t>((bytes[i] * 3 + grad[i]) / 4);
        bytes[i + 1] = static_cast<std::uint8_t>((bytes[i + 1] * 3 + grad[i + 1]) / 4);
    }
    return bytes;
}

struct V3 {
    float x, y, z;
};
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 norm3(V3 a) {
    const float l = std::sqrt(dot3(a, a));
    return {a.x / l, a.y / l, a.z / l};
}

inline rr::Mat4 look_at(V3 eye, V3 target, V3 up) {
    const V3 f = norm3(sub(target, eye));
    const V3 s = norm3(cross3(f, up));
    const V3 u = cross3(s, f);
    rr::Mat4 m;
    m.m[0] = s.x; m.m[4] = s.y; m.m[8] = s.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[12] = -dot3(s, eye);
    m.m[13] = -dot3(u, eye);
    m.m[14] = dot3(f, eye);
    return m;
}

inline rr::Mat4 ortho(float width, float height, float near_plane, float far_plane) {
    rr::Mat4 m;
    m.m[0] = 2.0f / width;
    m.m[5] = 2.0f / height;
    m.m[10] = -1.0f / (far_plane - near_plane);
    m.m[14] = -near_plane / (far_plane - near_plane);
    return m;
}

struct WorldObject {
    const MeshBuffers* mesh;
    rr::Mat4 model;
    float color[4];
};

struct World {
    explicit World(Rig& r)
        : floor(upload(r, Geo::box(5.0f, 0.1f, 5.0f))), sphere(upload(r, Geo::sphere(16, 24, 0.9f))), torus(upload(r, Geo::torus(24, 12, 0.8f, 0.3f))),
          box(upload(r, Geo::box(0.6f, 0.9f, 0.6f))) {}

    [[nodiscard]] std::vector<WorldObject> objects(float t) const {
        return {{&floor, rr::Mat4::translate(0, -1.0f, 0), {0.65f, 0.65f, 0.7f, 1}},
                {&sphere, rr::Mat4::translate(-1.4f + 0.3f * std::sin(t), -0.1f, 0.3f), {0.9f, 0.35f, 0.25f, 1}},
                {&torus, rr::Mat4::translate(1.2f, 0.1f + 0.2f * std::sin(t * 1.3f), -0.4f) * rr::Mat4::rotate_x(1.1f + 0.2f * t) * rr::Mat4::rotate_y(t), {0.3f, 0.7f, 0.4f, 1}},
                {&box, rr::Mat4::translate(0.1f, -0.1f, 1.4f) * rr::Mat4::rotate_y(0.5f + 0.5f * t), {0.3f, 0.4f, 0.9f, 1}}};
    }

    MeshBuffers floor, sphere, torus, box;
};

struct ObjectConstants {
    float vp[16];
    float model[16];
    float color[4];
    float extra[4];
};

struct LightConstants {
    float lvp[16];
    float dir[4];
    float pos[4];
};

inline rr::Tolerance lines_tol() {
    rr::Tolerance t = rr::Tolerance::raster(48, 2);
    t.outlier_fraction = 1e-3;
    t.max_outliers = 8;
    t.name = "lines(48,2)";
    return t;
}

inline rr::Tolerance scene_tol(int delta, double fraction = 3e-4, std::uint64_t count = 24) {
    rr::Tolerance t = rr::Tolerance::raster(delta, 1);
    t.outlier_fraction = fraction;
    t.max_outliers = count;
    t.name = "scene(" + std::to_string(delta) + ")";
    return t;
}

inline const char* kIdHash =
    "float3 idcol(uint id) { uint h = id * 2654435761u + 12345u; h ^= h >> 15; h *= 2246822519u; return float3((h >> 8) & 255u, (h >> 16) & 255u, (h >> 24) & 255u) / 255.0; }\n";
}  // namespace lsw::suites::render::d3d10
