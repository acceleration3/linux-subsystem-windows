#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

void mesh_with_raster(Rig& r, rr::CaptureSet& o, const D3D10_RASTERIZER_DESC& rd, bool box, bool depth_test = true, bool face_colors = false) {
    r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    ScenePipe pipe(r, face_colors ? "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, bool ff : SV_IsFrontFace) : SV_Target { float l = saturate(dot(normalize(n), float3(0.4, 0.8, 0.5))) * 0.6 + 0.3; return ff ? float4(l, 0.3 * l, 0.1, 1) : float4(0.1, 0.4 * l, l, 1); }\n" : nullptr);
    MeshBuffers m = upload(r, box ? Geo::box(1.0f, 0.8f, 0.9f) : (depth_test ? Geo::torus(18, 10, 1.0f, 0.45f) : Geo::sphere(10, 16, 1.2f)));
    Com<ID3D10RasterizerState> rs = r.raster(rd);
    auto dd = Rig::depth_desc();
    dd.DepthEnable = depth_test ? TRUE : FALSE;
    Com<ID3D10DepthStencilState> ds = r.depth(dd);
    if (!r.ok()) return;
    r.d->RSSetState(rs.get());
    r.d->OMSetDepthStencilState(ds.get(), 0);
    pipe.bind(r);
    pipe.draw(r, m, camera(0.7f, 0.5f, 4.5f));
    r.cap_main(o);
}


struct LineStats {
    unsigned lit = 0;
    unsigned fractional = 0;
    unsigned row_footprint = 0;
};

LineStats measure(const rr::Image& image) {
    LineStats s;
    for (unsigned y = 0; y < image.height(); ++y)
        for (unsigned x = 0; x < image.width(); ++x) {
            const double v = (image.channel(x, y, 0) + image.channel(x, y, 1) + image.channel(x, y, 2)) / 3.0;
            if (v > 8.0) ++s.lit;
            if (v > 25.0 && v < 230.0) ++s.fractional;
            if (y == image.height() / 2 && v > 8.0) ++s.row_footprint;
        }
    return s;
}

