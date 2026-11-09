#include "render/d3d10/util.hpp"

#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

using Top = D3D10_PRIMITIVE_TOPOLOGY;

void run_topology(Rig& r, rr::CaptureSet& out, Top topology, const std::vector<Vtx>& vertices, const std::vector<std::uint16_t>* indices = nullptr,
                  const char* gs_source = nullptr) {
    r.begin();
    r.use_std();
    Com<ID3D10GeometryShader> shader;
    if (gs_source != nullptr) {
        shader = r.gs(gs_source);
    }
    if (!r.ok()) {
        return;
    }
    r.d->GSSetShader(shader.get());
    Com<ID3D10Buffer> vb = r.vbuf(vertices);
    r.std_vb(vb.get());
    r.d->IASetPrimitiveTopology(topology);
    if (indices != nullptr) {
        Com<ID3D10Buffer> ib = r.ibuf16(*indices);
        r.d->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
        r.d->DrawIndexed(static_cast<UINT>(indices->size()), 0, 0);
    } else {
        r.d->Draw(static_cast<UINT>(vertices.size()), 0);
    }
    r.cap_main(out);
}

std::vector<Vtx> spokes(unsigned count) {
    std::vector<Vtx> v;
    for (unsigned i = 0; i < count; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        v.push_back(vtx(0.1f * std::cos(a), 0.1f * std::sin(a), 0.5f, palette(i)));
        v.push_back(vtx(0.85f * std::cos(a), 0.85f * std::sin(a), 0.5f, palette(i + 3)));
    }
    return v;
}

std::vector<Vtx> wave(unsigned count) {
    std::vector<Vtx> v;
    for (unsigned i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        v.push_back(vtx(-0.9f + 1.8f * t, 0.6f * std::sin(t * 9.0f), 0.5f, palette(i)));
    }
    return v;
}

std::vector<Vtx> ribbon(unsigned pairs) {
    std::vector<Vtx> v;
    for (unsigned i = 0; i < pairs; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(pairs - 1);
        const float x = -0.9f + 1.8f * t;
        const float y = 0.4f * std::sin(t * 6.0f);
        v.push_back(vtx(x, y + 0.3f, 0.5f, palette(i)));
        v.push_back(vtx(x, y - 0.3f, 0.5f, palette(i + 4)));
    }
    return v;
}

std::vector<Vtx> triangles() {
    std::vector<Vtx> v;
    const float centers[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned k = 0; k < 3; ++k) {
            const float a = 1.5708f + 2.0944f * static_cast<float>(k) - 0.2f * static_cast<float>(i);
            v.push_back(vtx(centers[i][0] + 0.4f * std::cos(a), centers[i][1] + 0.4f * std::sin(a), 0.5f, palette(i * 3 + k)));
        }
    }
    return v;
}

