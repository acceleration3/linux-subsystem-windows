#include "render/d3d11/scene3d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

struct ShowPass {
    Com<ID3D11PixelShader> ps;
    Com<ID3D11SamplerState> sampler;
    bool make(Rig& r, const char* source, D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_POINT) {
        ps = r.ps(std::string("Texture2D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n") + source);
        sampler = r.smp(sdesc(filter, D3D11_TEXTURE_ADDRESS_CLAMP));
        return ps && sampler;
    }
    void run(Rig& r, Target& to, ID3D11ShaderResourceView* from) {
        r.bind(to);
        ID3D11SamplerState* s = sampler.get();
        r.ctx->PSSetShaderResources(0, 1, &from);
        r.ctx->PSSetSamplers(0, 1, &s);
        r.draw_fs(ps.get());
        r.unbind_srvs();
    }
};

struct LifeState {
    Target a, b, view_target;
    Com<ID3D11ShaderResourceView> sa, sb;
    ShowPass step_pass, show_pass;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        if (!r.color(a, 64, 64, DXGI_FORMAT_R8_UNORM) || !r.color(b, 64, 64, DXGI_FORMAT_R8_UNORM) || !r.color(view_target)) return false;
        sa = r.srv(a.texture.get());
        sb = r.srv(b.texture.get());
        Bytes seed(64 * 64);
        std::uint32_t state = 12345;
        for (std::uint8_t& v : seed) v = rr::Pattern::unit_random(state) < 0.32f ? 255 : 0;
        for (int i = 0; i < 6; ++i) {
            const int x = 20 + i, y = 30;
            seed[static_cast<std::size_t>(y) * 64 + x] = 255;
        }
        r.ctx->UpdateSubresource(a.texture.get(), 0, nullptr, seed.data(), 64, 0);
        return sa && sb && step_pass.make(r,
            "float4 main(VO i) : SV_Target { int2 c = int2(i.p.xy); int n = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { if (x == 0 && y == 0) continue; n += t.Load(int3((c + int2(x, y) + 64) % 64, 0)).r > 0.5 ? 1 : 0; }\n"
            "  bool alive = t.Load(int3(c, 0)).r > 0.5; bool next = alive ? (n == 2 || n == 3) : (n == 3); return next ? 1.0 : 0.0; }\n") &&
               show_pass.make(r, "float4 main(VO i) : SV_Target { float v = t.Load(int3(int2(i.uv * 64.0), 0)).r; return float4(v * float3(0.3, 1.0, 0.5) + (1 - v) * float3(0.05, 0.05, 0.1), 1); }\n");
    }
    bool step(Rig& r, unsigned frame) {
        Target& from = frame % 2 == 0 ? a : b;
        Target& to = frame % 2 == 0 ? b : a;
        step_pass.run(r, to, (frame % 2 == 0 ? sa : sb).get());
        (void)from;
        show_pass.run(r, view_target, (frame % 2 == 0 ? sb : sa).get());
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, view_target); }
};
const AddState<LifeState> kLife(spec("scene", "game_of_life_ping_pong_16_frames").frames(16, {0, 5, 15}).tolerance(rr::Tolerance::tight(1)));