void draw_lines(Rig& r, bool antialiased, bool multisample_flag, float tilt = 0.0f) {
    auto rd = Rig::raster_desc();
    rd.AntialiasedLineEnable = antialiased ? TRUE : FALSE;
    rd.MultisampleEnable = multisample_flag ? TRUE : FALSE;
    Com<ID3D10RasterizerState> rs = r.raster(rd);
    auto bd = Rig::blend_desc();
    bd.BlendEnable[0] = TRUE;
    bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    bd.SrcBlendAlpha = D3D10_BLEND_ONE;
    bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    Com<ID3D10BlendState> bs = r.blend(bd);
    if (!r.ok()) return;
    r.d->RSSetState(rs.get());
    const float f[4] = {1, 1, 1, 1};
    r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
    std::vector<Vtx> v;
    for (unsigned i = 0; i < 14; ++i) {
        const float a = 0.2f + tilt + 3.1f * static_cast<float>(i) / 14.0f;
        v.push_back(vtx(0.0f, -0.9f, 0.5f, palette(7)));
        v.push_back(vtx(0.95f * std::cos(a), -0.9f + 1.8f * std::sin(a), 0.5f, palette(7)));
    }
    v.push_back(vtx(-0.7f, -0.8f, 0.5f, palette(7)));
    v.push_back(vtx(-0.7f, 0.8f, 0.5f, palette(7)));
    r.draw_std(v, D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
}

void line_scene(Rig& r, rr::CaptureSet& o, int mode) {
    const UINT samples = mode == 2 ? 4 : 1;
    Target a, b;
    r.target(a, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, samples);
    if (mode > 0) r.target(b, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, samples);
    if (!r.ok()) return;
    r.use_std(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
    r.viewport(128, 128);
    r.bind_rt(a);
    r.clear(a, 0, 0, 0, 1);
    draw_lines(r, false, false, mode == 2 ? 0.07f : 0.0f);
    rr::Image first;
    r.read(a, first);
    if (mode == 0) {
        o.add("", first);
        return;
    }
    r.bind_rt(b);
    r.clear(b, 0, 0, 0, 1);
    draw_lines(r, mode == 1 && !r.mutate("rs_aa"), mode == 2, mode == 2 ? 0.07f : 0.0f);
    rr::Image second;
    r.read(b, second);
    if (!r.ok()) return;
    o.add(mode == 1 ? "aliased" : "ms_off", first);
    o.add("", second);
    const LineStats base = measure(first);
    const LineStats test = measure(second);
    if (test.fractional < base.fractional + 20) {
        r.fail("the second image has " + std::to_string(test.fractional) + " fractional-coverage pixels against " + std::to_string(base.fractional) + " in the aliased image");
    } else if (mode == 1 && test.row_footprint < 2) {
        r.fail("the antialiased vertical line is " + std::to_string(test.row_footprint) + " pixel wide");
    } else if (mode == 1 && test.lit <= base.lit) {
        r.fail("the antialiased lines do not cover more pixels than the aliased lines");
    }
}

const std::vector<Vtx>& two_quads() {
    static const std::vector<Vtx> v = [] {
        std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.9f, 0.3f, 0.2f);
        std::vector<Vtx> b = Geo::quad(-0.5f, -0.5f, 0.9f, 0.5f, 0.5f, 0.2f, 0.4f, 0.95f);
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }();
    return v;
}

void bias_scene(Rig& r, rr::CaptureSet& o, DXGI_FORMAT format, int bias, float slope, float clamp, bool tilt, D3D10_COMPARISON_FUNC func, unsigned variant) {
    if (r.mutate("rs_bias")) {
        bias = 0;
        slope = 0;
    }
    r.target(r.main, 128, 128);
    r.depth_target(r.depth_main, 128, 128, format);
    if (!r.ok()) return;
    r.bind_rt(r.main, &r.depth_main);
    r.clear(r.main, 0.05f, 0.05f, 0.1f, 1);
    r.clear_depth(r.depth_main);
    r.viewport(128, 128);
    r.use_std();
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    dd.DepthFunc = func;
    Com<ID3D10DepthStencilState> ds = r.depth(dd);
    auto base = Rig::raster_desc();
    Com<ID3D10RasterizerState> flat = r.raster(base);
    base.DepthBias = bias;
    base.SlopeScaledDepthBias = slope;
    base.DepthBiasClamp = clamp;
    Com<ID3D10RasterizerState> biased = r.raster(base);
    if (!r.ok()) return;
    r.d->OMSetDepthStencilState(ds.get(), 0);
    std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.9f, 0.3f, 0.2f);
    std::vector<Vtx> b = Geo::quad(-0.7f + 0.08f * static_cast<float>(variant), -0.7f, 0.7f, 0.7f - 0.07f * static_cast<float>(variant), 0.5f, palette(variant + 2)[0], palette(variant + 2)[1], palette(variant + 2)[2]);
    if (tilt) {
        for (auto& v : b) v.z = 0.5f + 0.12f * v.x;
        for (auto& v : a) v.z = 0.5f + 0.12f * v.x;
    }
    r.d->RSSetState(flat.get());
    r.draw_std(a);
    r.d->RSSetState(biased.get());
    r.draw_std(b);
    r.cap_main(o);
    r.cap_depth(o, "depth", r.depth_main);
}