const char* kLineAdjGs =
    "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "[maxvertexcount(4)] void main(lineadj VO v[4], inout TriangleStream<VO> s) {\n"
    "  float2 d = normalize(v[2].p.xy - v[1].p.xy); float2 n = float2(-d.y, d.x) * 0.04;\n"
    "  float4 col = (v[0].c + v[3].c) * 0.5; VO o; o.uv = 0; o.c = col;\n"
    "  o.p = float4(v[1].p.xy + n, 0.5, 1); s.Append(o); o.p = float4(v[1].p.xy - n, 0.5, 1); s.Append(o);\n"
    "  o.p = float4(v[2].p.xy + n, 0.5, 1); s.Append(o); o.p = float4(v[2].p.xy - n, 0.5, 1); s.Append(o); }\n";

const char* kTriAdjGs =
    "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "[maxvertexcount(3)] void main(triangleadj VO v[6], inout TriangleStream<VO> s) {\n"
    "  float4 col = (v[1].c + v[3].c + v[5].c) / 3; VO o; o.uv = 0; o.c = col;\n"
    "  o.p = v[0].p; s.Append(o); o.p = v[2].p; s.Append(o); o.p = v[4].p; s.Append(o); }\n";

std::vector<Vtx> line_adj_list() {
    std::vector<Vtx> v;
    for (unsigned i = 0; i < 5; ++i) {
        const float y = 0.7f - 0.35f * static_cast<float>(i);
        v.push_back(vtx(-0.95f, y, 0.5f, palette(i)));
        v.push_back(vtx(-0.7f, y, 0.5f, palette(i)));
        v.push_back(vtx(0.7f, y - 0.1f, 0.5f, palette(i + 1)));
        v.push_back(vtx(0.95f, y, 0.5f, palette(i + 2)));
    }
    return v;
}

std::vector<Vtx> tri_adj_list() {
    std::vector<Vtx> v;
    const float centers[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
    for (unsigned i = 0; i < 4; ++i) {
        const float cx = centers[i][0];
        const float cy = centers[i][1];
        const Vtx corners[3] = {vtx(cx - 0.35f, cy - 0.3f, 0.5f, palette(i)), vtx(cx + 0.35f, cy - 0.3f, 0.5f, palette(i + 2)), vtx(cx, cy + 0.35f, 0.5f, palette(i + 4))};
        const Vtx adjacent[3] = {vtx(cx, cy - 0.5f, 0.5f, palette(i + 1)), vtx(cx + 0.5f, cy, 0.5f, palette(i + 3)), vtx(cx - 0.5f, cy, 0.5f, palette(i + 5))};
        v.push_back(corners[0]);
        v.push_back(adjacent[0]);
        v.push_back(corners[1]);
        v.push_back(adjacent[1]);
        v.push_back(corners[2]);
        v.push_back(adjacent[2]);
    }
    return v;
}

struct FormatCase {
    const char* name;
    DXGI_FORMAT format;
    char kind;
    int comps;
    char type;
    int bytes;
};

void encode(const FormatCase& f, const float* c, std::uint8_t* out) {
    for (int i = 0; i < f.comps; ++i) {
        const float v = c[i];
        const std::int32_t unit = static_cast<std::int32_t>(std::lround(v * 255.0f));
        switch (f.type) {
            case 'F': std::memcpy(out + i * 4, &v, 4); break;
            case 'H': { const std::uint16_t h = rr::PixelInfo::float_to_half(v); std::memcpy(out + i * 2, &h, 2); break; }
            case 'n': { const std::uint16_t u = static_cast<std::uint16_t>(std::lround(v * 65535.0f)); std::memcpy(out + i * 2, &u, 2); break; }
            case 's': { const std::uint16_t u = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767.0f))); std::memcpy(out + i * 2, &u, 2); break; }
            case 'b': out[i] = static_cast<std::uint8_t>(unit); break;
            case 'B': out[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::lround(v * 127.0f))); break;
            case 'u': { const std::uint16_t u = static_cast<std::uint16_t>(unit); std::memcpy(out + i * 2, &u, 2); break; }
            case 'U': { const std::uint32_t u = static_cast<std::uint32_t>(unit); std::memcpy(out + i * 4, &u, 4); break; }
            case 'i': { const std::int16_t u = static_cast<std::int16_t>(unit - 128); std::memcpy(out + i * 2, &u, 2); break; }
            case 'I': { const std::int32_t u = unit - 128; std::memcpy(out + i * 4, &u, 4); break; }
            case 'c': out[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(unit - 128)); break;
            default: break;
        }
    }
    if (f.type == 'R') {
        const std::uint32_t v = static_cast<std::uint32_t>(std::lround(c[0] * 1023.0f)) | (static_cast<std::uint32_t>(std::lround(c[1] * 1023.0f)) << 10) |
                                (static_cast<std::uint32_t>(std::lround(c[2] * 1023.0f)) << 20) | (3u << 30);
        std::memcpy(out, &v, 4);
    }
    if (f.type == 'Y') {
        for (int i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::lround(c[i] * 255.0f) - 128));
    }
}

