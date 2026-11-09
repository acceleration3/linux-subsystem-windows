#include "render/d3d12/gfx.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

const char* kTessVs =
    "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct CP { float3 pos : POSITION; };\n"
    "CP main(VsIn i) { CP o; o.pos = i.pos; return o; }\n";

const char* kTessPs =
    "struct DsOut { float4 pos : SV_Position; float3 wp : WP; };\n"
    "float4 main(DsOut i) : SV_Target {\n"
    "  float3 n = normalize(cross(ddx(i.wp), ddy(i.wp)));\n"
    "  float l = abs(dot(n, normalize(float3(0.4, 0.6, 0.7)))) * 0.75 + 0.25;\n"
    "  return float4(p0.rgb * l, 1); }\n";

const char* kLinePs =
    "struct DsOut { float4 pos : SV_Position; float3 wp : WP; };\n"
    "float4 main(DsOut i) : SV_Target { return float4(p0.rgb * (0.6 + 0.4 * frac(i.wp.x * 4.0)), 1); }\n";

std::string tess_hull(const char* domain, const char* partition, int points, const char* topology) {
    std::string edges = std::string(domain) == "tri" ? "3" : (std::string(domain) == "quad" ? "4" : "2");
    std::string text = std::string(Hlsl12::kRoot) + "struct CP { float3 pos : POSITION; };\n";
    text += "struct Patch { float edges[" + edges + "] : SV_TessFactor;";
    if (std::string(domain) == "tri") {
        text += " float inside : SV_InsideTessFactor; };\n";
    } else if (std::string(domain) == "quad") {
        text += " float inside[2] : SV_InsideTessFactor; };\n";
    } else {
        text += " };\n";
    }
    text += "Patch pc(InputPatch<CP, " + std::to_string(points) + "> p) { Patch o;\n";
    if (std::string(domain) == "tri") {
        text += "  o.edges[0] = p1.x; o.edges[1] = p1.y; o.edges[2] = p1.z; o.inside = p1.w;\n";
    } else if (std::string(domain) == "quad") {
        text += "  o.edges[0] = p1.x; o.edges[1] = p1.y; o.edges[2] = p1.z; o.edges[3] = p1.x; o.inside[0] = p1.w; o.inside[1] = p1.w * 0.7;\n";
    } else {
        text += "  o.edges[0] = p1.x; o.edges[1] = p1.y;\n";
    }
    text += "  return o; }\n";
    text += std::string("[domain(\"") + domain + "\")][partitioning(\"" + partition + "\")][outputtopology(\"" + topology + "\")][outputcontrolpoints(" +
            std::to_string(points) + ")][patchconstantfunc(\"pc\")]\n";
    text += "CP main(InputPatch<CP, " + std::to_string(points) + "> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    return text;
}

std::string tess_domain(const char* domain, int points) {
    std::string text = std::string(Hlsl12::kRoot) + "struct CP { float3 pos : POSITION; };\nstruct DsOut { float4 pos : SV_Position; float3 wp : WP; };\n";
    const std::string d = domain;
    if (d == "tri") {
        text += "struct Patch { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };\n";
        text += "[domain(\"tri\")] DsOut main(Patch c, float3 loc : SV_DomainLocation, const OutputPatch<CP, 3> p) {\n"
                "  float3 w = p[0].pos * loc.x + p[1].pos * loc.y + p[2].pos * loc.z;\n";
    } else if (d == "quad") {
        text += "struct Patch { float edges[4] : SV_TessFactor; float inside[2] : SV_InsideTessFactor; };\n";
        text += "[domain(\"quad\")] DsOut main(Patch c, float2 loc : SV_DomainLocation, const OutputPatch<CP, 4> p) {\n"
                "  float3 w = lerp(lerp(p[0].pos, p[1].pos, loc.x), lerp(p[3].pos, p[2].pos, loc.x), loc.y);\n";
    } else {
        text += "struct Patch { float edges[2] : SV_TessFactor; };\n";
        text += "[domain(\"isoline\")] DsOut main(Patch c, float2 loc : SV_DomainLocation, const OutputPatch<CP, 2> p) {\n"
                "  float3 w = lerp(p[0].pos, p[1].pos, loc.x); w.y += (loc.y - 0.5) * 0.5 + 0.1 * sin(loc.x * 9.0);\n";
    }
    (void)points;
    text += "  w.z += (0.28 * sin(w.x * 6.0) * cos(w.y * 5.0));\n"
            "  DsOut o; o.wp = w; o.pos = mul(mvp, float4(w, 1)); return o; }\n";
    return text;
}


