#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

void tilted_plane(Rig& r, rr::CaptureSet& o, const std::string& ps_body, bool depth_capture = false) {
    r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    ScenePipe pipe(r, ps_body.c_str());
    std::vector<float> flat(4, 0.0f);
    MeshBuffers m = upload(r, Geo::grid(1, 3.0f, flat));
    r.no_cull();
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    Com<ID3D10DepthStencilState> ds = r.depth(dd);
    if (!r.ok()) return;
    r.d->OMSetDepthStencilState(ds.get(), 0);
    pipe.bind(r);
    pipe.draw(r, m, rr::Mat4::perspective_d3d(1.9f, 1.0f, 0.2f, 40.0f) * rr::Mat4::translate(0, -0.6f, -1.6f) * rr::Mat4::rotate_x(-1.35f));
    r.cap_main(o);
    if (depth_capture) r.cap_depth(o, "depth", r.depth_main);
}

const char* kChecker =
    "float chk(float2 uv) { return fmod(floor(uv.x * 8) + floor(uv.y * 8), 2); }\n";

std::string interp_ps(const char* modifier) {
    return std::string(kChecker) + "float4 main(float4 p : SV_Position, float3 n : NORMAL, " + modifier +
           " float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target { return float4(uv.x, uv.y, chk(uv) * 0.7 + 0.15, 1); }\n";
}

