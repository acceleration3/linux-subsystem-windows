#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kGsLineAdj =
    "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
    "[maxvertexcount(4)] void main(lineadj VO v[4], inout TriangleStream<VO> s) {\n"
    "  float2 a = v[1].p.xy, b = v[2].p.xy; float2 d = normalize(b - a); float2 n = float2(-d.y, d.x);\n"
    "  float w = 0.025 + 0.05 * length(v[0].p.xy - a) + 0.05 * length(v[3].p.xy - b);\n"
    "  VO o; o.c = v[1].c; o.p = float4(a + n * w, 0, 1); s.Append(o); o.p = float4(a - n * w, 0, 1); s.Append(o);\n"
    "  o.c = v[2].c; o.p = float4(b + n * w, 0, 1); s.Append(o); o.p = float4(b - n * w, 0, 1); s.Append(o); }\n";

const char* const kGsTriAdj =
    "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
    "[maxvertexcount(3)] void main(triangleadj VO v[6], inout TriangleStream<VO> s) {\n"
    "  float2 m = (v[0].p.xy + v[2].p.xy + v[4].p.xy) / 3; VO o;\n"
    "  o.c = v[1].c; o.p = float4(lerp(m, v[0].p.xy, 0.9), 0, 1); s.Append(o);\n"
    "  o.c = v[3].c; o.p = float4(lerp(m, v[2].p.xy, 0.9), 0, 1); s.Append(o);\n"
    "  o.c = v[5].c; o.p = float4(lerp(m, v[4].p.xy, 0.9), 0, 1); s.Append(o); }\n";

bool run_topology(Rig& r, Cap& out, const std::vector<Vtx>& verts, D3D11_PRIMITIVE_TOPOLOGY topology, const char* gs_source, UINT count = 0) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) {
        return false;
    }
    Com<ID3D11GeometryShader> geometry;
    if (gs_source != nullptr) {
        geometry = r.gs(gs_source);
        if (!geometry) {
            return false;
        }
    }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    Com<ID3D11RasterizerState> rs = r.rast(rdesc());
    if (!vb || !rs) {
        return false;
    }
    r.bind(target);
    basic.bind(r, topology);
    basic.vertices(r, vb.get());
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->RSSetState(rs.get());
    r.ctx->Draw(count != 0 ? count : static_cast<UINT>(verts.size()), 0);
    return r.shot(out, target);
}

std::vector<Vtx> spiral_points() {
    std::vector<Vtx> out;
    for (int i = 0; i < 360; ++i) {
        const float t = static_cast<float>(i) / 360.0f;
        const float a = t * 40.0f;
        out.push_back(vtx(std::cos(a) * (0.1f + 0.8f * t), std::sin(a) * (0.1f + 0.8f * t), t));
    }
    return out;
}

std::vector<Vtx> star_lines() {
    std::vector<Vtx> out;
    for (int i = 0; i < 16; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 16.0f;
        out.push_back(vtx(0.15f * std::cos(a), 0.15f * std::sin(a), 0.0f));
        out.push_back(vtx(0.85f * std::cos(a), 0.85f * std::sin(a), static_cast<float>(i) / 16.0f));
    }
    return out;
}

std::vector<Vtx> wave_strip() {
    std::vector<Vtx> out;
    for (int i = 0; i < 40; ++i) {
        const float t = static_cast<float>(i) / 39.0f;
        out.push_back(vtx(-0.9f + 1.8f * t, 0.5f * std::sin(t * 12.0f) * (1 - 0.5f * t), t));
    }
    return out;
}

std::vector<Vtx> zigzag_strip() {
    std::vector<Vtx> out;
    for (int i = 0; i < 14; ++i) {
        const float t = static_cast<float>(i) / 13.0f;
        out.push_back(vtx(-0.9f + 1.8f * t, (i % 2 == 0 ? 0.6f : -0.5f) + 0.2f * std::sin(t * 8.0f), t));
    }
    return out;
}

std::vector<Vtx> adj_lines() {
    std::vector<Vtx> out;
    for (int s = 0; s < 4; ++s) {
        const float y = -0.7f + 0.45f * static_cast<float>(s);
        out.push_back(vtx(-0.8f, y + 0.2f, 0.1f * s));
        out.push_back(vtx(-0.6f, y, 0.1f * s + 0.2f));
        out.push_back(vtx(0.6f, y + 0.1f * s, 0.1f * s + 0.5f));
        out.push_back(vtx(0.8f, y - 0.1f, 0.1f * s + 0.7f));
    }
    return out;
}

std::vector<Vtx> adj_line_strip() {
    std::vector<Vtx> out;
    for (int i = 0; i < 9; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        out.push_back(vtx(-0.8f + 1.6f * t, 0.6f * std::sin(t * 6.0f), t));
    }
    return out;
}