struct DomainCase {
    const char* domain;
    int points;
    D3D_PRIMITIVE_TOPOLOGY topology;
    const char* output;
};

const DomainCase kDomains[] = {{"tri", 3, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, "triangle_cw"},
                               {"quad", 4, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST, "triangle_cw"},
                               {"isoline", 2, D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST, "line"}};

std::vector<Vertex> patch_vertices(const DomainCase& domain) {
    std::vector<Vertex> out;
    auto v = [&](float x, float y) { out.push_back({{x, y, 0}, {0, 0, 1}, {0, 0}}); };
    const std::string d = domain.domain;
    if (d == "tri") {
        v(-0.9f, -0.8f); v(0.0f, 0.9f); v(0.9f, -0.8f);
    } else if (d == "quad") {
        v(-0.85f, -0.85f); v(0.85f, -0.85f); v(0.85f, 0.85f); v(-0.85f, 0.85f);
    } else {
        for (int k = 0; k < 6; ++k) {
            const float y = -0.8f + 0.3f * static_cast<float>(k);
            v(-0.9f, y);
            v(0.9f, y);
        }
    }
    return out;
}

bool draw_tess(Kit& kit, const DomainCase& domain, const char* partition, const float p0[4], const float p1[4], rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
        return false;
    }
    const std::vector<Vertex> vertices = patch_vertices(domain);
    Geometry geometry;
    if (!geometry.create_vertices(kit, vertices.data(), static_cast<UINT>(vertices.size()), sizeof(Vertex), error)) {
        return false;
    }
    Pso builder(kit, stage.canvas.binder.signature);
    const bool line = std::string(domain.output) == "line";
    builder.vs(kTessVs)
        .stage(tess_hull(domain.domain, partition, domain.points, domain.output), "main", "hs")
        .stage(tess_domain(domain.domain, domain.points), "main", "ds")
        .ps(Hlsl12::with_root(line ? kLinePs : kTessPs))
        .targets({DXGI_FORMAT_R8G8B8A8_UNORM})
        .depth(DXGI_FORMAT_D32_FLOAT);
    builder.desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            list->SetPipelineState(state);
            geometry.bind(list, domain.topology);
            const rr::Mat4 matrix = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.8f) * rr::Mat4::rotate_x(-0.55f) * rr::Mat4::rotate_y(0.25f);
            set_cbv_root(list, matrix, p0, p1);
            geometry.draw_vertices(list);
        },
        error);
    return ok && stage.finish(out, error);
}

void register_tessellation() {
    const char* partitions[] = {"integer", "fractional_even", "fractional_odd", "pow2"};
    for (const DomainCase& domain : kDomains) {
        for (const char* partition : partitions) {
            const bool line = std::string(domain.output) == "line";
            const std::string name = std::string("tess_") + domain.domain + "_" + partition;
            Cases::single(rr::CaseSpec("stages", name).tolerance(line ? rr::Tolerance::raster(6, 1).with_outliers(0.02) : rr::Tolerance::blend(3)).tag("tessellation"),
                          [domain, partition](Kit& kit, rr::CaptureSet& out, std::string& error) {
                              const float color[4] = {0.9f, 0.55f, 0.2f, 1.0f};
                              const bool is_line = std::string(domain.output) == "line";
                              const float factors[4] = {is_line ? 4.0f : 3.7f, 3.7f, 3.7f, 2.6f};
                              return draw_tess(kit, domain, partition, color, factors, out, error);
                          });
        }
    }
}