struct FeedbackState {
    Target a, b, out_target;
    Com<ID3D11ShaderResourceView> sa, sb;
    ShowPass feedback;
    Com<ID3D11Buffer> cb;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        if (!r.color(a) || !r.color(b) || !r.color(out_target)) return false;
        sa = r.srv(a.texture.get());
        sb = r.srv(b.texture.get());
        const float clear[4] = {0, 0, 0, 1};
        r.ctx->ClearRenderTargetView(a.view.get(), clear);
        r.ctx->ClearRenderTargetView(b.view.get(), clear);
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        return sa && sb && cb && feedback.make(r,
            "cbuffer K : register(b0) { float4 k; };\n"
            "float4 main(VO i) : SV_Target { float2 c = i.uv - 0.5; float a = 0.09; float2 q = float2(c.x * cos(a) - c.y * sin(a), c.x * sin(a) + c.y * cos(a)) * 0.97 + 0.5;\n"
            "  float4 prev = t.Sample(s, q) * 0.93; float2 d = i.uv - (0.5 + 0.3 * float2(cos(k.x * 0.9), sin(k.x * 0.9))); float spot = smoothstep(0.07, 0.0, length(d));\n"
            "  return prev + float4(spot * float3(1.0, 0.6 + 0.4 * sin(k.x), 0.3), 0); }\n", D3D11_FILTER_MIN_MAG_MIP_LINEAR);
    }
    bool step(Rig& r, unsigned frame) {
        const float k[4] = {static_cast<float>(frame), 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b0 = cb.get();
        r.ctx->PSSetConstantBuffers(0, 1, &b0);
        Target& to = frame % 2 == 0 ? b : a;
        feedback.run(r, to, (frame % 2 == 0 ? sa : sb).get());
        return true;
    }
    bool shot(Rig& r, unsigned frame, Cap& out) { return r.shot(out, frame % 2 == 0 ? b : a); }
};
const AddState<FeedbackState> kFeedback(spec("scene", "render_to_texture_feedback_previous_frame").frames(10, {0, 4, 9}).tolerance(rr::Tolerance::blend(4).with_edge(1)));