std::vector<Vtx> adj_triangles(bool strip) {
    std::vector<Vtx> out;
    const int triangles = strip ? 5 : 4;
    for (int t = 0; t < triangles; ++t) {
        const float cx = strip ? -0.6f + 0.3f * static_cast<float>(t) : (t % 2 == 0 ? -0.45f : 0.45f);
        const float cy = strip ? 0.0f : (t < 2 ? 0.45f : -0.45f);
        const float a = 0.5f * static_cast<float>(t);
        float p[3][2];
        for (int k = 0; k < 3; ++k) {
            p[k][0] = cx + 0.35f * std::cos(a + 2.0944f * static_cast<float>(k));
            p[k][1] = cy + 0.35f * std::sin(a + 2.0944f * static_cast<float>(k));
        }
        for (int k = 0; k < 3; ++k) {
            out.push_back(vtx(p[k][0], p[k][1], 0.2f * static_cast<float>(t) + 0.1f * static_cast<float>(k)));
            out.push_back(vtx(p[k][0] * 1.1f, p[k][1] * 1.1f, 0.2f * static_cast<float>(t) + 0.5f + 0.1f * static_cast<float>(k)));
        }
        if (strip) {
            break;
        }
    }
    if (strip) {
        out.clear();
        for (int i = 0; i < 12; ++i) {
            const float t = static_cast<float>(i) / 11.0f;
            const float x = -0.8f + 1.6f * t;
            if (i % 2 == 0) {
                out.push_back(vtx(x, (i / 2) % 2 == 0 ? 0.55f : -0.55f, t));
            } else {
                out.push_back(vtx(x, 0.0f, t + 0.3f));
            }
        }
        for (int i = 0; i < 12; i += 2) {
            Vtx& v = out[static_cast<std::size_t>(i)];
            v.y = (i / 2) % 2 == 0 ? 0.6f : -0.6f;
        }
    }
    return out;
}

const Add kPoints(spec("ia", "topology_point_list").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, spiral_points(), D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, nullptr);
});
const Add kLines(spec("ia", "topology_line_list").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    return run_topology(r, out, star_lines(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST, nullptr);
});
const Add kLineStrip(spec("ia", "topology_line_strip").tolerance(rr::Tolerance::raster(2, 2)), [](Rig& r, Cap& out) {
    return run_topology(r, out, wave_strip(), D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, nullptr);
});
const Add kTriList(spec("ia", "topology_triangle_list").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, ring_fan(7, 0.8f, 0.0f, 0.0f, 0.3f), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nullptr);
});
const Add kTriStrip(spec("ia", "topology_triangle_strip").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, zigzag_strip(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, nullptr);
});
const Add kLineListAdj(spec("ia", "topology_line_list_adj").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, adj_lines(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ, kGsLineAdj);
});
const Add kLineStripAdj(spec("ia", "topology_line_strip_adj").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, adj_line_strip(), D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ, kGsLineAdj);
});
const Add kTriListAdj(spec("ia", "topology_triangle_list_adj").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, adj_triangles(false), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ, kGsTriAdj);
});
const Add kTriStripAdj(spec("ia", "topology_triangle_strip_adj").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    return run_topology(r, out, adj_triangles(true), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ, kGsTriAdj);
});

struct PatchRow {
    unsigned n;
    bool quad;
};

std::string patch_eval(unsigned n) {
    if (n == 1) {
        return "float2 pos = p[0].p.xy + (uv - 0.5) * 0.9; float3 col = float3(uv, 1 - uv.x * 0.5) * p[0].c;\n";
    }
    if (n == 3) {
        return "float2 pos = p[0].p.xy * uv.x + p[1].p.xy * uv.y + p[2].p.xy * (1 - uv.x - uv.y);\n"
               "float3 col = p[0].c * uv.x + p[1].c * uv.y + p[2].c * (1 - uv.x - uv.y);\n";
    }
    if (n == 4) {
        return "float2 pos = lerp(lerp(p[0].p.xy, p[1].p.xy, uv.x), lerp(p[2].p.xy, p[3].p.xy, uv.x), uv.y);\n"
               "float3 col = lerp(lerp(p[0].c, p[1].c, uv.x), lerp(p[2].c, p[3].c, uv.x), uv.y);\n";
    }
    std::string text =
        "float4 bu = float4((1-uv.x)*(1-uv.x)*(1-uv.x), 3*uv.x*(1-uv.x)*(1-uv.x), 3*uv.x*uv.x*(1-uv.x), uv.x*uv.x*uv.x);\n"
        "float4 bv = float4((1-uv.y)*(1-uv.y)*(1-uv.y), 3*uv.y*(1-uv.y)*(1-uv.y), 3*uv.y*uv.y*(1-uv.y), uv.y*uv.y*uv.y);\n"
        "float2 pos = 0; float3 col = 0;\n"
        "[unroll] for (int j = 0; j < 4; ++j) [unroll] for (int i = 0; i < 4; ++i) { float w = bu[i] * bv[j];\n";
    if (n == 16) {
        text += "  pos += w * p[j*4+i].p.xy; col += w * p[j*4+i].c; }\n";
    } else {
        text += "  pos += w * 0.5 * (p[j*4+i].p.xy + p[16+j*4+i].p.xy); col += w * 0.5 * (p[j*4+i].c + p[16+j*4+i].c); }\n";
    }
    return text;
}