const FormatCase kFormats[] = {
    {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, 'f', 4, 'F', 16}, {"r32g32b32_float", DXGI_FORMAT_R32G32B32_FLOAT, 'f', 3, 'F', 12},
    {"r32g32_float", DXGI_FORMAT_R32G32_FLOAT, 'f', 2, 'F', 8},               {"r32_float", DXGI_FORMAT_R32_FLOAT, 'f', 1, 'F', 4},
    {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, 'f', 2, 'H', 4},               {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, 'f', 4, 'H', 8},
    {"r16_float", DXGI_FORMAT_R16_FLOAT, 'f', 1, 'H', 2},                     {"r16g16_snorm", DXGI_FORMAT_R16G16_SNORM, 'f', 2, 's', 4},
    {"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM, 'f', 4, 's', 8},   {"r16g16_unorm", DXGI_FORMAT_R16G16_UNORM, 'f', 2, 'n', 4},
    {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, 'f', 4, 'n', 8},   {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, 'f', 4, 'b', 4},
    {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, 'f', 4, 'z', 4},           {"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM, 'f', 4, 'B', 4},
    {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, 'f', 2, 'b', 2},                   {"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT, 'u', 4, 'b', 4},
    {"r16g16_uint", DXGI_FORMAT_R16G16_UINT, 'u', 2, 'u', 4},                 {"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT, 'u', 4, 'u', 8},
    {"r32_uint", DXGI_FORMAT_R32_UINT, 'u', 1, 'U', 4},                       {"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT, 'u', 4, 'U', 16},
    {"r8g8b8a8_sint", DXGI_FORMAT_R8G8B8A8_SINT, 'i', 4, 'c', 4},             {"r16g16_sint", DXGI_FORMAT_R16G16_SINT, 'i', 2, 'i', 4},
    {"r16g16b16a16_sint", DXGI_FORMAT_R16G16B16A16_SINT, 'i', 4, 'i', 8},     {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, 'f', 3, 'R', 4},
};

void run_format(Rig& r, rr::CaptureSet& out, const FormatCase& fc) {
    r.begin();
    const char* type = fc.kind == 'f' ? "float4" : fc.kind == 'u' ? "uint4" : "int4";
    const std::string fix = fc.kind == 'i' ? " + 128.0" : "";
    std::string src = std::string("struct VI { float3 p : POSITION; ") + type + " c : COLOR; };\n" +
                      "struct VO { float4 p : SV_Position; float4 c : COLOR; };\n" +
                      "VO main(VI i) { VO o; o.p = float4(i.p, 1); float4 v = float4(i.c)" + fix + "; o.c = float4(" + (fc.kind == 'f' ? "v.rgb" : "v.rgb / 255.0") + ", 1); return o; }\n";
    Com<ID3D10VertexShader> vs = r.vs(src);
    Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
    Com<ID3D10InputLayout> layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"COLOR", 0, fc.format, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0}});
    if (!r.ok()) {
        return;
    }
    const UINT stride = 12 + static_cast<UINT>(fc.bytes);
    std::vector<std::uint8_t> data;
    for (unsigned t = 0; t < 12; ++t) {
        for (unsigned k = 0; k < 3; ++k) {
            const float a = 6.2831853f * (static_cast<float>(t) + (k == 0 ? 0.0f : k == 1 ? 0.9f : 0.45f)) / 12.0f;
            const float rad = k == 2 ? 0.35f : 0.9f;
            const float pos[3] = {rad * std::cos(a), rad * std::sin(a), 0.5f};
            const float* col = palette(t + k * 3);
            std::uint8_t record[48] = {};
            std::memcpy(record, pos, 12);
            FormatCase f = fc;
            if (f.type == 'z') {
                const std::uint8_t bgra[4] = {static_cast<std::uint8_t>(std::lround(col[2] * 255)), static_cast<std::uint8_t>(std::lround(col[1] * 255)),
                                              static_cast<std::uint8_t>(std::lround(col[0] * 255)), static_cast<std::uint8_t>(std::lround(col[3] * 255))};
                std::memcpy(record + 12, bgra, 4);
            } else {
                encode(f, col, record + 12);
            }
            data.insert(data.end(), record, record + stride);
        }
    }
    Com<ID3D10Buffer> vb = r.buf(static_cast<UINT>(data.size()), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, data.data());
    if (!r.ok()) {
        return;
    }
    const UINT offset = 0;
    ID3D10Buffer* buffer = vb.get();
    r.d->IASetInputLayout(layout.get());
    r.d->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.d->VSSetShader(vs.get());
    r.d->PSSetShader(ps.get());
    r.d->Draw(36, 0);
    r.cap_main(out);
}

const Registrar kTopologies{[] {
    single(spec("ia", "topology_point_list").tolerance(lines_tol()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_POINTLIST, ring_points(60, 0.85f)); });
    single(spec("ia", "topology_line_list").tolerance(lines_tol()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_LINELIST, spokes(16)); });
    single(spec("ia", "topology_line_strip").tolerance(lines_tol()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP, wave(40)); });
    single(spec("ia", "topology_triangle_list").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, triangles()); });
    single(spec("ia", "topology_triangle_strip").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, ribbon(12)); });
    single(spec("ia", "strip_restart_line").tolerance(lines_tol()), [](Rig& r, rr::CaptureSet& o) {
        std::vector<std::uint16_t> idx;
        for (unsigned i = 0; i < 20; ++i) idx.push_back(static_cast<std::uint16_t>(i));
        idx.push_back(0xffff);
        for (unsigned i = 20; i < 40; ++i) idx.push_back(static_cast<std::uint16_t>(i));
        std::vector<Vtx> v = wave(40);
        for (unsigned i = 20; i < 40; ++i) v[i].y = -v[i].y * 0.7f - 0.1f;
        run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP, v, &idx);
    });
    single(spec("ia", "strip_restart_triangle").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        std::vector<std::uint16_t> idx = {0, 1, 2, 3, 4, 5, 0xffff, 6, 7, 8, 9, 10, 11};
        std::vector<Vtx> v = ribbon(6);
        for (unsigned i = 0; i < 6; ++i) {
            v[i].y = v[i].y * 0.5f + 0.5f;
            v[6 + i].y = v[6 + i].y * 0.5f - 0.5f;
        }
        std::vector<Vtx> w = v;
        w.resize(12);
        for (unsigned i = 0; i < 6; ++i) {
            w[6 + i] = v[i];
            w[6 + i].y -= 1.0f;
            w[6 + i].r = 1 - w[6 + i].r;
        }
        run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, w, &idx);
    });
    single(spec("ia", "adjacency_line_list").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_LINELIST_ADJ, line_adj_list(), nullptr, kLineAdjGs); });
    single(spec("ia", "adjacency_line_strip").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        std::vector<Vtx> v = wave(14);
        run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ, v, nullptr, kLineAdjGs);
    });
    single(spec("ia", "adjacency_triangle_list").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ, tri_adj_list(), nullptr, kTriAdjGs); });
    single(spec("ia", "adjacency_triangle_strip").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        std::vector<Vtx> v;
        for (unsigned i = 0; i < 12; ++i) {
            const unsigned pair = i / 2;
            const bool main_row = (i % 2) == 0;
            const float x = -0.9f + 0.3f * static_cast<float>(pair);
            const float y = (pair % 2 == 0) ? (main_row ? 0.6f : -0.5f) : (main_row ? -0.5f : 0.6f);
            v.push_back(vtx(x, y, 0.5f, palette(i)));
        }
        run_topology(r, o, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ, v, nullptr, kTriAdjGs);
    });
}};

