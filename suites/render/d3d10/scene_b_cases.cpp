#include "render/d3d10/util.hpp"

#include "support/test_window.hpp"

#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

struct Particle {
    float p[3], v[3], life, size;
};

const char* kParticleVs =
    "struct VI { float3 p : POSITION; float3 v : NORMAL; float2 ls : TEXCOORD0; };\n"
    "VI main(VI i) { return i; }\n";

const char* kParticleUpdateGs =
    "struct VI { float3 p : POSITION; float3 v : NORMAL; float2 ls : TEXCOORD0; };\n"
    "float hash(float n) { return frac(sin(n * 12.9898) * 43758.5453); }\n"
    "[maxvertexcount(1)] void main(point VI i[1], uint pid : SV_PrimitiveID, inout PointStream<VI> s) {\n"
    "  VI o = i[0]; float dt = 0.05; o.v.y -= 1.6 * dt; o.p += o.v * dt; o.ls.x -= dt;\n"
    "  if (o.p.y < -0.85) { o.p.y = -0.85; o.v.y = -o.v.y * 0.65; o.v.xz *= 0.9; }\n"
    "  if (o.ls.x <= 0) { float h = hash(pid + o.ls.y * 17.0); o.p = float3(-0.6 + 0.2 * hash(pid * 3.1), 0.7, 0.3 * (hash(pid * 5.7) - 0.5)); o.v = float3(0.5 + 0.7 * hash(pid * 7.3 + 1), 0.2 * hash(pid) - 0.1, 0.4 * (hash(pid * 2.3) - 0.5)); o.ls.x = 2 + 3 * h; }\n"
    "  s.Append(o); }\n";