void viewport_depth(Rig& r, rr::CaptureSet& o, bool subrect) {
    r.target(r.main, 128, 128);
    r.depth_target(r.depth_main, 128, 128, DXGI_FORMAT_D32_FLOAT);
    if (!r.ok()) return;
    r.bind_rt(r.main, &r.depth_main);
    r.clear(r.main, 0.05f, 0.05f, 0.1f, 1);
    r.clear_depth(r.depth_main);
    r.use_std();
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    Com<ID3D10DepthStencilState> ds = r.depth(dd);
    r.no_cull();
    r.d->OMSetDepthStencilState(ds.get(), 0);
    if (subrect) {
        r.viewport(100, 70, 14, 30, 0.0f, 1.0f);
    } else {
        r.viewport(128, 128, 0, 0, 0.25f, 0.75f);
    }
    std::vector<Vtx> a = Geo::quad(-1, -1, 1, 1, 0.8f, 0.9f, 0.3f, 0.2f);
    std::vector<Vtx> b = Geo::quad(-0.8f, -0.8f, 0.8f, 0.8f, 0.2f, 0.2f, 0.5f, 0.95f);
    for (auto& v : b) v.z = 0.1f + 0.4f * (v.x + 0.8f);
    r.draw_std(a);
    r.draw_std(b);
    r.cap_main(o);
    r.cap_depth(o, "depth", r.depth_main);
}

void scissor_scene(Rig& r, rr::CaptureSet& o, int mode) {
    r.begin();
    r.use_std();
    auto rd = Rig::raster_desc();
    rd.ScissorEnable = TRUE;
    Com<ID3D10RasterizerState> rs = r.raster(rd);
    if (!r.ok()) return;
    r.d->RSSetState(rs.get());
    std::vector<Vtx> full = Geo::quad(-1, -1, 1, 1, 0.5f, 0.9f, 0.3f, 0.2f);
    for (unsigned i = 0; i < full.size(); ++i) {
        full[i].g = 0.3f + 0.6f * (full[i].x * 0.5f + 0.5f);
        full[i].b = 0.2f + 0.7f * (full[i].y * 0.5f + 0.5f);
    }
    Com<ID3D10Buffer> vb = r.vbuf(full);
    r.std_vb(vb.get());
    if (mode == 0) {
        D3D10_RECT rect = {20, 30, 100, 90};
        r.d->RSSetScissorRects(1, &rect);
        r.d->Draw(6, 0);
    } else if (mode == 1) {
        for (int i = 0; i < 4; ++i) {
            D3D10_RECT rect = {i * 30, i * 25, i * 30 + 40, i * 25 + 40};
            r.d->RSSetScissorRects(1, &rect);
            std::vector<Vtx> q = Geo::quad(-1, -1, 1, 1, 0.5f, palette(i)[0], palette(i)[1], palette(i)[2]);
            r.draw_std(q);
        }
    } else {
        r.clear(r.main, 0.4f, 0.4f, 0.4f, 1);
        D3D10_RECT rect = {0, 0, 60, 128};
        r.d->RSSetScissorRects(1, &rect);
        r.d->Draw(6, 0);
        D3D10_RECT empty = {64, 64, 64, 100};
        r.d->RSSetScissorRects(1, &empty);
        std::vector<Vtx> q = Geo::quad(-1, -1, 1, 1, 0.5f, 1, 1, 1);
        r.draw_std(q);
    }
    r.cap_main(o);
}

