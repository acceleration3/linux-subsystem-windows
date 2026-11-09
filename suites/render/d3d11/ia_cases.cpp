#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

struct FormatRow {
    const char* name;
    DXGI_FORMAT format;
    int used;
    char kind;
    double scale;
};

const FormatRow kFormats[] = {
    {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 'd', 1},
    {"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT, 4, 'u', 100000},
    {"r32g32b32a32_sint", DXGI_FORMAT_R32G32B32A32_SINT, 4, 'i', 1000000},
    {"r32g32b32_float", DXGI_FORMAT_R32G32B32_FLOAT, 3, 'd', 1},
    {"r32g32b32_uint", DXGI_FORMAT_R32G32B32_UINT, 3, 'u', 100000},
    {"r32g32b32_sint", DXGI_FORMAT_R32G32B32_SINT, 3, 'i', 1000000},
    {"r32g32_float", DXGI_FORMAT_R32G32_FLOAT, 2, 'd', 1},
    {"r32g32_uint", DXGI_FORMAT_R32G32_UINT, 2, 'u', 100000},
    {"r32g32_sint", DXGI_FORMAT_R32G32_SINT, 2, 'i', 1000000},
    {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, 4, 'd', 1},
    {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, 4, 'n', 1},
    {"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM, 4, 'd', 1},
    {"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT, 4, 'u', 4000},
    {"r16g16b16a16_sint", DXGI_FORMAT_R16G16B16A16_SINT, 4, 'i', 10000},
    {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, 2, 'd', 1},
    {"r16g16_unorm", DXGI_FORMAT_R16G16_UNORM, 2, 'n', 1},
    {"r16g16_snorm", DXGI_FORMAT_R16G16_SNORM, 2, 'd', 1},
    {"r16g16_uint", DXGI_FORMAT_R16G16_UINT, 2, 'u', 4000},
    {"r16g16_sint", DXGI_FORMAT_R16G16_SINT, 2, 'i', 10000},
    {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, 3, 'n', 1},
    {"r10g10b10a2_uint", DXGI_FORMAT_R10G10B10A2_UINT, 3, 'u', 1000},
    {"r11g11b10_float", DXGI_FORMAT_R11G11B10_FLOAT, 3, 'n', 1},
    {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, 4, 'n', 1},
    {"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM, 4, 'd', 1},
    {"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT, 4, 'u', 250},
    {"r8g8b8a8_sint", DXGI_FORMAT_R8G8B8A8_SINT, 4, 'i', 100},
    {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, 4, 'n', 1},
    {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, 2, 'n', 1},
    {"r8g8_snorm", DXGI_FORMAT_R8G8_SNORM, 2, 'd', 1},
    {"r8g8_uint", DXGI_FORMAT_R8G8_UINT, 2, 'u', 250},
    {"r8g8_sint", DXGI_FORMAT_R8G8_SINT, 2, 'i', 100},
    {"r32_float", DXGI_FORMAT_R32_FLOAT, 1, 'd', 1},
    {"r32_uint", DXGI_FORMAT_R32_UINT, 1, 'u', 100000},
    {"r32_sint", DXGI_FORMAT_R32_SINT, 1, 'i', 1000000},
    {"r16_float", DXGI_FORMAT_R16_FLOAT, 1, 'd', 1},
    {"r16_unorm", DXGI_FORMAT_R16_UNORM, 1, 'n', 1},
    {"r16_snorm", DXGI_FORMAT_R16_SNORM, 1, 'd', 1},
    {"r16_uint", DXGI_FORMAT_R16_UINT, 1, 'u', 4000},
    {"r16_sint", DXGI_FORMAT_R16_SINT, 1, 'i', 10000},
    {"r8_unorm", DXGI_FORMAT_R8_UNORM, 1, 'n', 1},
    {"r8_snorm", DXGI_FORMAT_R8_SNORM, 1, 'd', 1},
    {"r8_uint", DXGI_FORMAT_R8_UINT, 1, 'u', 250},
    {"r8_sint", DXGI_FORMAT_R8_SINT, 1, 'i', 100},
};

struct FormatMf {
    const FormatRow* row;
    FormatSpec spec;
    std::vector<Vtx> shape;
    Target target;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11InputLayout> il;
    Com<ID3D11Buffer> vb;

    explicit FormatMf(const FormatRow* r) : row(r), spec(FormatCodec::spec(r->format)), shape(ring_fan(6, 0.8f, 0.0f, 0.0f, 0.2f)) {}
    std::string needs(Rig& r) { return need_format(r, row->format, D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER, "vertex buffers"); }
    bool init(Rig& r) {
        std::string ys = "static const float ys[" + std::to_string(shape.size()) + "] = {";
        for (std::size_t i = 0; i < shape.size(); ++i) ys += std::to_string(shape[i].y) + (i + 1 < shape.size() ? "," : "};\n");
        const char* type = row->kind == 'u' ? "uint4" : row->kind == 'i' ? "int4" : "float4";
        std::string decode;
        switch (row->kind) {
            case 'n': decode = "a * 2.0 - 1.0"; break;
            case 'u': decode = "float4(a) * (2.0 / " + std::to_string(row->scale) + ") - 1.0"; break;
            case 'i': decode = "float4(a) / " + std::to_string(row->scale); break;
            default: decode = "float4(a)"; break;
        }
        std::string src = ys + "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n" + "VO main(" + type + " a : ATTR, uint id : SV_VertexID) { VO o; float4 d = " + decode + ";\n";
        src += row->used == 1 ? "  float2 q = float2(d.x, ys[id]);\n" : "  float2 q = d.xy;\n";
        src += "  float z = " + std::string(row->used >= 3 ? "d.z" : "0.0") + "; float w = " + (row->used >= 4 ? "d.w" : "0.0") + ";\n";
        src += "  o.p = float4(q, 0, 1); o.c = float3(0.5 + 0.5 * sin(id * 1.7), 0.5 + 0.5 * cos(id * 0.9), 0.4) * (0.6 + 0.4 * abs(z)) + 0.2 * abs(w); return o; }\n";
        Bytes code;
        vs = r.vs(src, &code);
        ps = r.ps(Hlsl2d::kPs);
        if (!vs || !ps || !r.color(target)) return false;
        il = r.layout({elem("ATTR", row->format, 0)}, code);
        vb = r.buf(static_cast<UINT>(shape.size() * static_cast<std::size_t>(spec.bytes)), D3D11_BIND_VERTEX_BUFFER);
        return il && vb;
    }
    bool step(Rig& r, unsigned frame) {
        Bytes data(shape.size() * static_cast<std::size_t>(spec.bytes));
        const double angle = 0.45 * frame;
        for (std::size_t i = 0; i < shape.size(); ++i) {
            double x = shape[i].x, y = shape[i].y;
            if (row->used >= 2) {
                const double rx = x * std::cos(angle) - y * std::sin(angle);
                y = x * std::sin(angle) + y * std::cos(angle);
                x = rx;
            } else {
                x *= 1.0 - 0.14 * frame;
            }
            const double z = 0.6 * std::sin(static_cast<double>(i) + frame);
            const double w = 0.5 * std::cos(static_cast<double>(i) - frame);
            const double logical[4] = {x, y, z, w};
            double native[4];
            for (int k = 0; k < 4; ++k) {
                switch (row->kind) {
                    case 'n': native[k] = (logical[k] + 1.0) * 0.5; break;
                    case 'u': native[k] = std::nearbyint((logical[k] + 1.0) * 0.5 * row->scale); break;
                    case 'i': native[k] = std::nearbyint(logical[k] * row->scale); break;
                    default: native[k] = logical[k]; break;
                }
            }
            FormatCodec::encode(spec, native, data.data() + i * static_cast<std::size_t>(spec.bytes));
        }
        r.ctx->UpdateSubresource(vb.get(), 0, nullptr, data.data(), 0, 0);
        r.bind(target);
        const UINT stride = static_cast<UINT>(spec.bytes);
        const UINT offset = 0;
        ID3D11Buffer* buffer = vb.get();
        r.ctx->IASetInputLayout(il.get());
        r.ctx->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        r.ctx->Draw(static_cast<UINT>(shape.size()), 0);
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};

struct FormatRegistrar {
    FormatRegistrar() {
        for (const FormatRow& row : kFormats) {
            const FormatRow* p = &row;
            const bool animated = row.format == DXGI_FORMAT_R32G32B32A32_FLOAT || row.format == DXGI_FORMAT_R8G8B8A8_UNORM;
            AddState<FormatMf>(spec("ia", (std::string("format_") + row.name).c_str()).frames(animated ? 4 : 1, animated ? std::vector<unsigned>{0, 3} : std::vector<unsigned>{0}).tolerance(rr::Tolerance::raster(3, 1)),
                               [p] { return FormatMf(p); });
        }
    }
};
const FormatRegistrar kFormatRegistrar;

const char* const kInstVs =
    "cbuffer C : register(b0) { float4 off[8]; };\n"
    "struct VI { float2 p : POSITION; float3 c : COLOR; };\n"
    "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
    "VO main(VI i, uint inst : SV_InstanceID) { VO o; o.p = float4(i.p * 0.34 + off[inst].xy, 0, 1);\n"
    "  o.c = lerp(i.c, float3(off[inst].zzz), 0.35); return o; }\n";

struct Instances {
    float off[8][4];
    Instances() {
        for (int k = 0; k < 8; ++k) {
            off[k][0] = -0.55f + 0.37f * static_cast<float>(k % 4);
            off[k][1] = k < 4 ? 0.5f : -0.5f;
            off[k][2] = 0.1f + 0.12f * static_cast<float>(k);
            off[k][3] = 0;
        }
    }
};

struct DrawScene {
    Target target;
    Basic basic;
    Com<ID3D11Buffer> vb, ib16, ib32, cb, flat;
    UINT index_count = 0, flat_count = 0;
    std::vector<Vtx> mesh;
    std::vector<std::uint32_t> idx;

    bool make(Rig& r) {
        mesh = grid_mesh(4, 1.0f, idx);
        std::vector<std::uint16_t> idx16(idx.begin(), idx.end());
        std::vector<Vtx> flat_vertices;
        for (std::uint32_t i : idx) {
            flat_vertices.push_back(mesh[i]);
        }
        index_count = static_cast<UINT>(idx.size());
        flat_count = static_cast<UINT>(flat_vertices.size());
        const Instances instances;
        vb = r.vbuf(mesh);
        ib16 = r.ibuf(idx16);
        ib32 = r.ibuf(idx);
        flat = r.vbuf(flat_vertices);
        cb = r.cb(instances);
        return r.color(target) && basic.make(r, nullptr, kInstVs) && vb && ib16 && ib32 && flat && cb;
    }

    void prepare(Rig& r, bool indexed, bool wide = false) {
        r.bind(target);
        basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        basic.vertices(r, indexed ? vb.get() : flat.get());
        ID3D11Buffer* c = cb.get();
        r.ctx->VSSetConstantBuffers(0, 1, &c);
        r.ctx->IASetIndexBuffer(indexed ? (wide ? ib32.get() : ib16.get()) : nullptr, wide ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
        Com<ID3D11RasterizerState> none = r.rast(rdesc());
        r.ctx->RSSetState(none.get());
    }
};

const rr::Tolerance kTri = rr::Tolerance::raster();

const Add kDraw(spec("ia", "draw").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    s.prepare(r, false);
    r.ctx->Draw(s.flat_count, 0);
    return r.shot(out, s.target);
});
const Add kDrawIndexed(spec("ia", "draw_indexed").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    s.prepare(r, true);
    r.ctx->DrawIndexed(s.index_count, 0, 0);
    return r.shot(out, s.target);
});
const Add kDrawInstanced(spec("ia", "draw_instanced").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    s.prepare(r, false);
    r.ctx->DrawInstanced(s.flat_count, 5, 0, 0);
    return r.shot(out, s.target);
});
const Add kDrawIndexedInstanced(spec("ia", "draw_indexed_instanced").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    s.prepare(r, true);
    r.ctx->DrawIndexedInstanced(s.index_count, 8, 0, 0, 0);
    return r.shot(out, s.target);
});
const Add kDrawInstancedIndirect(spec("ia", "draw_instanced_indirect").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    const UINT args[10] = {9999, 9999, 9999, 9999, 9999, s.flat_count, 6, 0, 0, 0};
    Com<ID3D11Buffer> indirect = r.buf(sizeof args, 0, args, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    if (!indirect) return false;
    s.prepare(r, false);
    r.ctx->DrawInstancedIndirect(indirect.get(), 20);
    return r.shot(out, s.target);
});
const Add kDrawIndexedInstancedIndirect(spec("ia", "draw_indexed_instanced_indirect").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    const UINT args[10] = {9999, 9999, 9999, 9999, 9999, s.index_count, 7, 0, 0, 0};
    Com<ID3D11Buffer> indirect = r.buf(sizeof args, 0, args, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    if (!indirect) return false;
    s.prepare(r, true);
    r.ctx->DrawIndexedInstancedIndirect(indirect.get(), 20);
    return r.shot(out, s.target);
});
const Add kIndex32(spec("ia", "index_32bit").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    s.prepare(r, true, true);
    r.ctx->DrawIndexedInstanced(s.index_count, 3, 0, 0, 0);
    return r.shot(out, s.target);
});

bool base_vertex(Rig& r, Cap& out, bool negative) {
    DrawScene s;
    if (!s.make(r)) return false;
    std::vector<Vtx> padded;
    const UINT pad = negative ? 0 : 41;
    for (UINT i = 0; i < pad; ++i) {
        padded.push_back(vtx(5.0f, 5.0f, 0.5f));
    }
    padded.insert(padded.end(), s.mesh.begin(), s.mesh.end());
    std::vector<std::uint16_t> indices;
    for (std::uint32_t i : s.idx) {
        indices.push_back(static_cast<std::uint16_t>(i + (negative ? 30 : 0)));
    }
    Com<ID3D11Buffer> vb = r.vbuf(padded);
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    if (!vb || !ib) return false;
    s.prepare(r, true);
    s.basic.vertices(r, vb.get());
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
    r.ctx->DrawIndexedInstanced(s.index_count, 6, 0, negative ? -30 : static_cast<INT>(pad), 0);
    return r.shot(out, s.target);
}
const Add kBaseNeg(spec("ia", "base_vertex_negative").tolerance(kTri), [](Rig& r, Cap& out) { return base_vertex(r, out, true); });
const Add kBasePos(spec("ia", "base_vertex_positive").tolerance(kTri), [](Rig& r, Cap& out) { return base_vertex(r, out, false); });

const Add kStartIndex(spec("ia", "start_index_location").tolerance(kTri), [](Rig& r, Cap& out) {
    DrawScene s;
    if (!s.make(r)) return false;
    std::vector<std::uint16_t> indices = {0, 24, 4, 24, 0, 20, 4, 20, 24, 0, 4, 20};
    for (std::uint32_t i : s.idx) indices.push_back(static_cast<std::uint16_t>(i));
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    if (!ib) return false;
    s.prepare(r, true);
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
    r.ctx->DrawIndexedInstanced(s.index_count, 4, 12, 0, 0);
    return r.shot(out, s.target);
});

template <class Index>
bool restart(Rig& r, Cap& out, DXGI_FORMAT format, Index marker) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    std::vector<Vtx> verts;
    std::vector<Index> indices;
    for (int strip = 0; strip < 4; ++strip) {
        const float y = -0.75f + 0.5f * static_cast<float>(strip);
        const Index base = static_cast<Index>(verts.size());
        for (int i = 0; i < 8; ++i) {
            const float t = static_cast<float>(i) / 7.0f;
            verts.push_back(vtx(-0.85f + 1.7f * t, y + (i % 2 == 0 ? 0.22f : -0.02f), t + 0.2f * static_cast<float>(strip)));
            indices.push_back(static_cast<Index>(base + i));
        }
        if (strip < 3) indices.push_back(marker);
    }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    if (!vb || !ib) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    basic.vertices(r, vb.get());
    r.ctx->IASetIndexBuffer(ib.get(), format, 0);
    r.ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    return r.shot(out, target);
}
const Add kRestart16(spec("ia", "primitive_restart_16").tolerance(kTri),
                     [](Rig& r, Cap& out) { return restart<std::uint16_t>(r, out, DXGI_FORMAT_R16_UINT, 0xFFFF); });
const Add kRestart32(spec("ia", "primitive_restart_32").tolerance(kTri),
                     [](Rig& r, Cap& out) { return restart<std::uint32_t>(r, out, DXGI_FORMAT_R32_UINT, 0xFFFFFFFFu); });

const Add kRestartLines(spec("ia", "primitive_restart_line_strip").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    std::vector<Vtx> verts;
    std::vector<std::uint16_t> indices;
    for (int strip = 0; strip < 5; ++strip) {
        for (int i = 0; i < 12; ++i) {
            const float t = static_cast<float>(i) / 11.0f;
            indices.push_back(static_cast<std::uint16_t>(verts.size()));
            verts.push_back(vtx(-0.85f + 1.7f * t, -0.8f + 0.4f * static_cast<float>(strip) + 0.2f * std::sin(t * 9.0f + strip), t));
        }
        indices.push_back(0xFFFF);
    }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    if (!vb || !ib) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    basic.vertices(r, vb.get());
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
    r.ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    return r.shot(out, target);
});