const char* kParticleSpriteGs =
    "struct VI { float3 p : POSITION; float3 v : NORMAL; float2 ls : TEXCOORD0; };\n"
    "struct GO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "[maxvertexcount(4)] void main(point VI i[1], inout TriangleStream<GO> s) { GO o; float h = 0.018 + 0.01 * i[0].ls.y; float k = saturate(i[0].ls.x / 3); o.c = float4(1.0, 0.5 + 0.4 * k, 0.2 + 0.6 * (1 - k), 1);\n"
    "  o.p = float4(i[0].p.xy + float2(-h, h), 0.5, 1); o.uv = float2(0, 0); s.Append(o); o.p = float4(i[0].p.xy + float2(h, h), 0.5, 1); o.uv = float2(1, 0); s.Append(o);\n"
    "  o.p = float4(i[0].p.xy + float2(-h, -h), 0.5, 1); o.uv = float2(0, 1); s.Append(o); o.p = float4(i[0].p.xy + float2(h, -h), 0.5, 1); o.uv = float2(1, 1); s.Append(o); }\n";

struct ParticleState {
    Com<ID3D10Buffer> buffers[2];
    Com<ID3D10VertexShader> vs;
    Com<ID3D10GeometryShader> update_gs, sprite_gs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10BlendState> additive;
    Com<ID3D10DepthStencilState> off;
    int current = 0;
};

const UINT kParticles = 1024;

const Registrar kParticlesCase{[] {
    MultiBody<ParticleState> body;
    body.setup = [](Rig& r, ParticleState& s) {
        r.begin(256, 256);
        s.vs = r.vs(kParticleVs);
        s.layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0}});
        const std::vector<D3D10_SO_DECLARATION_ENTRY> decl = {{"POSITION", 0, 0, 3, 0}, {"NORMAL", 0, 0, 3, 0}, {"TEXCOORD", 0, 0, 2, 0}};
        s.update_gs = r.gs_so(kParticleUpdateGs, decl, 32);
        s.sprite_gs = r.gs(kParticleSpriteGs);
        s.ps = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float2 q = uv * 2 - 1; float a = saturate(1 - dot(q, q)); return float4(c.rgb * a, a); }\n");
        std::vector<Particle> init(kParticles);
        std::uint32_t state = 99;
        for (auto& p : init) {
            p.p[0] = -0.6f + 1.5f * rr::Pattern::unit_random(state);
            p.p[1] = -0.8f + 1.5f * rr::Pattern::unit_random(state);
            p.p[2] = 0;
            p.v[0] = 0.4f * (rr::Pattern::unit_random(state) - 0.2f);
            p.v[1] = 0.5f * rr::Pattern::unit_random(state);
            p.v[2] = 0;
            p.life = 0.5f + 4.0f * rr::Pattern::unit_random(state);
            p.size = rr::Pattern::unit_random(state);
        }
        s.buffers[0] = r.vbuf(init.data(), static_cast<UINT>(init.size() * sizeof(Particle)), D3D10_BIND_STREAM_OUTPUT);
        s.buffers[1] = r.vbuf(nullptr, kParticles * sizeof(Particle), D3D10_BIND_STREAM_OUTPUT);
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_ONE;
        bd.DestBlend = D3D10_BLEND_ONE;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_ONE;
        s.additive = r.blend(bd);
        s.off = r.depth(Rig::depth_desc());
        r.no_cull();
    };
    body.frame = [](Rig& r, ParticleState& s, unsigned f) {
        const UINT stride = sizeof(Particle), zero = 0;
        r.d->OMSetDepthStencilState(s.off.get(), 0);
        r.d->IASetInputLayout(s.layout.get());
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        r.d->VSSetShader(s.vs.get());
        r.d->GSSetShader(s.update_gs.get());
        r.d->PSSetShader(nullptr);
        ID3D10Buffer* source = s.buffers[s.current].get();
        ID3D10Buffer* target = s.buffers[1 - s.current].get();
        r.d->IASetVertexBuffers(0, 1, &source, &stride, &zero);
        r.d->SOSetTargets(1, &target, &zero);
        if (f == 0) {
            r.d->Draw(kParticles, 0);
        } else {
            r.d->DrawAuto();
        }
        ID3D10Buffer* none = nullptr;
        r.d->SOSetTargets(1, &none, &zero);
        s.current = 1 - s.current;
        r.clear(r.main, 0.02f, 0.02f, 0.05f, 1);
        source = s.buffers[s.current].get();
        r.d->IASetVertexBuffers(0, 1, &source, &stride, &zero);
        r.d->GSSetShader(s.sprite_gs.get());
        r.d->PSSetShader(s.ps.get());
        const float fct[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(s.additive.get(), fct, 0xffffffffu);
        r.d->DrawAuto();
        r.d->OMSetBlendState(nullptr, fct, 0xffffffffu);
    };
    multi<ParticleState>(spec("scene", "gpu_particles_stream_out_ping_pong").size(256, 256).frames(10, {0, 4, 9}).tolerance(scene_tol(12, 1e-3, 64)), body);
}};