void register_tess_variants() {
    struct Variant {
        const char* name;
        const char* domain;
        float color_factors[4];
        const char* partition;
    };
    const Variant variants[] = {{"tess_tri_edges_differ", "tri", {1.0f, 4.0f, 8.0f, 3.0f}, "integer"},
                                {"tess_quad_edges_differ", "quad", {2.0f, 5.0f, 9.0f, 3.0f}, "fractional_odd"},
                                {"tess_tri_factor_one", "tri", {1.0f, 1.0f, 1.0f, 1.0f}, "integer"},
                                {"tess_tri_high_factor", "tri", {16.0f, 16.0f, 16.0f, 12.0f}, "fractional_even"}};
    for (const Variant& variant : variants) {
        Cases::single(rr::CaseSpec("stages", variant.name).tolerance(rr::Tolerance::blend(3)).tag("tessellation"),
                      [variant](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          const DomainCase& domain = std::string(variant.domain) == "tri" ? kDomains[0] : kDomains[1];
                          const float color[4] = {0.9f, 0.55f, 0.2f, 1.0f};
                          return draw_tess(kit, domain, variant.partition, color, variant.color_factors, out, error);
                      });
    }
    for (int quad = 0; quad < 2; ++quad) {
        Cases::single(rr::CaseSpec("stages", quad != 0 ? "tess_quad_sm6" : "tess_tri_sm6").variant(1).tolerance(rr::Tolerance::blend(3)).tag("tessellation").tag("x64-only"),
                      [quad](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          const float color[4] = {0.5f, 0.85f, 0.4f, 1.0f};
                          const float factors[4] = {5.5f, 5.5f, 5.5f, 4.0f};
                          return draw_tess(kit, kDomains[quad], "fractional_odd", color, factors, out, error);
                      });
    }
}