std::vector<Vtx> patch_points(unsigned n) {
    std::vector<Vtx> out;
    if (n == 1) {
        out.push_back(vtx(0.0f, 0.0f, 0.1f));
        out.back().r = out.back().g = out.back().b = 1.0f;
    } else if (n == 3) {
        out = {vtx(-0.8f, -0.7f, 0.0f), vtx(0.8f, -0.6f, 0.33f), vtx(0.0f, 0.8f, 0.66f)};
    } else if (n == 4) {
        out = {vtx(-0.8f, -0.8f, 0.0f), vtx(0.7f, -0.6f, 0.25f), vtx(-0.6f, 0.8f, 0.5f), vtx(0.8f, 0.7f, 0.75f)};
    } else {
        for (unsigned k = 0; k < n; ++k) {
            const unsigned j = (k % 16) / 4;
            const unsigned i = k % 4;
            const float x = -0.8f + 0.533f * static_cast<float>(i);
            const float y = -0.8f + 0.533f * static_cast<float>(j);
            const float bend = (k < 16 ? 0.12f : -0.12f) * std::sin(static_cast<float>(i * 3 + j));
            out.push_back(vtx(x + bend, y - bend, static_cast<float>(k) / static_cast<float>(n)));
        }
    }
    return out;
}

bool run_patch(Rig& r, Cap& out, unsigned n, bool quad, const char* partition, const char* topology, float outer, float inner, bool wire,
               unsigned variant = 0) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) {
        return false;
    }
    const std::string count = std::to_string(n);
    const std::string input = std::string(Hlsl2d::kVo);
    std::string hull = input;
    if (quad) {
        hull += "struct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
                "HC pc(InputPatch<VO," + count + "> p) { HC h; h.e[0] = h.e[1] = h.e[2] = h.e[3] = " + std::to_string(outer) +
                "; h.i[0] = h.i[1] = " + std::to_string(inner) + "; return h; }\n";
    } else {
        hull += "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
                "HC pc(InputPatch<VO," + count + "> p) { HC h; h.e[0] = h.e[1] = h.e[2] = " + std::to_string(outer) + "; h.i = " +
                std::to_string(inner) + "; return h; }\n";
    }
    (void)variant;
    hull += std::string("[domain(\"") + (quad ? "quad" : "tri") + "\")][partitioning(\"" + partition + "\")][outputtopology(\"" + topology +
            "\")][outputcontrolpoints(" + count + ")][patchconstantfunc(\"pc\")]\n"
            "VO main(InputPatch<VO," + count + "> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    std::string domain = input;
    domain += quad ? "struct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
                   : "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n";
    domain += std::string("[domain(\"") + (quad ? "quad" : "tri") + "\")]\nVO main(HC hc, " + (quad ? "float2 uv" : "float3 bc") +
              " : SV_DomainLocation, const OutputPatch<VO," + count + "> p) {\n";
    if (!quad) {
        domain += "  float2 uv = bc.xy;\n";
    }
    domain += patch_eval(n) + "  VO o; o.p = float4(pos, 0, 1); o.c = col; return o; }\n";
    Com<ID3D11HullShader> hull_shader = r.hs(hull);
    Com<ID3D11DomainShader> domain_shader = r.ds(domain);
    if (!hull_shader || !domain_shader) {
        return false;
    }
    const std::vector<Vtx> points = patch_points(n);
    Com<ID3D11Buffer> vb = r.vbuf(points);
    Com<ID3D11RasterizerState> rs = r.rast(rdesc(wire ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID));
    if (!vb || !rs) {
        return false;
    }
    r.bind(target);
    basic.bind(r, static_cast<D3D11_PRIMITIVE_TOPOLOGY>(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + n - 1));
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(hull_shader.get(), nullptr, 0);
    r.ctx->DSSetShader(domain_shader.get(), nullptr, 0);
    r.ctx->RSSetState(rs.get());
    r.ctx->Draw(n, 0);
    return r.shot(out, target);
}

struct PatchRegistrar {
    PatchRegistrar() {
        static const PatchRow rows[] = {{1, true}, {3, false}, {4, true}, {16, true}, {32, true}};
        for (const PatchRow& row : rows) {
            Add(spec("ia", ("topology_patch_" + std::to_string(row.n)).c_str()).tolerance(rr::Tolerance::raster(2, 1)),
                [row](Rig& r, Cap& out) { return run_patch(r, out, row.n, row.quad, "integer", "triangle_cw", 6.0f, 6.0f, false); });
        }
    }
};
const PatchRegistrar kPatchRegistrar;

}  // namespace

bool run_patch_public(Rig& r, Cap& out, unsigned n, bool quad, const char* partition, const char* topology, float outer, float inner, bool wire) {
    return run_patch(r, out, n, quad, partition, topology, outer, inner, wire);
}

}  // namespace lsw::suites::render::d3d11