const Registrar kLife{[] {
    struct S {
        Target a, b;
        Com<ID3D10ShaderResourceView> va, vb;
        Com<ID3D10PixelShader> step_ps, show_ps;
        Com<ID3D10SamplerState> point;
        Com<ID3D10Texture2D> seed;
        bool flip = false;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256);
        r.target(s.a, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.target(s.b, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
        s.va = r.srv(s.a.texture.get());
        s.vb = r.srv(s.b.texture.get());
        std::vector<std::uint8_t> cells(64 * 64 * 4);
        std::uint32_t state = 31337;
        for (int i = 0; i < 64 * 64; ++i) {
            const bool alive = rr::Pattern::unit_random(state) < 0.33f;
            cells[i * 4] = cells[i * 4 + 1] = cells[i * 4 + 2] = alive ? 255 : 0;
            cells[i * 4 + 3] = 255;
        }
        s.seed = r.tex2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, 1, 1, {cells.data()}, {64 * 4});
        if (!r.ok()) return;
        r.d->CopyResource(s.a.texture.get(), s.seed.get());
        s.step_ps = r.ps("Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 q = int2(p.xy); int n = 0;\n"
                         "  [unroll] for (int y = -1; y <= 1; ++y) [unroll] for (int x = -1; x <= 1; ++x) if (x != 0 || y != 0) n += t.Load(int3((q + int2(x, y) + 64) % 64, 0)).r > 0.5 ? 1 : 0;\n"
                         "  bool alive = t.Load(int3(q, 0)).r > 0.5; bool next = alive ? (n == 2 || n == 3) : (n == 3); return next ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1); }\n");
        s.show_ps = r.ps("Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float v = t.Load(int3(int2(p.xy) / 4, 0)).r; float2 g = frac(p.xy / 4); float edge = (g.x < 0.12 || g.y < 0.12) ? 0.25 : 1.0; return float4(float3(0.2, 0.9, 0.5) * v * edge + float3(0.05, 0.05, 0.1), 1); }\n");
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned) {
        Target& src = s.flip ? s.b : s.a;
        Target& dst = s.flip ? s.a : s.b;
        ID3D10ShaderResourceView* none = nullptr;
        r.d->PSSetShaderResources(0, 1, &none);
        r.bind_rt(dst);
        r.viewport(64, 64);
        r.ps_srv(0, (s.flip ? s.vb : s.va).get());
        r.fullscreen(s.step_ps.get());
        r.d->PSSetShaderResources(0, 1, &none);
        r.bind_rt(r.main);
        r.viewport(256, 256);
        r.ps_srv(0, (s.flip ? s.va : s.vb).get());
        r.fullscreen(s.show_ps.get());
        r.d->PSSetShaderResources(0, 1, &none);
        s.flip = !s.flip;
        (void)src;
    };
    multi<S>(spec("scene", "game_of_life_ping_pong_wraparound").size(256, 256).frames(12, {0, 5, 11}).tolerance(rr::Tolerance::blend(2)), body);
}};

const Registrar kRing{[] {
    struct S {
        Com<ID3D10Buffer> ring;
        UINT cursor = 0;
        int wraps = 0;
    };
    MultiBody<S> body;
    const UINT capacity = 4000;
    body.setup = [capacity](Rig& r, S& s) {
        r.begin(256, 256);
        r.use_std();
        s.ring = r.buf(capacity * sizeof(Vtx), D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, nullptr);
        r.no_cull();
    };
    body.frame = [capacity](Rig& r, S& s, unsigned f) {
        const UINT count = 6 * 130;
        D3D10_MAP mode = D3D10_MAP_WRITE_NO_OVERWRITE;
        if (s.cursor + count > capacity) {
            mode = D3D10_MAP_WRITE_DISCARD;
            s.cursor = 0;
            ++s.wraps;
        }
        void* mapped = nullptr;
        r.check(s.ring->Map(mode, 0, &mapped), "Map of the ring");
        if (!r.ok()) return;
        Vtx* out = static_cast<Vtx*>(mapped) + s.cursor;
        for (UINT i = 0; i < 130; ++i) {
            const float t = static_cast<float>(f) * 0.31f + static_cast<float>(i) * 0.11f;
            const float cx = 0.7f * std::cos(t * 1.3f + i * 0.05f), cy = 0.7f * std::sin(t * 0.9f + i * 0.03f), h = 0.03f + 0.02f * std::sin(t * 3);
            const float* c = palette(i);
            const std::vector<Vtx> q = Geo::quad(cx - h, cy - h, cx + h, cy + h, 0.5f, c[0], c[1], c[2]);
            std::memcpy(out + i * 6, q.data(), 6 * sizeof(Vtx));
        }
        s.ring->Unmap();
        r.clear(r.main, 0.04f, 0.05f, 0.09f, 1);
        r.std_vb(s.ring.get());
        r.d->Draw(count, s.cursor);
        s.cursor += count;
    };
    multi<S>(spec("scene", "dynamic_vertex_ring_no_overwrite_wrap").size(256, 256).frames(12, {0, 6, 11}).tolerance(rr::Tolerance::blend(3)), body);
}};