const Registrar kPs{[] {
    for (const char* m : {"linear", "noperspective", "nointerpolation"}) {
        const std::string modifier = m;
        single(spec("ps", "interpolation_" + modifier).tolerance(rr::Tolerance::raster(8, 1)), [modifier](Rig& r, rr::CaptureSet& o) { tilted_plane(r, o, interp_ps(modifier.c_str())); });
    }
    single(spec("ps", "sv_is_front_face").tolerance(rr::Tolerance::raster(4, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ScenePipe pipe(r, "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, bool ff : SV_IsFrontFace) : SV_Target {\n"
                          "  float l = saturate(dot(normalize(n), float3(0.3, 0.8, 0.5))) * 0.6 + 0.3; return ff ? float4(l, l * 0.5, 0.1, 1) : float4(0.1, l * 0.5, l, 1); }\n");
        MeshBuffers m = upload(r, Geo::torus(18, 10, 1.0f, 0.4f));
        r.no_cull();
        Com<ID3D10DepthStencilState> ds = r.depth([] { auto d = Rig::depth_desc(); d.DepthEnable = FALSE; return d; }());
        if (!r.ok()) return;
        pipe.bind(r);
        pipe.draw(r, m, camera(0.3f, 0.9f, 4.5f));
        r.cap_main(o);
    });
    single(spec("ps", "sv_primitive_id").tolerance(rr::Tolerance::raster(4, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ScenePipe pipe(r, "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, uint id : SV_PrimitiveID) : SV_Target {\n"
                          "  uint h = id * 2654435761u + 5u; return float4(float((h >> 8) & 255u) / 255.0, float((h >> 16) & 255u) / 255.0, float((h >> 24) & 255u) / 255.0, 1); }\n");
        MeshBuffers m = upload(r, Geo::sphere(8, 12, 1.2f));
        r.no_cull();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        pipe.bind(r);
        pipe.draw(r, m, camera(0.5f, 0.4f, 4.0f));
        r.cap_main(o);
    });
    single(spec("ps", "sv_depth_output").tolerance(rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(5e-4f, 5e-4f)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        r.use_std();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        Com<ID3D10PixelShader> warp = r.ps("struct PO { float4 c : SV_Target; float d : SV_Depth; };\n"
                                          "PO main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO o; o.c = float4(uv, 0.5 + 0.5 * sin(uv.x * 9), 1); o.d = 0.2 + 0.6 * uv.x * uv.y; return o; }\n");
        Com<ID3D10PixelShader> flat = r.ps(Std::kPsUv);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        r.d->PSSetShader(flat.get());
        r.draw_std(Geo::quad(-1, -1, 1, 0.0f, 0.5f, 0.9f, 0.9f, 0.2f));
        r.d->PSSetShader(warp.get());
        r.draw_std(Geo::quad_uv(-0.8f, -0.8f, 0.8f, 0.8f, 0.9f, 0, 0, 1, 1));
        r.cap_main(o);
        r.cap_depth(o, "depth", r.depth_main);
    });
    single(spec("ps", "discard_alpha_test").tolerance(rr::Tolerance::raster(4, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> tex = r.rgba_tex(32, 32, rr::Pattern::noise(32, 32, 99), true);
        Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, "Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  float4 v = t.Sample(s, uv * 2); if (v.g < 0.5) discard; clip(v.r - 0.2); return float4(v.rgb, 1); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("ps", "derivatives_ddx_ddy").tolerance(rr::Tolerance::blend(6)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        ScenePipe pipe(r, "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target {\n"
                          "  float3 dx = ddx(w) * 14, dy = ddy(w) * 14; return float4(abs(dx.x) + 0.15, abs(dy.y) + 0.15, abs(dx.z) + abs(dy.z), 1); }\n");
        MeshBuffers m = upload(r, Geo::sphere(16, 24, 1.3f));
        r.no_cull();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        pipe.bind(r);
        pipe.draw(r, m, camera(0.5f, 0.4f, 4.2f));
        r.cap_main(o);
    });
    single(spec("ps", "derivatives_fwidth_and_branches").tolerance(rr::Tolerance::blend(4)), [](Rig& r, rr::CaptureSet& o) {
        tilted_plane(r, o, "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target {\n"
                           "  float f = fwidth(uv.x * 8) * 20; float4 c = float4(f, f * 0.5, 1 - f, 1); if (uv.x > 0.5) c = c.bgra; return c; }\n");
    });
    single(spec("ps", "integer_bit_operations").tolerance(rr::Tolerance::exact()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        paint(r, o, "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"
                    "  uint x = uint(p.x), y = uint(p.y); uint h = (x * 73856093u) ^ (y * 19349663u); h = (h << 3) | (h >> 29); h ^= h >> 13; h = h * 1597334677u + (x & y) - (x | y);\n"
                    "  int s = int(h) >> 7; uint q = (h % 251u) + (h / 977u) % 5u; uint hh = f32tof16(float(q) * 0.01); float back = f16tof32(hh);\n"
                    "  return float4((h & 255u) / 255.0, (q & 255u) / 255.0, float((s >> 3) & 255) / 255.0, 1.0 + 0.0 * back); }\n",
              {}, {}, full_uv());
    });
    single(spec("ps", "math_transcendental").tolerance(rr::Tolerance::blend(5)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        paint(r, o, PS_HEAD "  float x = uv.x * 3 + 0.1, y = uv.y * 2 + 0.2; return float4(0.5 + 0.5 * sin(x * 4) * cos(y * 5), saturate(pow(x * 0.5, 1.5) * 0.4 + log2(y + 1) * 0.3), saturate(exp2(-x * 0.7) + sqrt(y) * 0.2 - rsqrt(x + 1) * 0.1), 1); }\n",
              {}, {}, full_uv());
    });
    single(spec("ps", "math_rounding_and_selection").tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        paint(r, o, PS_HEAD "  float x = uv.x * 7.3 - 2, y = uv.y * 5.1 - 1; float a = frac(x) * step(0.5, frac(y)) + floor(y) * 0.1 + ceil(x) * 0.05 + round(x * 0.5) * 0.1;\n"
                    "  float b = smoothstep(0.2, 0.9, frac(x * y)) + sign(x) * 0.1 + clamp(y, 0, 2) * 0.2 + lerp(0.2, 0.7, saturate(x)) * 0.3 + fmod(abs(x), 1.7) * 0.1 + max(min(x, y), -1) * 0.05;\n"
                    "  return float4(saturate(a), saturate(b), saturate(abs(x - y) * 0.3), 1); }\n",
              {}, {}, full_uv());
    });
    single(spec("ps", "loops_and_dynamic_branches").tolerance(rr::Tolerance::blend(4)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        paint(r, o, PS_HEAD "  float2 z = (uv - 0.5) * 2.2; float2 k = float2(-0.4, 0.58); int i = 0; [loop] for (; i < 12; ++i) { float2 n = float2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + k; if (dot(n, n) > 4) break; z = n; }\n"
                    "  float t = i / 12.0; float acc = 0; [unroll] for (int j = 0; j < 4; ++j) acc += sin(t * (j + 1) * 3); return float4(t, saturate(0.5 + 0.25 * acc), 1 - t, 1); }\n",
              {}, {}, full_uv());
    });
    single(spec("ps", "sixteen_interpolators").tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        std::string decl = "struct VO { float4 p : SV_Position;";
        std::string vs_body, ps_body = "float4 acc = 0;";
        for (int i = 0; i < 15; ++i) {
            decl += " float4 t" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) + ";";
            vs_body += " o.t" + std::to_string(i) + " = float4(i.uv * " + std::to_string(i + 1) + ", " + std::to_string(i % 3) + ".0, 1) * 0.1;";
            ps_body += " acc += i.t" + std::to_string(i) + " * " + std::to_string(0.1f * static_cast<float>(i % 5 + 1)) + ";";
        }
        decl += " };\n";
        Com<ID3D10VertexShader> vs = r.vs(decl + "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\nVO main(VI i) { VO o; o.p = float4(i.p, 1);" + vs_body + " return o; }\n");
        Com<ID3D10InputLayout> layout = r.std_layout();
        Com<ID3D10PixelShader> ps = r.ps(decl + "float4 main(VO i) : SV_Target { " + ps_body + " return float4(saturate(acc.rgb * 0.3), 1); }\n");
        if (!r.ok()) return;
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.draw_std(full_uv());
        r.cap_main(o);
    });
    single(spec("ps", "eight_render_targets").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        Target t[8];
        for (auto& x : t) r.target(x, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.use_std();
        std::string body = "struct PO {";
        for (int i = 0; i < 8; ++i) body += " float4 c" + std::to_string(i) + " : SV_Target" + std::to_string(i) + ";";
        body += " };\nPO main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO o;";
        for (int i = 0; i < 8; ++i) body += " o.c" + std::to_string(i) + " = float4(frac(uv.x * " + std::to_string(i + 1) + "), uv.y * " + std::to_string(0.1f * static_cast<float>(i + 1)) + ", " + std::to_string(i / 7.0f) + ", 1);";
        body += " return o; }\n";
        Com<ID3D10PixelShader> ps = r.ps(body);
        if (!r.ok()) return;
        ID3D10RenderTargetView* views[8];
        for (int i = 0; i < 8; ++i) views[i] = t[i].view.get();
        r.d->OMSetRenderTargets(8, views, nullptr);
        r.viewport(64, 64);
        r.no_cull();
        r.d->PSSetShader(ps.get());
        r.draw_std(full_uv());
        for (int i = 0; i < 8; ++i) r.cap(o, "target" + std::to_string(i), t[i]);
    });
    single(spec("ps", "fourteen_constant_buffers").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::string src;
        std::string sum = "float4 acc = 0;";
        std::vector<Com<ID3D10Buffer>> bufs;
        ID3D10Buffer* raw[14];
        for (int i = 0; i < 14; ++i) {
            const float v[4] = {0.02f * (i + 1), 0.015f * (14 - i), 0.01f * ((i * 5) % 7), 0.0f};
            bufs.push_back(r.cbuf(16, v));
            raw[i] = bufs.back().get();
            src += "cbuffer B" + std::to_string(i) + " : register(b" + std::to_string(i) + ") { float4 v" + std::to_string(i) + "; };\n";
            sum += " acc += v" + std::to_string(i) + " * (1 + uv.x * " + std::to_string(i % 3) + ");";
        }
        Com<ID3D10PixelShader> ps = r.ps(src + PS_HEAD + sum + " return float4(acc.rgb * (0.5 + 0.5 * uv.y), 1); }\n");
        if (!r.ok()) return;
        r.d->PSSetConstantBuffers(0, 14, raw);
        r.d->PSSetShader(ps.get());
        r.draw_std(full_uv());
        r.cap_main(o);
    });
    single(spec("ps", "constant_buffer_dynamic_array_index").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        float table[64][4];
        for (int i = 0; i < 64; ++i) {
            table[i][0] = (i % 8) / 7.0f;
            table[i][1] = (i / 8) / 7.0f;
            table[i][2] = ((i * 3) % 11) / 10.0f;
            table[i][3] = 1;
        }
        Com<ID3D10Buffer> cb = r.cbuf(sizeof table, table);
        Com<ID3D10PixelShader> ps = r.ps("cbuffer T : register(b0) { float4 table[64]; };\n" PS_HEAD "  int2 q = int2(uv * 8); int k = q.y * 8 + q.x; int j = (k * 5 + 3) % 64; return lerp(table[k], table[j], frac(uv.x * 8)); }\n");
        if (!r.ok()) return;
        r.ps_cb(0, cb.get());
        r.d->PSSetShader(ps.get());
        r.draw_std(full_uv());
        r.cap_main(o);
    });
    single(spec("ps", "twenty_four_shader_resources").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<Com<ID3D10Texture2D>> textures;
        std::vector<Com<ID3D10ShaderResourceView>> views;
        ID3D10ShaderResourceView* raw[24];
        std::string src, sum = "float4 acc = 0; int idx = int(uv.x * 6) + 6 * int(uv.y * 4);";
        for (int i = 0; i < 24; ++i) {
            rr::Pattern::Bytes px(4 * 4, 0);
            for (int k = 0; k < 4; ++k) {
                px[k * 4] = static_cast<std::uint8_t>(i * 10);
                px[k * 4 + 1] = static_cast<std::uint8_t>(255 - i * 9);
                px[k * 4 + 2] = static_cast<std::uint8_t>(k * 60);
                px[k * 4 + 3] = 255;
            }
            textures.push_back(r.rgba_tex(2, 2, px));
            views.push_back(r.srv(textures.back().get()));
            raw[i] = views.back().get();
            src += "Texture2D t" + std::to_string(i) + " : register(t" + std::to_string(i) + ");\n";
            sum += " if (idx == " + std::to_string(i) + ") acc = t" + std::to_string(i) + ".Load(int3(int2(frac(uv * float2(6, 4)) * 2), 0));";
        }
        Com<ID3D10PixelShader> ps = r.ps(src + PS_HEAD + sum + " return acc; }\n");
        if (!r.ok()) return;
        r.d->PSSetShaderResources(0, 24, raw);
        r.d->PSSetShader(ps.get());
        r.draw_std(full_uv());
        r.cap_main(o);
    });
    for (int per_sample = 0; per_sample < 2; ++per_sample) {
        single(spec("ps", per_sample ? "interpolation_sample_modifier_msaa" : "interpolation_linear_extrapolates_at_edges_msaa", 1).tolerance(rr::Tolerance::raster(10, 1)), [per_sample](Rig& r, rr::CaptureSet& o) {
            D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 1, 4);
            d.SampleDesc.Quality = 0xffffffffu;
            r.check(r.d->CreateTexture2D(&d, nullptr, r.main.texture.put()), "standard pattern target");
            if (!r.ok()) return;
            r.main.view = r.rtv(r.main.texture.get());
            d.SampleDesc.Count = 1;
            d.SampleDesc.Quality = 0;
            r.check(r.d->CreateTexture2D(&d, nullptr, r.main.resolved.put()), "resolve target");
            r.main.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            r.main.width = r.main.height = 128;
            r.main.samples = 4;
            if (!r.ok()) return;
            r.bind_rt(r.main);
            r.clear(r.main, 0.1f, 0.1f, 0.15f, 1);
            r.viewport(128, 128);
            r.use_std();
            r.no_cull();
            const std::string mod = per_sample ? "sample " : "";
            Com<ID3D10PixelShader> ps = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, " + mod + "float2 uv : TEXCOORD0) : SV_Target { float edge = (uv.x < 0 || uv.y < 0 || uv.x + uv.y > 1) ? 1 : 0; return float4(edge, uv.x, uv.y, 1); }\n");
            if (!r.ok()) return;
            r.d->PSSetShader(ps.get());
            std::vector<Vtx> t = {vtx(-0.8f, 0.7f, 0.5f, palette(0), 0, 0), vtx(0.7f, 0.65f, 0.5f, palette(0), 1, 0), vtx(-0.75f, -0.8f, 0.5f, palette(0), 0, 1)};
            r.draw_std(t);
            r.cap_main(o);
        });
    }
    single(spec("ps", "sv_coverage_output_mask_msaa", 1).tolerance(rr::Tolerance::raster(10, 1)), [](Rig& r, rr::CaptureSet& o) {
        D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 1, 4);
        d.SampleDesc.Quality = 0xffffffffu;
        r.check(r.d->CreateTexture2D(&d, nullptr, r.main.texture.put()), "standard pattern target");
        if (!r.ok()) return;
        r.main.view = r.rtv(r.main.texture.get());
        d.SampleDesc.Count = 1;
        d.SampleDesc.Quality = 0;
        r.check(r.d->CreateTexture2D(&d, nullptr, r.main.resolved.put()), "resolve target");
        r.main.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        r.main.width = r.main.height = 128;
        r.main.samples = 4;
        if (!r.ok()) return;
        r.bind_rt(r.main);
        r.clear(r.main, 0.1f, 0.1f, 0.15f, 1);
        r.viewport(128, 128);
        r.use_std();
        r.no_cull();
        Com<ID3D10PixelShader> ps = r.ps("struct PO { float4 c : SV_Target; uint m : SV_Coverage; };\nPO main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO o; o.c = float4(0.9, 0.7 * uv.y + 0.2, 0.2, 1); o.m = uv.x < 0.5 ? 0x5 : 0xa; return o; }\n");
        if (!r.ok()) return;
        r.d->PSSetShader(ps.get());
        r.draw_std(Geo::quad_uv(-0.8f, -0.8f, 0.8f, 0.8f, 0.5f, 0, 0, 1, 1));
        r.cap_main(o);
    });
    single(spec("ps", "sv_position_pixel_centers").tolerance(rr::Tolerance::tight(1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        paint(r, o, PS_HEAD "  float2 f = frac(p.xy); float chk = fmod(floor(p.x / 8) + floor(p.y / 8), 2); return float4(p.x / 128, p.y / 128, abs(f.x - 0.5) * 10 + chk * 0.5, 1); }\n", {}, {}, full_uv());
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