const Registrar kFormatCases{[] {
    for (const FormatCase& fc : kFormats) {
        const FormatCase copy = fc;
        single(spec("ia", std::string("format_") + fc.name, fc.name[0] == 'b' ? 1 : 0).tolerance(rr::Tolerance::blend(3)),
               [copy](Rig& r, rr::CaptureSet& o) { run_format(r, o, copy); },
               [copy](Rig& r) -> std::string {
                   return (r.format_support(copy.format) & D3D10_FORMAT_SUPPORT_IA_VERTEX_BUFFER) != 0 ? "" : std::string("IA vertex format ") + copy.name;
               });
    }
}};

const Registrar kSlots{[] {
    single(spec("ia", "sixteen_vertex_slots").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        std::string src = "struct VI { float2 p : POSITION;";
        for (int i = 0; i < 15; ++i) src += " float t" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) + ";";
        src += " };\nstruct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
               "VO main(VI i) { VO o; o.p = float4(i.p, 0.5, 1);\n"
               "  o.c = float4(i.t0 + i.t1 * 0.5 + i.t2 * 0.25 + i.t3 * 0.125 + i.t4 * 0.0625, i.t5 + i.t6 * 0.5 + i.t7 * 0.25 + i.t8 * 0.125 + i.t9 * 0.0625,\n"
               "    i.t10 + i.t11 * 0.5 + i.t12 * 0.25 + i.t13 * 0.125 + i.t14 * 0.0625, 1); return o; }\n";
        Com<ID3D10VertexShader> vs = r.vs(src);
        Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
        std::vector<D3D10_INPUT_ELEMENT_DESC> elements = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0}};
        for (UINT i = 0; i < 15; ++i) elements.push_back({"TEXCOORD", i, DXGI_FORMAT_R32_FLOAT, i + 1, 0, D3D10_INPUT_PER_VERTEX_DATA, 0});
        Com<ID3D10InputLayout> layout = r.layout(elements);
        if (!r.ok()) return;
        const float pos[12] = {-0.9f, 0.9f, 0.9f, 0.9f, 0.9f, -0.9f, -0.9f, 0.9f, 0.9f, -0.9f, -0.9f, -0.9f};
        std::vector<Com<ID3D10Buffer>> buffers;
        buffers.push_back(r.vbuf(pos, sizeof pos));
        for (unsigned s = 0; s < 15; ++s) {
            float values[6];
            for (unsigned v = 0; v < 6; ++v) {
                const unsigned corner = v == 3 ? 0 : v == 4 ? 2 : v == 5 ? 3 : v;
                values[v] = ((corner + s) % 4) / 3.0f;
            }
            buffers.push_back(r.vbuf(values, sizeof values));
        }
        if (!r.ok()) return;
        ID3D10Buffer* raw[16];
        UINT strides[16];
        UINT offsets[16] = {};
        for (unsigned i = 0; i < 16; ++i) {
            raw[i] = buffers[i].get();
            strides[i] = i == 0 ? 8 : 4;
        }
        r.d->IASetInputLayout(layout.get());
        r.d->IASetVertexBuffers(0, 16, raw, strides, offsets);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.d->Draw(6, 0);
        r.cap_main(o);
    });
}};