const Registrar kFeedback{[] {
    struct S {
        Target a, b;
        Com<ID3D10ShaderResourceView> va, vb;
        Com<ID3D10PixelShader> fade_ps, tex_ps;
        Com<ID3D10SamplerState> linear;
        bool flip = false;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256);
        r.target(s.a, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.target(s.b, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        s.va = r.srv(s.a.texture.get());
        s.vb = r.srv(s.b.texture.get());
        r.clear(s.a, 0, 0, 0, 1);
        r.clear(s.b, 0, 0, 0, 1);
        s.fade_ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  float2 q = uv - 0.5; float cs = cos(0.06), sn = sin(0.06); q = float2(cs * q.x - sn * q.y, sn * q.x + cs * q.y) * 0.97; return float4(t.Sample(s, q + 0.5).rgb * 0.95, 1); }\n");
        s.tex_ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, uv); }\n");
        s.linear = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        r.use_std();
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        Target& dst = s.flip ? s.a : s.b;
        ID3D10ShaderResourceView* none = nullptr;
        r.d->PSSetShaderResources(0, 1, &none);
        r.bind_rt(dst);
        r.viewport(256, 256);
        r.ps_smp(0, s.linear.get());
        r.ps_srv(0, (s.flip ? s.vb : s.va).get());
        Com<ID3D10VertexShader> vs = r.vs(Std::kVs);
        Com<ID3D10InputLayout> layout = r.std_layout();
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(s.fade_ps.get());
        r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
        const float a = 0.8f * static_cast<float>(f);
        Com<ID3D10PixelShader> flat = r.ps(Std::kPs);
        r.d->PSSetShader(flat.get());
        r.draw_std(Geo::quad(0.5f * std::cos(a) - 0.06f, 0.5f * std::sin(a) - 0.06f, 0.5f * std::cos(a) + 0.06f, 0.5f * std::sin(a) + 0.06f, 0.5f, palette(f)[0], palette(f)[1], palette(f)[2]));
        r.d->PSSetShaderResources(0, 1, &none);
        r.bind_rt(r.main);
        r.ps_srv(0, (s.flip ? s.va : s.vb).get());
        r.d->PSSetShader(s.tex_ps.get());
        r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
        r.d->PSSetShaderResources(0, 1, &none);
        s.flip = !s.flip;
    };
    multi<S>(spec("scene", "render_to_texture_feedback_trails").size(256, 256).frames(8, {0, 3, 7}).tolerance(rr::Tolerance::filter(10, 0.01)), body);
}};

struct AtlasText {
    static std::vector<std::uint8_t> atlas() {
        std::vector<std::uint8_t> a(128 * 64);
        for (int g = 0; g < 128; ++g) {
            std::uint32_t h = static_cast<std::uint32_t>(g) * 2654435761u + 17u;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    h = h * 1664525u + 1013904223u;
                    const bool inside = x > 0 && x < 7 && y > 0 && y < 7;
                    a[(static_cast<std::size_t>(g / 16) * 8 + y) * 128 + (g % 16) * 8 + x] = inside && ((h >> 24) & 3) != 0 ? 255 : 0;
                }
        }
        return a;
    }
};