void fill_rule_scene(Rig& r, rr::CaptureSet& o, int mode) {
    r.begin();
    r.use_std();
    auto bd = Rig::blend_desc();
    bd.BlendEnable[0] = TRUE;
    bd.SrcBlend = D3D10_BLEND_ONE;
    bd.DestBlend = D3D10_BLEND_ONE;
    bd.SrcBlendAlpha = D3D10_BLEND_ONE;
    bd.DestBlendAlpha = D3D10_BLEND_ONE;
    Com<ID3D10BlendState> bs = r.blend(bd);
    if (!r.ok()) return;
    const float zero[4] = {};
    r.clear(r.main, 0, 0, 0, 0);
    const float factor[4] = {1, 1, 1, 1};
    r.d->OMSetBlendState(bs.get(), factor, 0xffffffffu);
    auto px = [](float x) { return x / 64.0f - 1.0f; };
    auto py = [](float y) { return 1.0f - y / 64.0f; };
    std::vector<Vtx> v;
    const float c = 0.25f;
    if (mode == 0) {
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i) {
                const float x0 = 8.5f + 14.0f * i, y0 = 8.5f + 14.0f * j;
                std::vector<Vtx> q = Geo::quad(px(x0), py(y0 + 14.0f), px(x0 + 14.0f), py(y0), 0.5f, c, c, c, c);
                v.insert(v.end(), q.begin(), q.end());
            }
    } else if (mode == 1) {
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i) {
                const float x0 = 7.0f + 14.0f * i, y0 = 7.0f + 14.0f * j;
                v.push_back(vtx(px(x0), py(y0), 0.5f, palette(0)));
                v.push_back(vtx(px(x0 + 14), py(y0), 0.5f, palette(0)));
                v.push_back(vtx(px(x0 + 14), py(y0 + 14), 0.5f, palette(0)));
                v.push_back(vtx(px(x0), py(y0), 0.5f, palette(0)));
                v.push_back(vtx(px(x0 + 14), py(y0 + 14), 0.5f, palette(0)));
                v.push_back(vtx(px(x0), py(y0 + 14), 0.5f, palette(0)));
                for (int k = 0; k < 6; ++k) {
                    Vtx& e = v[v.size() - 6 + k];
                    e.r = e.g = e.b = e.a = c;
                }
            }
    } else {
        const int n = 8;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const float x0 = 5.5f + 13.0f * i, y0 = 5.5f + 13.0f * j;
                const float xs[3] = {x0, x0 + 13.0f, x0};
                const float ys[3] = {y0, y0 + 13.0f, y0 + 13.0f};
                const float x1[3] = {x0, x0 + 13.0f, x0 + 13.0f};
                const float y1[3] = {y0, y0, y0 + 13.0f};
                for (int k = 0; k < 3; ++k) v.push_back(vtx(px(xs[k]), py(ys[k]), 0.5f, palette(0)));
                for (int k = 0; k < 3; ++k) v.push_back(vtx(px(x1[k]), py(y1[k]), 0.5f, palette(0)));
                for (int k = 0; k < 6; ++k) {
                    Vtx& e = v[v.size() - 6 + k];
                    e.r = e.g = e.b = e.a = c;
                }
            }
    }
    r.draw_std(v);
    std::vector<Vtx> marker = Geo::quad(-1, -1, -0.8f, -0.8f, 0.5f, 128.0f / 255.0f, 0.0f, 0.0f, 128.0f / 255.0f);
    r.draw_std(marker);
    (void)zero;
    r.cap_main(o);
}