const char* kInstVs =
    "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; float3 off : OFFSET; float4 tint : TINT; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
    "VO main(VI i, uint iid : SV_InstanceID) { VO o; float3 shift = float3(0.0, -0.1 * (iid % 3), 0); o.p = float4(i.p * 0.12 + i.off + shift, 1); o.c = i.c * i.tint * (0.7 + 0.1 * (iid % 4)); return o; }\n";

struct InstanceData {
    float off[3];
    float tint[4];
};

std::vector<InstanceData> instance_grid(unsigned count) {
    std::vector<InstanceData> v;
    for (unsigned i = 0; i < count; ++i) {
        const float x = -0.75f + 0.5f * static_cast<float>(i % 4);
        const float y = 0.7f - 0.45f * static_cast<float>(i / 4);
        const float* p = palette(i);
        v.push_back({{x, y, 0.5f}, {p[0], p[1], p[2], 1}});
    }
    return v;
}

void run_instanced(Rig& r, rr::CaptureSet& out, UINT step_rate, int mode) {
    r.begin();
    Com<ID3D10VertexShader> vs = r.vs(kInstVs);
    Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
    Com<ID3D10InputLayout> layout =
        r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                  {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0},
                  {"OFFSET", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D10_INPUT_PER_INSTANCE_DATA, r.mutate("ia_step") ? 1u : step_rate},
                  {"TINT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 12, D3D10_INPUT_PER_INSTANCE_DATA, r.mutate("ia_step") ? 1u : step_rate}});
    const std::vector<InstanceData> inst = instance_grid(16);
    Com<ID3D10Buffer> ib = r.vbuf(inst.data(), static_cast<UINT>(inst.size() * sizeof(InstanceData)));
    const std::vector<Vtx> quad = Geo::quad(-1, -1, 1, 1, 0, 1, 1, 1);
    std::vector<Vtx> colored = quad;
    for (unsigned i = 0; i < colored.size(); ++i) {
        const float* c = palette(i % 6 + 1);
        colored[i].r = 0.5f + 0.5f * c[0];
        colored[i].g = 0.5f + 0.5f * c[1];
        colored[i].b = 0.5f + 0.5f * c[2];
    }
    Com<ID3D10Buffer> vb = r.vbuf(colored);
    Com<ID3D10Buffer> index = r.ibuf16({0, 1, 2, 3, 4, 5});
    if (!r.ok()) return;
    ID3D10Buffer* buffers[2] = {vb.get(), ib.get()};
    const UINT strides[2] = {sizeof(Vtx), sizeof(InstanceData)};
    const UINT offsets[2] = {0, 0};
    r.d->IASetInputLayout(layout.get());
    r.d->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    r.d->IASetIndexBuffer(index.get(), DXGI_FORMAT_R16_UINT, 0);
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.d->VSSetShader(vs.get());
    r.d->PSSetShader(ps.get());
    if (mode == 0) {
        r.d->DrawInstanced(6, 16, 0, 0);
    } else if (mode == 1) {
        r.d->DrawIndexedInstanced(6, 10, 0, 0, 3);
    } else {
        r.d->DrawInstanced(6, 8, 0, 6);
    }
    r.cap_main(out);
}

const Registrar kInstancing{[] {
    for (UINT rate : {1u, 2u, 3u}) {
        single(spec("ia", "instance_step_rate_" + std::to_string(rate)).tolerance(rr::Tolerance::blend()),
               [rate](Rig& r, rr::CaptureSet& o) { run_instanced(r, o, rate, 0); });
    }
    single(spec("ia", "draw_indexed_instanced").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { run_instanced(r, o, 1, 1); });
    single(spec("ia", "draw_instanced_start_instance").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { run_instanced(r, o, 1, 2); });
}};

void indexed_fan(Rig& r, rr::CaptureSet& out, bool wide, UINT base_vertex, UINT start_index, UINT ib_offset_bytes, UINT vb_offset_vertices) {
    r.begin();
    r.use_std();
    std::vector<Vtx> ring;
    ring.push_back(vtx(0, 0, 0.5f, palette(7)));
    for (unsigned i = 0; i < 12; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 12.0f;
        ring.push_back(vtx(0.9f * std::cos(a), 0.9f * std::sin(a), 0.5f, palette(i)));
    }
    std::vector<Vtx> padded(vb_offset_vertices + base_vertex);
    for (auto& p : padded) p = vtx(5, 5, 0.5f, palette(0));
    padded.insert(padded.end(), ring.begin(), ring.end());
    Com<ID3D10Buffer> vb = r.vbuf(padded);
    std::vector<std::uint32_t> idx(start_index + ib_offset_bytes / (wide ? 4 : 2), 0);
    for (unsigned i = 0; i < 12; ++i) {
        idx.push_back(0);
        idx.push_back(1 + (i + 1) % 12);
        idx.push_back(1 + i);
    }
    Com<ID3D10Buffer> ib;
    if (wide) {
        ib = r.ibuf32(idx);
    } else {
        std::vector<std::uint16_t> narrow(idx.begin(), idx.end());
        ib = r.ibuf16(narrow);
    }
    if (!r.ok()) return;
    r.std_vb(vb.get(), 0, vb_offset_vertices * sizeof(Vtx));
    r.d->IASetIndexBuffer(ib.get(), wide ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, ib_offset_bytes);
    r.d->DrawIndexed(36, start_index, static_cast<INT>(base_vertex));
    r.cap_main(out);
}

const Registrar kIndexing{[] {
    single(spec("ia", "index_16bit").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, false, 0, 0, 0, 0); });
    single(spec("ia", "index_32bit").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, true, 0, 0, 0, 0); });
    single(spec("ia", "index_base_vertex").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, false, 7, 0, 0, 0); });
    single(spec("ia", "index_start_location").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, true, 0, 9, 0, 0); });
    single(spec("ia", "index_buffer_offset").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, false, 0, 0, 24, 0); });
    single(spec("ia", "vertex_buffer_offset").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { indexed_fan(r, o, true, 0, 0, 0, 5); });
    single(spec("ia", "draw_start_vertex").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<Vtx> v = triangles();
        v.insert(v.begin(), 9, vtx(3, 3, 0.5f, palette(0)));
        Com<ID3D10Buffer> vb = r.vbuf(v);
        r.std_vb(vb.get());
        r.d->Draw(6, 9 + 6);
        r.cap_main(o);
    });
    single(spec("ia", "system_values_vertex_instance_id").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        Com<ID3D10VertexShader> vs = r.vs(
            "struct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
            "VO main(uint vid : SV_VertexID, uint iid : SV_InstanceID) { VO o; float2 corner = float2((vid & 1) ? 1 : -1, (vid & 2) ? -1 : 1);\n"
            "  float2 cell = float2(iid % 4, iid / 4);\n"
            "  o.p = float4(corner * 0.2 + float2(-0.75, 0.75) + cell * float2(0.5, -0.5), 0.5, 1);\n"
            "  o.c = float4(frac(iid * 0.37 + 0.1), frac(iid * 0.61 + vid * 0.05), (vid + 1) * 0.25, 1); return o; }\n");
        Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
        if (!r.ok()) return;
        r.d->IASetInputLayout(nullptr);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.d->DrawInstanced(4, 16, 0, 0);
        r.cap_main(o);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