const Registrar kText{[] {
    single(spec("scene", "glyph_atlas_text_layout_with_shadow").size(256, 256).tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.use_std();
        const std::vector<std::uint8_t> atlas = AtlasText::atlas();
        Com<ID3D10Texture2D> tex = r.tex2d(128, 64, DXGI_FORMAT_R8_UNORM, D3D10_BIND_SHADER_RESOURCE, 1, 1, {atlas.data()}, {128});
        Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
        Com<ID3D10SamplerState> point = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        Com<ID3D10PixelShader> ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\nfloat4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float a = t.Sample(s, uv).r; return float4(c.rgb, c.a * a); }\n");
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        Com<ID3D10BlendState> bs = r.blend(bd);
        if (!r.ok()) return;
        r.d->PSSetShader(ps.get());
        r.ps_srv(0, view.get());
        r.ps_smp(0, point.get());
        const float f[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
        r.draw_std(Geo::quad(-1, -1, 1, 1, 0.9f, 0.16f, 0.2f, 0.32f));
        r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
        std::vector<Vtx> verts;
        auto px = [](float x) { return x / 128.0f - 1.0f; };
        auto py = [](float y) { return 1.0f - y / 128.0f; };
        const char* lines[] = {"LSW RENDER SUITE", "glyph atlas text", "0123456789 ABCDEFGH", "alpha blended quads", "point sampled R8 atlas", "line six of seven", "final line"};
        for (int pass = 0; pass < 2; ++pass) {
            for (int li = 0; li < 7; ++li) {
                const float scale = li == 0 ? 3.0f : 2.0f;
                float x = 12.0f + (pass == 0 ? 2.0f : 0.0f), y = 14.0f + li * 26.0f + (pass == 0 ? 2.0f : 0.0f);
                for (const char* c = lines[li]; *c != 0; ++c) {
                    const int g = static_cast<unsigned char>(*c) & 127;
                    const float u0 = static_cast<float>(g % 16) * 8 / 128.0f, v0 = static_cast<float>(g / 16) * 8 / 64.0f;
                    const float col[4] = {pass == 0 ? 0.0f : 1.0f, pass == 0 ? 0.0f : 0.9f - 0.1f * li, pass == 0 ? 0.0f : 0.5f, pass == 0 ? 0.6f : 1.0f};
                    std::vector<Vtx> q = Geo::quad_uv(px(x), py(y + 8 * scale), px(x + 8 * scale), py(y), 0.5f, u0, v0, u0 + 8.0f / 128.0f, v0 + 8.0f / 64.0f);
                    for (auto& v : q) { v.r = col[0]; v.g = col[1]; v.b = col[2]; v.a = col[3]; }
                    verts.insert(verts.end(), q.begin(), q.end());
                    x += 8 * scale;
                }
            }
        }
        r.draw_std(verts);
        r.cap_main(o);
    });
}};

const Registrar kUi{[] {
    struct S {
        Com<ID3D10PixelShader> panel_ps;
        Com<ID3D10BlendState> alpha;
        Com<ID3D10RasterizerState> scissor;
        Com<ID3D10Buffer> cb;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256);
        r.use_std();
        s.panel_ps = r.ps("cbuffer P : register(b0) { float4 color; float4 params; };\nfloat4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"
                          "  float2 q = abs(uv - 0.5) * 2; float2 d = q - (1 - params.x); float sd = length(max(d, 0)) + min(max(d.x, d.y), 0); float a = saturate((params.y - sd) * 30 * params.z) ; float g = 0.85 + 0.15 * uv.y; return float4(color.rgb * g, color.a * a); }\n");
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        s.alpha = r.blend(bd);
        auto rd = Rig::raster_desc();
        rd.ScissorEnable = TRUE;
        s.scissor = r.raster(rd);
        s.cb = r.cbuf(32);
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        r.clear(r.main, 0.18f, 0.2f, 0.26f, 1);
        r.d->RSSetState(s.scissor.get());
        D3D10_RECT full = {0, 0, 256, 256};
        r.d->RSSetScissorRects(1, &full);
        const float bf[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(s.alpha.get(), bf, 0xffffffffu);
        r.d->PSSetShader(s.panel_ps.get());
        r.ps_cb(0, s.cb.get());
        auto panel = [&](float x0, float y0, float x1, float y1, const float color[4], float radius, float fade) {
            const float data[8] = {color[0], color[1], color[2], color[3], radius, 1.0f, fade, 0};
            r.d->UpdateSubresource(s.cb.get(), 0, nullptr, data, 0, 0);
            r.draw_std(Geo::quad_uv(x0 / 128.0f - 1, 1 - y1 / 128.0f, x1 / 128.0f - 1, 1 - y0 / 128.0f, 0.5f, 0, 0, 1, 1));
        };
        const float shadow[4] = {0, 0, 0, 0.35f}, win[4] = {0.93f, 0.94f, 0.96f, 0.96f}, title[4] = {0.25f, 0.4f, 0.75f, 1}, row[4] = {0.8f, 0.85f, 0.95f, 1};
        const float fade = std::min(1.0f, 0.4f + 0.2f * static_cast<float>(f));
        panel(30, 36, 232, 226, shadow, 0.08f, fade);
        panel(24, 28, 226, 218, win, 0.08f, fade);
        panel(24, 28, 226, 60, title, 0.2f, fade);
        D3D10_RECT list = {32, 68, 218, 208};
        r.d->RSSetScissorRects(1, &list);
        const float scroll = 14.0f * static_cast<float>(f);
        for (int i = 0; i < 12; ++i) {
            float c[4] = {row[0] - 0.05f * (i % 3), row[1], row[2] + 0.03f * (i % 2), 1};
            panel(34, 72 + i * 22 - scroll, 216, 90 + i * 22 - scroll, c, 0.3f, fade);
        }
        r.d->RSSetScissorRects(1, &full);
        const float knob[4] = {0.95f, 0.5f, 0.2f, 1};
        panel(206, 70 + scroll * 1.2f, 214, 100 + scroll * 1.2f, knob, 0.9f, fade);
    };
    multi_single<S>(spec("scene", "ui_panels_scissor_scroll_alpha").size(256, 256).tolerance(rr::Tolerance::blend(4)), body, {0, 2, 4});
}};