struct StreamingState {
    Com<ID3D11Texture2D> tex;
    Com<ID3D11ShaderResourceView> srv;
    Target target;
    ShowPass pass;
    unsigned levels = 0;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        levels = rr::Pattern::level_count(64, 64);
        tex = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels);
        if (!tex || !r.color(target)) return false;
        for (unsigned l = 0; l < levels; ++l) {
            const unsigned size = std::max(1u, 64u >> l);
            const Bytes data = rr::Pattern::checker(size, size, std::max(1u, size / 4), rr::Pattern::pack(30, 30, 40), rr::Pattern::pack(60, 60, 70));
            r.ctx->UpdateSubresource(tex.get(), l, nullptr, data.data(), size * 4, 0);
        }
        srv = r.srv(tex.get());
        return srv && pass.make(r, "float4 main(VO i) : SV_Target { float lod = i.uv.x * 6.0; return t.SampleLevel(s, i.uv * float2(1, 1), floor(lod)); }\n");
    }
    bool step(Rig& r, unsigned frame) {
        const unsigned level = frame % levels;
        const unsigned size = std::max(1u, 64u >> level);
        const Bytes data = rr::Pattern::checker(size, size, std::max(1u, size / 4), rr::Pattern::pack(240, 100 + frame * 20, 40), rr::Pattern::pack(40 + frame * 30, 200, 220 - frame * 20));
        const D3D11_BOX box{0, 0, 0, size, size, 1};
        r.ctx->UpdateSubresource(tex.get(), level, &box, data.data(), size * 4, 0);
        pass.run(r, target, srv.get());
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<StreamingState> kStreaming(spec("scene", "mipmapped_texture_streaming_update_subresource_per_frame").frames(7, {0, 3, 6}).tolerance(rr::Tolerance::tight(1)));

struct DiffuseState {
    Com<ID3D11Texture2D> ping[2];
    Com<ID3D11UnorderedAccessView> uav[2];
    Com<ID3D11ShaderResourceView> srv[2];
    Com<ID3D11ComputeShader> cs;
    Target target;
    ShowPass pass;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        for (int i = 0; i < 2; ++i) {
            ping[i] = r.tex(64, 64, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
            if (!ping[i]) return false;
            uav[i] = r.uav(ping[i].get());
            srv[i] = r.srv(ping[i].get());
            if (!uav[i] || !srv[i]) return false;
            const float zero[4] = {};
            r.ctx->ClearUnorderedAccessViewFloat(uav[i].get(), zero);
        }
        cs = r.cs("Texture2D<float> src : register(t0);\nRWTexture2D<float> dst : register(u0);\ncbuffer K : register(b0) { float4 k; };\n"
                  "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { int2 c = int2(id.xy); float s = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) s += src.Load(int3(clamp(c + int2(x, y), 0, 63), 0)) * (x == 0 && y == 0 ? 0.2 : 0.1);\n"
                  "  float2 src_pos = float2(32 + 20 * cos(k.x * 0.8), 32 + 20 * sin(k.x * 0.8)); if (length(float2(c) - src_pos) < 2.5) s += 0.9; dst[id.xy] = s * 0.985; }\n");
        return cs && r.color(target) && pass.make(r, "float4 main(VO i) : SV_Target { float v = t.Load(int3(int2(i.uv * 64.0), 0)).r; return float4(v * 1.5, v * v * 1.2, saturate(v * 0.5), 1); }\n");
    }
    bool step(Rig& r, unsigned frame) {
        Com<ID3D11Buffer> cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        const float k[4] = {static_cast<float>(frame), 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        const int from = frame % 2, to = 1 - from;
        ID3D11ShaderResourceView* v = srv[from].get();
        ID3D11UnorderedAccessView* u = uav[to].get();
        r.ctx->CSSetShader(cs.get(), nullptr, 0);
        r.ctx->CSSetConstantBuffers(0, 1, &b);
        r.ctx->CSSetShaderResources(0, 1, &v);
        r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
        r.ctx->Dispatch(8, 8, 1);
        ID3D11UnorderedAccessView* none = nullptr;
        ID3D11ShaderResourceView* nonev = nullptr;
        r.ctx->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
        r.ctx->CSSetShaderResources(0, 1, &nonev);
        pass.run(r, target, srv[to].get());
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<DiffuseState> kDiffuse(spec("scene", "compute_heat_diffusion_ping_pong_uav").frames(12, {0, 5, 11}).tolerance(rr::Tolerance::blend(2)));

struct TaaState {
    Target current, history_a, history_b, out_target;
    Depth depth;
    Com<ID3D11ShaderResourceView> sc, sa, sb;
    CubePipe cubes;
    ShowPass resolve;
    Com<ID3D11DepthStencilState> ds;
    Com<ID3D11Buffer> cb;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        if (!r.color(current) || !r.color(history_a) || !r.color(history_b) || !r.color(out_target) || !r.depth(depth) || !cubes.make(r)) return false;
        sc = r.srv(current.texture.get());
        sa = r.srv(history_a.texture.get());
        sb = r.srv(history_b.texture.get());
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        resolve.ps = r.ps("Texture2D cur : register(t0);\nTexture2D hist : register(t1);\nSamplerState s : register(s0);\ncbuffer K : register(b0) { float4 k; };\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                          "float4 main(VO i) : SV_Target { float3 c = cur.Sample(s, i.uv).rgb; float3 h = hist.Sample(s, i.uv).rgb; float3 mn = 1, mx = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { float3 n = cur.Sample(s, i.uv + float2(x, y) / 128.0).rgb; mn = min(mn, n); mx = max(mx, n); }\n"
                          "  h = clamp(h, mn, mx); return float4(lerp(c, h, k.x > 0.5 ? 0.85 : 0.0), 1); }\n");
        resolve.sampler = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
        return sc && sa && sb && ds && cb && resolve.ps && resolve.sampler;
    }
    bool step(Rig& r, unsigned frame) {
        static const float halton2[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
        static const float halton3[8] = {0.333f, 0.667f, 0.111f, 0.444f, 0.778f, 0.222f, 0.556f, 0.889f};
        const float jx = (halton2[frame % 8] - 0.5f) * 2.0f / 128.0f;
        const float jy = (halton3[frame % 8] - 0.5f) * 2.0f / 128.0f;
        const rr::Mat4 jitter = rr::Mat4::translate(jx, jy, 0);
        const float clear[4] = {0.08f, 0.09f, 0.14f, 1};
        r.bind(current, &depth, clear);
        cubes.bind(r);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        cubes.draw(r, jitter * CubePipe::camera() * rr::Mat4::translate(-0.5f, 0, 0) * rr::Mat4::rotate_y(0.7f) * rr::Mat4::rotate_x(0.5f));
        cubes.draw(r, jitter * CubePipe::camera() * rr::Mat4::translate(0.9f, 0.3f, 0.5f) * rr::Mat4::rotate_y(-0.6f) * rr::Mat4::rotate_z(0.4f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f));
        Target& to = frame % 2 == 0 ? history_b : history_a;
        ID3D11ShaderResourceView* views[2] = {sc.get(), (frame % 2 == 0 ? sa : sb).get()};
        r.bind(to);
        const float k[4] = {frame > 0 ? 1.0f : 0.0f, 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        ID3D11SamplerState* s = resolve.sampler.get();
        r.ctx->PSSetConstantBuffers(0, 1, &b);
        r.ctx->PSSetShaderResources(0, 2, views);
        r.ctx->PSSetSamplers(0, 1, &s);
        r.ctx->OMSetDepthStencilState(nullptr, 0);
        r.draw_fs(resolve.ps.get());
        r.unbind_srvs();
        return true;
    }
    bool shot(Rig& r, unsigned frame, Cap& out) { return r.shot(out, frame % 2 == 0 ? history_b : history_a); }
};
const AddState<TaaState> kTaa(spec("scene", "temporal_accumulation_jittered_history").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::psnr(26)));

struct MotionState {
    Target color, velocity, result;
    Depth depth;
    Com<ID3D11ShaderResourceView> sc, sv;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11InputLayout> il;
    Com<ID3D11Buffer> vb, ib, cb;
    Com<ID3D11DepthStencilState> ds;
    ShowPass blur;
    rr::Mat4 previous[2];
    bool have_previous = false;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        Bytes code;
        vs = r.vs("cbuffer F : register(b0) { column_major float4x4 cur; column_major float4x4 prev; };\nstruct VI { float3 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; float4 a : CUR; float4 b : PREV; };\n"
                  "VO main(VI i) { VO o; o.a = mul(cur, float4(i.p, 1)); o.b = mul(prev, float4(i.p, 1)); o.p = o.a; o.c = i.c; return o; }\n", &code);
        ps = r.ps("struct VO { float4 p : SV_Position; float3 c : COLOR; float4 a : CUR; float4 b : PREV; };\nstruct PO { float4 c : SV_Target0; float4 v : SV_Target1; };\n"
                  "PO main(VO i) { PO o; o.c = float4(i.c, 1); float2 v = i.a.xy / i.a.w - i.b.xy / i.b.w; o.v = float4(v * 0.5, 0, 1); return o; }\n");
        il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 12)}, code);
        const auto verts = rr::Mesh::cube_vertices();
        const auto indices = rr::Mesh::cube_indices();
        vb = r.vbuf(verts);
        ib = r.ibuf(indices);
        cb = r.buf(128, D3D11_BIND_CONSTANT_BUFFER);
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        if (!vs || !ps || !il || !vb || !cb || !ds || !r.color(color) || !r.color(velocity, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(result) || !r.depth(depth)) return false;
        sc = r.srv(color.texture.get());
        sv = r.srv(velocity.texture.get());
        blur.ps = r.ps("Texture2D col : register(t0);\nTexture2D vel : register(t1);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                       "float4 main(VO i) : SV_Target { float2 v = vel.Sample(s, i.uv).xy * float2(1, -1); float3 a = 0; for (int k = 0; k < 9; ++k) a += col.Sample(s, i.uv - v * (k / 8.0 - 0.5)).rgb; return float4(a / 9.0, 1); }\n");
        blur.sampler = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
        return sc && sv && blur.ps && blur.sampler;
    }
    bool step(Rig& r, unsigned frame) {
        const rr::Mat4 now = CubePipe::camera() * rr::Mat4::translate(-1.0f + 0.4f * frame, 0, 0) * rr::Mat4::rotate_y(0.5f * frame) * rr::Mat4::rotate_x(0.4f);
        if (!have_previous) {
            previous[0] = now;
            have_previous = true;
        }
        struct Pair {
            rr::Mat4 cur, prev;
        } pair{now, previous[0]};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, &pair, 0, 0);
        previous[0] = now;
        ID3D11RenderTargetView* views[2] = {color.view.get(), velocity.view.get()};
        const float clear[4] = {0.1f, 0.1f, 0.15f, 1};
        const float zero[4] = {};
        r.ctx->OMSetRenderTargets(2, views, depth.view.get());
        r.ctx->ClearRenderTargetView(views[0], clear);
        r.ctx->ClearRenderTargetView(views[1], zero);
        r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        r.viewport(0, 0, 128, 128);
        const UINT stride = sizeof(rr::ColoredVertex), offset = 0;
        ID3D11Buffer* b = vb.get();
        ID3D11Buffer* c = cb.get();
        D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK);
        rd.FrontCounterClockwise = TRUE;
        r.ctx->RSSetState(r.rast(rd).get());
        r.ctx->IASetInputLayout(il.get());
        r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
        r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->VSSetConstantBuffers(0, 1, &c);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        r.ctx->DrawIndexed(36, 0, 0);
        ID3D11RenderTargetView* none[2] = {};
        r.ctx->OMSetRenderTargets(2, none, nullptr);
        r.bind(result);
        ID3D11ShaderResourceView* v[2] = {sc.get(), sv.get()};
        ID3D11SamplerState* s = blur.sampler.get();
        r.ctx->PSSetShaderResources(0, 2, v);
        r.ctx->PSSetSamplers(0, 1, &s);
        r.ctx->OMSetDepthStencilState(nullptr, 0);
        r.draw_fs(blur.ps.get());
        r.unbind_srvs();
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, result); }
};
const AddState<MotionState> kMotion(spec("scene", "motion_blur_previous_frame_matrices").frames(6, {0, 2, 5}).tolerance(rr::Tolerance::psnr(26)));