const Registrar kRs{[] {
    for (int box = 0; box < 2; ++box) {
        for (int wire = 0; wire < 2; ++wire) {
            if (box == 1 && wire == 0) continue;
            single(spec("rs", std::string("fill_") + (wire ? "wireframe" : "solid") + (box ? "_box" : "_torus")).tolerance(wire ? lines_tol() : rr::Tolerance::blend()),
                   [box, wire](Rig& r, rr::CaptureSet& o) {
                       auto rd = Rig::raster_desc();
                       rd.FillMode = wire ? D3D10_FILL_WIREFRAME : D3D10_FILL_SOLID;
                       mesh_with_raster(r, o, rd, box != 0);
                   });
        }
    }
    single(spec("rs", "cull_none_without_depth").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        auto rd = Rig::raster_desc();
        mesh_with_raster(r, o, rd, false, false);
    });
    single(spec("rs", "cull_back_clockwise_front_without_depth").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        auto rd = Rig::raster_desc();
        rd.CullMode = r.mutate("rs_cull") ? D3D10_CULL_NONE : D3D10_CULL_BACK;
        mesh_with_raster(r, o, rd, false, false);
    });
    single(spec("rs", "cull_front_clockwise_front_without_depth").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        auto rd = Rig::raster_desc();
        rd.CullMode = D3D10_CULL_FRONT;
        mesh_with_raster(r, o, rd, false, false);
    });
    single(spec("rs", "front_counter_clockwise_flag_colours_faces").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        auto rd = Rig::raster_desc();
        rd.FrontCounterClockwise = r.mutate("rs_ccw") ? FALSE : TRUE;
        mesh_with_raster(r, o, rd, false, false, true);
    });
    struct BiasCase {
        const char* name;
        DXGI_FORMAT format;
        int bias;
        float slope;
        float clamp;
        bool tilt;
        D3D10_COMPARISON_FUNC func;
        float depth_tol;
    };
    const BiasCase biases[] = {
        {"bias_d16_negative", DXGI_FORMAT_D16_UNORM, -40, 0, 0, false, D3D10_COMPARISON_LESS, 2e-5f},
        {"bias_d24_negative", DXGI_FORMAT_D24_UNORM_S8_UINT, -4000, 0, 0, false, D3D10_COMPARISON_LESS, 1.5e-7f},
        {"bias_d32f_negative", DXGI_FORMAT_D32_FLOAT, -60000, 0, 0, false, D3D10_COMPARISON_LESS, 1e-6f},
        {"bias_d32f_positive_hides", DXGI_FORMAT_D32_FLOAT, 60000, 0, 0, false, D3D10_COMPARISON_LESS_EQUAL, 1e-6f},
        {"bias_slope_scaled", DXGI_FORMAT_D32_FLOAT, 0, -4.0f, 0, true, D3D10_COMPARISON_LESS, 1e-4f},
        {"bias_clamped", DXGI_FORMAT_D24_UNORM_S8_UINT, -400000, 0, -0.0005f, false, D3D10_COMPARISON_LESS, 2e-6f},
    };
    unsigned bias_index = 0;
    for (const BiasCase& b : biases) {
        const unsigned bias_variant = bias_index++;
        single(spec("rs", b.name).tolerance(rr::Tolerance::raster(2, 1)).tolerance_for("depth", rr::Tolerance::floats(b.depth_tol, 0.0f).with_outliers(2e-4)),
               [b, bias_variant](Rig& r, rr::CaptureSet& o) { bias_scene(r, o, b.format, b.bias, b.slope, b.clamp, b.tilt, b.func, bias_variant); });
    }
    for (int clip = 0; clip < 2; ++clip) {
        single(spec("rs", clip ? "depth_clip_enabled" : "depth_clip_disabled").tolerance(rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(2e-4f, 2e-4f)),
               [clip](Rig& r, rr::CaptureSet& o) {
                   r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
                   r.use_std();
                   auto rd = Rig::raster_desc();
                   rd.DepthClipEnable = clip ? TRUE : FALSE;
                   Com<ID3D10RasterizerState> rs = r.raster(rd);
                   auto dd = Rig::depth_desc();
                   dd.DepthEnable = TRUE;
                   Com<ID3D10DepthStencilState> ds = r.depth(dd);
                   if (!r.ok()) return;
                   r.d->RSSetState(rs.get());
                   r.d->OMSetDepthStencilState(ds.get(), 0);
                   std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.8f, 0.4f, 0.2f);
                   std::vector<Vtx> b = Geo::quad(-0.8f, -0.8f, 0.8f, 0.8f, 0.5f, 0.2f, 0.6f, 0.9f);
                   for (auto& v : b) v.z = -0.3f + 1.6f * (v.x + 0.8f) / 1.6f;
                   r.draw_std(a);
                   r.draw_std(b);
                   r.cap_main(o);
                   r.cap_depth(o, "depth", r.depth_main);
               });
    }
    single(spec("rs", "scissor_single_rect").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { scissor_scene(r, o, 0); });
    single(spec("rs", "scissor_changes_between_draws").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { scissor_scene(r, o, 1); });
    single(spec("rs", "scissor_clipped_and_empty").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { scissor_scene(r, o, 2); });
    single(spec("rs", "viewport_subrect").tolerance(rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(2e-4f, 2e-4f)), [](Rig& r, rr::CaptureSet& o) { viewport_depth(r, o, true); });
    single(spec("rs", "viewport_depth_range").tolerance(rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(2e-4f, 2e-4f)), [](Rig& r, rr::CaptureSet& o) { viewport_depth(r, o, false); });
    single(spec("rs", "sixteen_viewports_gs_index").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
        r.no_cull();
        Com<ID3D10GeometryShader> gs = r.gs(
            "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
            "struct GO { float4 p : SV_Position; float4 c : COLOR; uint vp : SV_ViewportArrayIndex; };\n"
            "[maxvertexcount(48)] void main(triangle VO v[3], inout TriangleStream<GO> s) { GO o;\n"
            "  [unroll] for (uint k = 0; k < 16; ++k) { [unroll] for (int i = 0; i < 3; ++i) { o.p = v[i].p; o.c = float4(frac(v[i].c.rgb * (1 + k * 0.17) + k * 0.05), 1); o.vp = k; s.Append(o); } s.RestartStrip(); } }\n");
        if (!r.ok()) return;
        D3D10_VIEWPORT vps[16];
        for (int i = 0; i < 16; ++i) vps[i] = {(i % 4) * 32, (i / 4) * 32, 32, 32, 0.0f, 1.0f};
        r.d->RSSetViewports(16, vps);
        r.d->GSSetShader(gs.get());
        std::vector<Vtx> v = Geo::triangle(0, 0.1f, 0.9f, 0.5f, 1, 0.5f, 0.2f);
        v[1].g = 1;
        v[2].b = 1;
        r.draw_std(v);
        r.cap_main(o);
    });
    single(spec("rs", "antialiased_lines_off").tolerance(lines_tol()), [](Rig& r, rr::CaptureSet& o) { line_scene(r, o, 0); });
    single(spec("rs", "antialiased_lines_on_widen_and_fade").tolerance(rr::Tolerance::psnr(12.0)).tolerance_for("aliased", lines_tol()), [](Rig& r, rr::CaptureSet& o) { line_scene(r, o, 1); });
    single(spec("rs", "multisample_enable_lines_on_msaa_target").tolerance(rr::Tolerance::psnr(14.0)).tolerance_for("ms_off", rr::Tolerance::psnr(18.0)), [](Rig& r, rr::CaptureSet& o) { line_scene(r, o, 2); });
    single(spec("rs", "fill_rule_shared_quads_additive").tolerance(rr::Tolerance::exact()), [](Rig& r, rr::CaptureSet& o) { fill_rule_scene(r, o, 0); });
    single(spec("rs", "fill_rule_shared_diagonal_additive").tolerance(rr::Tolerance::exact()), [](Rig& r, rr::CaptureSet& o) { fill_rule_scene(r, o, 1); });
    single(spec("rs", "fill_rule_pixel_center_edges_additive").tolerance(rr::Tolerance::exact()), [](Rig& r, rr::CaptureSet& o) { fill_rule_scene(r, o, 2); });
    single(spec("rs", "guard_band_huge_triangle").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        r.no_cull();
        std::vector<Vtx> v = {vtx(-30000, -20000, 0.5f, palette(0)), vtx(26000, 30000, 0.5f, palette(2)), vtx(-4000, 41000, 0.5f, palette(1))};
        std::vector<Vtx> w = {vtx(-0.5f, -0.5f, 0.5f, palette(3)), vtx(100.0f, 0.2f, 0.5f, palette(4)), vtx(-0.2f, 0.9f, 0.5f, palette(5))};
        r.draw_std(w);
        r.draw_std(v);
        r.cap_main(o);
    });
    single(spec("rs", "guard_band_vertex_behind_eye").tolerance(rr::Tolerance::raster(3, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ScenePipe pipe(r);
        MeshBuffers m = upload(r, Geo::grid(8, 6.0f, Geo::height_field(8, 3)));
        r.no_cull();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        pipe.bind(r);
        pipe.draw(r, m, rr::Mat4::perspective_d3d(1.2f, 1.0f, 0.1f, 40.0f) * rr::Mat4::translate(0, -0.8f, -1.5f));
        r.cap_main(o);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