const char* const kStreamVs =
    "struct VI { float2 p : POSITION; float3 c : COLOR; float s : SCALE; float2 o : OFFSET; };\n"
    "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
    "VO main(VI i) { VO o; o.p = float4(i.p * i.s + i.o, 0, 1); o.c = i.c; return o; }\n";

bool streams(Rig& r, Cap& out, unsigned step_rate, unsigned instances, bool offsets) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r, nullptr, kStreamVs,
                                         {elem("POSITION", DXGI_FORMAT_R32G32_FLOAT, offsets ? 4 : 0, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 0, 1),
                                          elem("SCALE", DXGI_FORMAT_R32_FLOAT, 0, 2), elem("OFFSET", DXGI_FORMAT_R32G32_FLOAT, 0, 3, 0, true, step_rate)})) {
        return false;
    }
    const std::vector<Vtx> fan = ring_fan(5, 1.0f);
    std::vector<float> pos;
    std::vector<float> col;
    std::vector<float> scale;
    for (std::size_t i = 0; i < fan.size(); ++i) {
        if (offsets) pos.push_back(7.0f);
        pos.push_back(fan[i].x);
        pos.push_back(fan[i].y);
        col.insert(col.end(), {fan[i].r, fan[i].g, fan[i].b});
        scale.push_back(0.22f + 0.04f * static_cast<float>(i % 3));
    }
    std::vector<float> inst;
    const unsigned distinct = (instances + step_rate - 1) / step_rate;
    for (unsigned i = 0; i < distinct; ++i) {
        inst.push_back(-0.6f + 0.6f * static_cast<float>(i % 3));
        inst.push_back(0.55f - 0.55f * static_cast<float>(i / 3));
    }
    std::vector<float> padded(32, 0.0f);
    padded.insert(padded.end(), pos.begin(), pos.end());
    Com<ID3D11Buffer> b0 = r.buf(static_cast<UINT>((offsets ? padded.size() : pos.size()) * 4), D3D11_BIND_VERTEX_BUFFER, offsets ? padded.data() : pos.data());
    Com<ID3D11Buffer> b1 = r.buf(static_cast<UINT>(col.size() * 4), D3D11_BIND_VERTEX_BUFFER, col.data());
    Com<ID3D11Buffer> b2 = r.buf(static_cast<UINT>(scale.size() * 4), D3D11_BIND_VERTEX_BUFFER, scale.data());
    Com<ID3D11Buffer> b3 = r.buf(static_cast<UINT>(inst.size() * 4), D3D11_BIND_VERTEX_BUFFER, inst.data());
    if (!b0 || !b1 || !b2 || !b3) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* buffers[4] = {b0.get(), b1.get(), b2.get(), b3.get()};
    const UINT strides[4] = {offsets ? 12u : 8u, 12, 4, 8};
    const UINT starts[4] = {offsets ? 128u : 0u, 0, 0, 0};
    r.ctx->IASetVertexBuffers(0, 4, buffers, strides, starts);
    r.ctx->DrawInstanced(static_cast<UINT>(fan.size()), instances, 0, 0);
    return r.shot(out, target);
}
const Add kStreams4(spec("ia", "streams_four_slots").tolerance(kTri), [](Rig& r, Cap& out) { return streams(r, out, 1, 6, false); });
const Add kStepRate3(spec("ia", "instance_step_rate_3").tolerance(kTri), [](Rig& r, Cap& out) { return streams(r, out, 3, 9, false); });
const Add kStepRate2(spec("ia", "instance_step_rate_2").tolerance(kTri), [](Rig& r, Cap& out) { return streams(r, out, 2, 8, false); });
const Add kStreamOffsets(spec("ia", "stream_offsets_and_element_offsets").tolerance(kTri), [](Rig& r, Cap& out) { return streams(r, out, 1, 4, true); });