void register_geometry_shader() {
    const char* tri_input =
        "struct In { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct Out { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n";
    struct Variant {
        const char* name;
        std::string source;
    };
    const std::string lit_ps = Hlsl12::kLitPs;
    const Variant variants[] = {
        {"gs_passthrough_triangles",
         std::string(tri_input) +
             "[maxvertexcount(3)] void main(triangle In v[3], inout TriangleStream<Out> s) { for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.nrm = v[k].nrm; o.uv = v[k].uv; s.Append(o); } }\n"},
        {"gs_amplify_triple_copies",
         std::string(tri_input) +
             "[maxvertexcount(9)] void main(triangle In v[3], inout TriangleStream<Out> s) {\n"
             "  for (int c = 0; c < 3; ++c) { for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.pos.x += (c - 1) * 0.35 * o.pos.w; o.pos.xy *= 0.7; o.nrm = v[k].nrm; o.uv = v[k].uv; s.Append(o); } s.RestartStrip(); } }\n"},
        {"gs_instanced_three",
         std::string(tri_input) +
             "[instance(3)][maxvertexcount(3)] void main(triangle In v[3], uint id : SV_GSInstanceID, inout TriangleStream<Out> s) {\n"
             "  for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.pos.y += (id - 1) * 0.4 * o.pos.w; o.pos.xy *= 0.65; o.nrm = v[k].nrm; o.uv = v[k].uv; s.Append(o); } }\n"},
        {"gs_cull_back_facing",
         std::string(tri_input) +
             "[maxvertexcount(3)] void main(triangle In v[3], inout TriangleStream<Out> s) {\n"
             "  float2 a = v[1].pos.xy / v[1].pos.w - v[0].pos.xy / v[0].pos.w; float2 b = v[2].pos.xy / v[2].pos.w - v[0].pos.xy / v[0].pos.w;\n"
             "  if (a.x * b.y - a.y * b.x < 0) { for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.nrm = v[k].nrm; o.uv = v[k].uv; s.Append(o); } } }\n"},
        {"gs_shrink_exploded_faces",
         std::string(tri_input) +
             "[maxvertexcount(3)] void main(triangle In v[3], inout TriangleStream<Out> s) {\n"
             "  float4 c = (v[0].pos + v[1].pos + v[2].pos) / 3.0; for (int k = 0; k < 3; ++k) { Out o; o.pos = lerp(c, v[k].pos, 0.6); o.nrm = v[k].nrm; o.uv = v[k].uv; s.Append(o); } }\n"},
    };
    for (const Variant& variant : variants) {
        Cases::single(rr::CaseSpec("stages", variant.name).tolerance(rr::Tolerance::blend(3)).tag("geometry"),
                      [variant](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                              return false;
                          }
                          Pso builder = stage.pso();
                          builder.stage(variant.source, "main", "gs");
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  stage.mesh(list, stage.sphere, rr::Mat4::rotate_y(0.5f), 0.9f, 0.55f, 0.2f);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
    Cases::single(rr::CaseSpec("stages", "gs_point_sprites").tolerance(rr::Tolerance::blend(3)).tag("geometry"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        std::vector<Vertex> points;
        for (int k = 0; k < 40; ++k) {
            const float a = static_cast<float>(k) * 0.61f;
            const float r = 0.1f + 0.021f * static_cast<float>(k);
            points.push_back({{r * std::cos(a), r * std::sin(a), 0}, {0, 0, 1}, {static_cast<float>(k) / 40.0f, 0}});
        }
        Geometry geometry;
        if (!geometry.create_vertices(kit, points.data(), static_cast<UINT>(points.size()), sizeof(Vertex), error)) {
            return false;
        }
        const char* vs =
            "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct VsOut { float4 pos : SV_Position; float t : TEXCOORD0; };\n"
            "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos, 1); o.t = i.uv.x; return o; }\n";
        const char* gs =
            "struct In { float4 pos : SV_Position; float t : TEXCOORD0; };\n"
            "struct Out { float4 pos : SV_Position; float2 q : TEXCOORD0; float t : TEXCOORD1; };\n"
            "[maxvertexcount(4)] void main(point In v[1], inout TriangleStream<Out> s) {\n"
            "  float h = 0.03 + 0.05 * v[0].t; float2 c[4] = {float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)};\n"
            "  for (int k = 0; k < 4; ++k) { Out o; o.pos = v[0].pos + float4(c[k] * h, 0, 0); o.q = c[k]; o.t = v[0].t; s.Append(o); } }\n";
        const char* ps =
            "struct Out { float4 pos : SV_Position; float2 q : TEXCOORD0; float t : TEXCOORD1; };\n"
            "float4 main(Out i) : SV_Target { float d = saturate(1 - length(i.q)); return float4(lerp(float3(1, 0.3, 0.1), float3(0.2, 0.6, 1), i.t) * d, d); }\n";
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(vs).stage(gs, "main", "gs").ps(ps).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        builder.desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.02f, 0.02f, 0.05f, 1.0f};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                geometry.bind(list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                geometry.draw_vertices(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(rr::CaseSpec("stages", "gs_render_target_array_index").tolerance(rr::Tolerance::blend(3)).uniform_ok("slice0").tag("geometry"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Binder binder;
        Surface array_target;
        Surface depth;
        if (!binder.create(kit, error) || !array_target.color(kit, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, error, 1, 1, 3) ||
            !depth.depth(kit, 64, 64, DXGI_FORMAT_D32_FLOAT, error, 1, 3)) {
            return false;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.ArraySize = 3;
        kit.device()->CreateDepthStencilView(depth.resource, &dsv, depth.view);
        Geometry sphere;
        if (!sphere.create(kit, Shapes::sphere(14, 20, 0.8f), true, error)) {
            return false;
        }
        const char* gs =
            "struct In { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct Out { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint slice : SV_RenderTargetArrayIndex; };\n"
            "[instance(3)][maxvertexcount(3)] void main(triangle In v[3], uint id : SV_GSInstanceID, inout TriangleStream<Out> s) {\n"
            "  for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.pos.x += (id - 1) * 0.5 * o.pos.w; o.nrm = v[k].nrm; o.uv = v[k].uv; o.slice = id; s.Append(o); } }\n";
        const char* ps =
            "struct Out { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint slice : SV_RenderTargetArrayIndex; };\n"
            "float4 main(Out i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
            "  float3 base = i.slice == 0 ? float3(0.9, 0.3, 0.2) : (i.slice == 1 ? float3(0.2, 0.8, 0.3) : float3(0.3, 0.4, 0.95)); return float4(base * l, 1); }\n";
        Pso builder(kit, binder.signature);
        builder.vs(Hlsl12::with_root(Hlsl12::kLitVs)).stage(gs, "main", "gs").ps(ps).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                list->OMSetRenderTargets(1, &array_target.view, FALSE, &depth.view);
                list->ClearRenderTargetView(array_target.view, clear, 0, nullptr);
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                const D3D12_VIEWPORT vp = viewport_of(64, 64);
                const D3D12_RECT scissor = rect_of(64, 64);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                binder.bind_graphics(list);
                list->SetPipelineState(state);
                const float color[4] = {1, 1, 1, 1};
                set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f) * rr::Mat4::rotate_y(0.5f), color);
                sphere.bind(list);
                sphere.draw(list);
            },
            error);
        if (!ok) {
            return false;
        }
        for (UINT slice = 0; slice < 3; ++slice) {
            rr::Image image;
            if (!kit.read_image(array_target.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, slice, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("slice" + std::to_string(slice), std::move(image));
        }
        return true;
    });
    Cases::single(rr::CaseSpec("stages", "gs_viewport_array_index_16").tolerance(rr::Tolerance::blend(3)).tag("geometry"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        const char* gs =
            "struct In { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct Out { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint vp : SV_ViewportArrayIndex; };\n"
            "[instance(16)][maxvertexcount(3)] void main(triangle In v[3], uint id : SV_GSInstanceID, inout TriangleStream<Out> s) {\n"
            "  for (int k = 0; k < 3; ++k) { Out o; o.pos = v[k].pos; o.nrm = v[k].nrm + float3(id * 0.03, 0, 0); o.uv = v[k].uv; o.vp = id; s.Append(o); } }\n";
        const char* ps =
            "struct Out { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint vp : SV_ViewportArrayIndex; };\n"
            "float4 main(Out i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
            "  return float4(float3(0.3 + 0.04 * i.vp, 0.9 - 0.05 * i.vp, 0.4 + 0.03 * (i.vp % 5)) * l, 1); }\n";
        Pso builder = stage.pso();
        builder.stage(gs, "main", "gs").ps(ps);
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                D3D12_VIEWPORT viewports[16];
                D3D12_RECT scissors[16];
                for (int k = 0; k < 16; ++k) {
                    viewports[k] = {static_cast<float>((k % 4) * 32), static_cast<float>((k / 4) * 32), 32, 32, 0, 1};
                    scissors[k] = {(k % 4) * 32, (k / 4) * 32, (k % 4) * 32 + 32, (k / 4) * 32 + 32};
                }
                list->RSSetViewports(16, viewports);
                list->RSSetScissorRects(16, scissors);
                stage.mesh(list, stage.sphere, rr::Mat4::rotate_y(0.5f), 0.9f, 0.55f, 0.2f);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() {
        register_tessellation();
        register_tess_variants();
        register_geometry_shader();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