struct ParticleState {
    Com<ID3D11Buffer> particles, alive, args, count;
    Com<ID3D11UnorderedAccessView> particle_uav, alive_uav;
    Com<ID3D11ShaderResourceView> particle_srv, alive_srv;
    Com<ID3D11ComputeShader> cs;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11Buffer> cb;
    Com<ID3D11BlendState> additive;
    Target target;
    std::string needs(Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); }
    bool init(Rig& r) {
        constexpr UINT kCount = 512;
        std::vector<float> seed(kCount * 8);
        for (UINT i = 0; i < kCount; ++i) {
            std::uint32_t s = i * 977u + 13u;
            const float a = rr::Pattern::unit_random(s) * 6.2831853f;
            const float v = 0.2f + rr::Pattern::unit_random(s) * 0.5f;
            seed[i * 8 + 0] = 0.0f;
            seed[i * 8 + 1] = -0.7f;
            seed[i * 8 + 2] = 0.35f * std::cos(a) * v;
            seed[i * 8 + 3] = 0.5f + std::fabs(std::sin(a)) * v;
            seed[i * 8 + 4] = rr::Pattern::unit_random(s) * 3.0f;
            seed[i * 8 + 5] = 3.0f;
        }
        particles = r.buf(kCount * 32, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, seed.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 32);
        alive = r.buf(kCount * 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
        const UINT init_args[4] = {6, 0, 0, 0};
        args = r.buf(16, 0, init_args, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        if (!particles || !alive || !args || !cb) return false;
        particle_uav = D3d11Tools::structured_uav(r.kit.dev, particles.get(), kCount);
        particle_srv = D3d11Tools::structured_srv(r.kit.dev, particles.get(), 0, kCount);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = kCount;
        ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
        alive_uav = r.uav(alive.get(), &ud);
        alive_srv = D3d11Tools::structured_srv(r.kit.dev, alive.get(), 0, kCount);
        cs = r.cs(
            "struct P { float2 pos; float2 vel; float age; float life; float2 pad; };\nRWStructuredBuffer<P> ps : register(u0);\nAppendStructuredBuffer<uint> alive : register(u1);\ncbuffer K : register(b0) { float4 k; };\n"
            "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { P p = ps[id.x]; p.age += 0.12; p.vel.y -= 0.35 * 0.12; p.pos += p.vel * 0.12;\n"
            "  if (p.age > p.life) { p.pos = float2(0.0, -0.7); uint h = id.x * 747796405u + asuint(k.x) * 2891336453u + 1u; h = ((h >> ((h >> 28) + 4)) ^ h) * 277803737u; h = (h >> 22) ^ h; float a = (h & 0xFFFFFu) / 1048576.0 * 6.2831853;\n"
            "    uint g = (h ^ 0x9E3779B9u) * 2654435761u; float v = 0.2 + ((g >> 8) & 0xFFFFu) / 65536.0 * 0.5; p.vel = float2(0.35 * cos(a) * v, 0.5 + abs(sin(a)) * v); p.age = 0; }\n"
            "  ps[id.x] = p; if (id.x < 64 * (1 + (uint)k.x)) alive.Append(id.x); }\n");
        vs = r.vs("struct P { float2 pos; float2 vel; float age; float life; float2 pad; };\nStructuredBuffer<P> ps : register(t0);\nStructuredBuffer<uint> alive : register(t1);\nstruct VO { float4 p : SV_Position; float2 q : Q; float a : A; };\n"
                  "static const float2 q[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1), float2(0, 1)};\n"
                  "VO main(uint v : SV_VertexID, uint inst : SV_InstanceID) { P p = ps[alive[inst]]; float2 c = q[v];\n"
                  "  VO o; o.q = c; o.p = float4(p.pos + (c - 0.5) * 0.06, 0, 1); o.a = 1.0 - p.age / p.life; return o; }\n");
        ps = r.ps("struct VO { float4 p : SV_Position; float2 q : Q; float a : A; };\nfloat4 main(VO i) : SV_Target { float d = length(i.q - 0.5) * 2; float f = saturate(1 - d); return float4(float3(1.0, 0.55, 0.2) * f * i.a, f * i.a); }\n");
        D3D11_BLEND_DESC bd = bdesc();
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        additive = r.blend(bd);
        return particle_uav && particle_srv && alive_uav && alive_srv && cs && vs && ps && additive && r.color(target);
    }
    bool step(Rig& r, unsigned frame) {
        const float k[4] = {static_cast<float>(std::min(frame, 7u)), 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        ID3D11UnorderedAccessView* uavs[2] = {particle_uav.get(), alive_uav.get()};
        const UINT counts[2] = {0xFFFFFFFFu, 0};
        r.ctx->CSSetShader(cs.get(), nullptr, 0);
        r.ctx->CSSetConstantBuffers(0, 1, &b);
        r.ctx->CSSetUnorderedAccessViews(0, 2, uavs, counts);
        r.ctx->Dispatch(8, 1, 1);
        r.ctx->CopyStructureCount(args.get(), 4, alive_uav.get());
        ID3D11UnorderedAccessView* none[2] = {};
        r.ctx->CSSetUnorderedAccessViews(0, 2, none, nullptr);
        const float clear[4] = {0.02f, 0.02f, 0.05f, 1};
        r.bind(target, nullptr, clear);
        r.ctx->OMSetBlendState(additive.get(), clear, 0xffffffffu);
        ID3D11ShaderResourceView* views[2] = {particle_srv.get(), alive_srv.get()};
        r.ctx->VSSetShaderResources(0, 2, views);
        r.ctx->IASetInputLayout(nullptr);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        r.ctx->DrawInstancedIndirect(args.get(), 0);
        ID3D11ShaderResourceView* nonev[2] = {};
        r.ctx->VSSetShaderResources(0, 2, nonev);
        r.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<ParticleState> kParticles(spec("scene", "gpu_particles_compute_append_draw_instanced_indirect_16_frames").frames(16, {1, 7, 15}).tolerance(rr::Tolerance::psnr(26)));

}  // namespace

}  // namespace lsw::suites::render::d3d11