const Add kStride0(spec("ia", "stream_stride_zero").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r, nullptr, nullptr, {elem("POSITION", DXGI_FORMAT_R32G32_FLOAT, 0, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 8, 1)}))
        return false;
    std::vector<Vtx> fan = ring_fan(9, 0.85f);
    const Vtx tint = vtx(0, 0, 0.62f);
    Com<ID3D11Buffer> b0 = r.vbuf(fan);
    Com<ID3D11Buffer> b1 = r.buf(sizeof(Vtx), D3D11_BIND_VERTEX_BUFFER, &tint);
    if (!b0 || !b1) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* buffers[2] = {b0.get(), b1.get()};
    const UINT strides[2] = {sizeof(Vtx), 0};
    const UINT starts[2] = {0, 0};
    r.ctx->IASetVertexBuffers(0, 2, buffers, strides, starts);
    r.ctx->Draw(static_cast<UINT>(fan.size()), 0);
    return r.shot(out, target);
});

const Add kSixteenSlots(spec("ia", "streams_sixteen_slots").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    std::string src = "struct VI {";
    std::string sum = "float2 q = 0;";
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    for (int i = 0; i < 16; ++i) {
        src += " float2 a" + std::to_string(i) + " : A" + std::to_string(i) + ";";
        sum += " q += i.a" + std::to_string(i) + ";";
        elements.push_back(elem("A", DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(i), static_cast<UINT>(i)));
    }
    src += " };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\nVO main(VI i, uint id : SV_VertexID) { VO o; " + sum +
           " o.p = float4(q, 0, 1); o.c = float3(0.5 + 0.5 * sin(id * 1.3), 0.5 + 0.5 * cos(id * 2.1), 0.8 - 0.05 * id); return o; }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(src, &code);
    Com<ID3D11PixelShader> ps = r.ps(Hlsl2d::kPs);
    if (!r.color(target) || !vs || !ps) return false;
    Com<ID3D11InputLayout> il = r.layout(elements, code);
    if (!il) return false;
    const std::vector<Vtx> fan = ring_fan(6, 0.8f, 0.0f, 0.0f, 0.1f);
    Com<ID3D11Buffer> buffers[16];
    ID3D11Buffer* raw[16];
    UINT strides[16];
    UINT starts[16] = {};
    for (int i = 0; i < 16; ++i) {
        std::vector<float> data;
        for (const Vtx& v : fan) {
            const float f = (i == 0) ? 0.5f : (i == 15 ? 0.25f : 0.25f / 14.0f);
            data.push_back(v.x * f + (i == 7 ? 0.0f : 0.0f));
            data.push_back(v.y * f);
        }
        buffers[i] = r.buf(static_cast<UINT>(data.size() * 4), D3D11_BIND_VERTEX_BUFFER, data.data());
        if (!buffers[i]) return false;
        raw[i] = buffers[i].get();
        strides[i] = 8;
    }
    r.bind(target);
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 16, raw, strides, starts);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->Draw(static_cast<UINT>(fan.size()), 0);
    return r.shot(out, target);
});