const Registrar kEnv{[] {
    struct S {
        Com<ID3D10Texture2D> cube, depth;
        Com<ID3D10RenderTargetView> faces[6];
        Com<ID3D10DepthStencilView> face_depth;
        Com<ID3D10ShaderResourceView> srv;
        Com<ID3D10VertexShader> vs;
        Com<ID3D10PixelShader> flat_ps, reflect_ps;
        Com<ID3D10InputLayout> layout;
        Com<ID3D10Buffer> cb;
        Com<ID3D10SamplerState> smp;
        Com<ID3D10DepthStencilState> depth_on;
        MeshBuffers sphere, torus, box;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.cube = r.tex2d(96, 96, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 1, 6, {}, {}, D3D10_RESOURCE_MISC_TEXTURECUBE);
        s.depth = r.tex2d(96, 96, DXGI_FORMAT_D32_FLOAT, D3D10_BIND_DEPTH_STENCIL);
        for (UINT i = 0; i < 6; ++i) {
            D3D10_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rd.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2DARRAY;
            rd.Texture2DArray.FirstArraySlice = i;
            rd.Texture2DArray.ArraySize = 1;
            s.faces[i] = r.rtv(s.cube.get(), &rd);
        }
        s.face_depth = r.dsv(s.depth.get());
        for (int i = 0; i < 6; ++i) {
            const float init[4] = {0.15f * i, 0.1f, 0.3f - 0.04f * i, 1};
            r.d->ClearRenderTargetView(s.faces[i].get(), init);
        }
        D3D10_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURECUBE;
        sd.TextureCube.MipLevels = 1;
        s.srv = r.srv(s.cube.get(), &sd);
        s.vs = r.vs("cbuffer C : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 eye; };\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float3 w : TEXCOORD1; };\nVO main(VI i) { VO o; float4 w = mul(model, float4(i.p, 1)); o.p = mul(vp, w); o.n = mul((float3x3)model, i.n); o.w = w.xyz; return o; }\n");
        s.layout = r.layout(mesh_elements());
        s.flat_ps = r.ps("cbuffer C : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 eye; };\nfloat4 main(float4 p : SV_Position, float3 n : NORMAL, float3 w : TEXCOORD1) : SV_Target { float l = saturate(dot(normalize(n), normalize(float3(0.4, 0.8, 0.5)))) * 0.7 + 0.3; return float4(color.rgb * l, 1); }\n");
        s.reflect_ps = r.ps("cbuffer C : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 eye; };\nTextureCube env : register(t0); SamplerState s : register(s0);\n"
                            "float4 main(float4 p : SV_Position, float3 n : NORMAL, float3 w : TEXCOORD1) : SV_Target { float3 v = normalize(w - eye.xyz); float3 rv = reflect(v, normalize(n)); return float4(env.Sample(s, rv).rgb * 0.9 + 0.1 * color.rgb, 1); }\n");
        s.cb = r.cbuf(sizeof(ObjectConstants));
        s.smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        s.depth_on = r.depth(dd);
        s.sphere = upload(r, Geo::sphere(20, 28, 1.1f));
        s.torus = upload(r, Geo::torus(20, 10, 0.6f, 0.22f));
        s.box = upload(r, Geo::box(0.35f, 0.35f, 0.35f));
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        const float t = 0.7f * static_cast<float>(f);
        const V3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const V3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
        r.d->IASetInputLayout(s.layout.get());
        r.d->VSSetShader(s.vs.get());
        r.vs_cb(0, s.cb.get());
        r.ps_cb(0, s.cb.get());
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        ID3D10ShaderResourceView* none = nullptr;
        r.d->PSSetShaderResources(0, 1, &none);
        auto draw_scene = [&](const rr::Mat4& vp, bool with_center) {
            struct Item { const MeshBuffers* m; rr::Mat4 model; float col[4]; };
            std::vector<Item> items = {{&s.torus, rr::Mat4::translate(2.4f * std::cos(t), 0.2f, 2.4f * std::sin(t)) * rr::Mat4::rotate_x(t), {0.9f, 0.4f, 0.2f, 1}},
                                       {&s.box, rr::Mat4::translate(-2.4f * std::cos(t * 0.8f), 0.6f * std::sin(t), -2.4f * std::sin(t * 0.8f)) * rr::Mat4::rotate_y(t), {0.2f, 0.8f, 0.4f, 1}},
                                       {&s.box, rr::Mat4::translate(0, 2.3f, 0) * rr::Mat4::scale(2, 0.3f, 2), {0.3f, 0.4f, 0.9f, 1}},
                                       {&s.box, rr::Mat4::translate(0, -2.3f, 0) * rr::Mat4::scale(3, 0.3f, 3), {0.7f, 0.7f, 0.3f, 1}}};
            for (const Item& it : items) {
                ObjectConstants c{};
                std::memcpy(c.vp, vp.m, 64);
                std::memcpy(c.model, it.model.m, 64);
                std::memcpy(c.color, it.col, 16);
                r.d->UpdateSubresource(s.cb.get(), 0, nullptr, &c, 0, 0);
                draw_mesh(r, *it.m);
            }
            (void)with_center;
        };
        r.d->PSSetShader(s.flat_ps.get());
        rr::Mat4 proj = rr::Mat4::perspective_d3d(1.5707963f, 1.0f, 0.1f, 30.0f);
        proj.m[0] = -proj.m[0];
        for (int i = static_cast<int>(f % 6); i == static_cast<int>(f % 6); i += 6) {
            ID3D10RenderTargetView* rt = s.faces[i].get();
            r.d->OMSetRenderTargets(1, &rt, s.face_depth.get());
            const float sky[4] = {0.3f + 0.1f * i, 0.5f, 0.8f - 0.1f * i, 1};
            r.d->ClearRenderTargetView(rt, sky);
            r.d->ClearDepthStencilView(s.face_depth.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
            r.viewport(96, 96);
            draw_scene(proj * look_at({0, 0, 0}, dirs[i], ups[i]), false);
        }
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.1f, 0.1f, 0.14f, 1);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        r.d->PSSetShader(s.flat_ps.get());
        const rr::Mat4 cam = camera(0.0f, 0.0f, 9.0f, 1.0f, 0.55f);
        draw_scene(cam, false);
        r.d->PSSetShader(s.reflect_ps.get());
        r.ps_srv(0, s.srv.get());
        r.ps_smp(0, s.smp.get());
        ObjectConstants c{};
        std::memcpy(c.vp, cam.m, 64);
        const rr::Mat4 id = rr::Mat4::identity();
        std::memcpy(c.model, id.m, 64);
        c.color[0] = 0.3f; c.color[1] = 0.3f; c.color[2] = 0.35f; c.color[3] = 1;
        c.extra[2] = 9.0f;
        r.d->UpdateSubresource(s.cb.get(), 0, nullptr, &c, 0, 0);
        draw_mesh(r, s.sphere);
        r.d->PSSetShaderResources(0, 1, &none);
    };
    multi<S>(spec("scene", "environment_cube_one_face_refreshed_per_frame").size(256, 256).frames(8, {2, 5, 7}).tolerance(scene_tol(24, 5e-4, 40)), body);
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