const Add kStartInstance(spec("ia", "start_instance_location").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    const char* vs_source =
        "struct VI { float2 p : POSITION; float3 c : COLOR; float2 o : OFFSET; };\n"
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "VO main(VI i, uint inst : SV_InstanceID) { VO o; o.p = float4(i.p * 0.28 + i.o, 0, 1); o.c = i.c * (1.0 - 0.18 * inst); return o; }\n";
    if (!r.color(target) || !basic.make(r, nullptr, vs_source,
                                         {elem("POSITION", DXGI_FORMAT_R32G32_FLOAT, 0, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 8, 0),
                                          elem("OFFSET", DXGI_FORMAT_R32G32_FLOAT, 0, 1, 0, true, 1)}))
        return false;
    const std::vector<Vtx> fan = ring_fan(6, 1.0f);
    float offsets[8][2];
    for (int i = 0; i < 8; ++i) {
        offsets[i][0] = -0.65f + 0.43f * static_cast<float>(i % 4);
        offsets[i][1] = i < 4 ? 0.5f : -0.5f;
    }
    Com<ID3D11Buffer> b0 = r.vbuf(fan);
    Com<ID3D11Buffer> b1 = r.buf(sizeof offsets, D3D11_BIND_VERTEX_BUFFER, offsets);
    if (!b0 || !b1) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* buffers[2] = {b0.get(), b1.get()};
    const UINT strides[2] = {sizeof(Vtx), 8};
    const UINT starts[2] = {0, 0};
    r.ctx->IASetVertexBuffers(0, 2, buffers, strides, starts);
    r.ctx->DrawInstanced(static_cast<UINT>(fan.size()), 4, 0, 3);
    return r.shot(out, target);
});

const Add kSvIds(spec("ia", "sv_vertexid_instanceid_geometry").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    const char* vs_source =
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "VO main(uint v : SV_VertexID, uint inst : SV_InstanceID) { VO o;\n"
        "  static const uint tbl[6] = {0, 1, 2, 1, 3, 2}; uint q = v / 6; uint k = v % 6; uint corner = tbl[k];\n"
        "  float2 c = float2(corner & 1, corner >> 1);\n"
        "  float2 cell = float2(q % 4, q / 4);\n"
        "  float2 p = ((cell + c * 0.8) / 4.0 * 1.6 - 0.8) * float2(1, 1) * (0.45) + float2(inst % 2 ? 0.5 : -0.5, inst / 2 ? -0.5 : 0.5);\n"
        "  o.p = float4(p, 0, 1); o.c = float3(cell / 4.0, 0.3 + 0.2 * inst) + float3(c * 0.2, 0.0); return o; }\n";
    Com<ID3D11VertexShader> vs = r.vs(vs_source);
    Com<ID3D11PixelShader> ps = r.ps(Hlsl2d::kPs);
    if (!r.color(target) || !vs || !ps) return false;
    r.bind(target);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->DrawInstanced(96, 4, 0, 0);
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
