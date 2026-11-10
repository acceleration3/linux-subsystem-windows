#include "render/d3d11/scene3d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

rr::Mat4 view_matrix() { return rr::Mat4::rotate_x(0.5f) * rr::Mat4::translate(0.0f, -3.0f, -8.5f); }
rr::Mat4 proj_matrix() { return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 60.0f); }

struct Stage {
    LitPipe pipe;
    GpuMesh cube, sphere, ground;
    bool make(Rig& r) { return pipe.make(r) && cube.make(r, LitMesh::cube()) && sphere.make(r, LitMesh::sphere(24, 16)) && ground.make(r, LitMesh::plane(6.0f)); }
    void draw(Rig& r, const rr::Mat4& vp) {
        pipe.bind(r);
        ground.bind(r);
        pipe.set(r, vp, rr::Mat4::identity(), 0.7f, 0.7f, 0.65f, 1.0f);
        ground.draw(r);
        cube.bind(r);
        pipe.set(r, vp, trs(-2.2f, 1.0f, -0.5f, 1.0f, 1.0f, 1.0f, 0.5f), 0.85f, 0.3f, 0.25f);
        cube.draw(r);
        pipe.set(r, vp, trs(1.6f, 0.8f, 1.2f, 0.8f, 1.6f, 0.8f, -0.4f), 0.25f, 0.5f, 0.85f);
        cube.draw(r);
        sphere.bind(r);
        pipe.set(r, vp, trs(0.4f, 1.1f, -1.8f, 1.1f, 1.1f, 1.1f), 0.3f, 0.8f, 0.35f);
        sphere.draw(r);
    }
};

const char* const kFsHead = "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";

bool pass(Rig& r, Target& to, ID3D11PixelShader* ps, std::initializer_list<ID3D11ShaderResourceView*> views, ID3D11SamplerState* sampler = nullptr) {
    r.unbind_srvs();
    r.bind(to);
    std::vector<ID3D11ShaderResourceView*> v(views);
    r.ctx->PSSetShaderResources(0, static_cast<UINT>(v.size()), v.data());
    if (sampler != nullptr) r.ctx->PSSetSamplers(0, 1, &sampler);
    r.draw_fs(ps);
    return true;
}

const Add kSeparateStencil(spec("om", "stencil_front_and_back_face_separate_ops").uniform_ok("stencil"), [](Rig& r, Cap& out) {
    Depth depth;
    CubePipe cubes;
    if (!r.depth(depth, 128, 128, DXGI_FORMAT_D24_UNORM_S8_UINT) || !cubes.make(r, nullptr, D3D11_CULL_NONE)) return false;
    D3D11_DEPTH_STENCIL_DESC d = ddesc(false, D3D11_COMPARISON_ALWAYS, false);
    d.StencilEnable = TRUE;
    d.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR_SAT, D3D11_COMPARISON_ALWAYS};
    d.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    Com<ID3D11DepthStencilState> state = r.dss(d);
    if (!state) return false;
    r.ctx->OMSetRenderTargets(0, nullptr, depth.view.get());
    r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 10);
    r.viewport(0, 0, 128, 128);
    cubes.bind(r);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    r.ctx->OMSetDepthStencilState(state.get(), 77);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.6f, 0, 0) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f));
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(0.7f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f) * rr::Mat4::scale(0.9f, 0.9f, 0.9f));
    rr::Image stencil;
    if (!r.kit.read_stencil(depth, stencil, r.error)) return false;
    out.add("stencil", std::move(stencil));
    return true;
});

const Add kStencilMasks(spec("om", "stencil_read_and_write_masks_with_reference").uniform_ok("stencil"), [](Rig& r, Cap& out) {
    Depth depth;
    Com<ID3D11VertexShader> vs = r.vs(Hlsl3d::kFsVsZ);
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!vs || !zb || !rs || !r.depth(depth, 128, 128, DXGI_FORMAT_D24_UNORM_S8_UINT)) return false;
    auto make = [&](D3D11_COMPARISON_FUNC func, D3D11_STENCIL_OP pass_op, UINT8 read_mask, UINT8 write_mask) {
        D3D11_DEPTH_STENCIL_DESC d = ddesc(false, D3D11_COMPARISON_ALWAYS, false);
        d.StencilEnable = TRUE;
        d.StencilReadMask = read_mask;
        d.StencilWriteMask = write_mask;
        d.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, pass_op, func};
        d.BackFace = d.FrontFace;
        return r.dss(d);
    };
    Com<ID3D11DepthStencilState> fill = make(D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_REPLACE, 0xff, 0xff);
    Com<ID3D11DepthStencilState> masked_write = make(D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_REPLACE, 0xff, 0x0f);
    Com<ID3D11DepthStencilState> masked_read_equal = make(D3D11_COMPARISON_EQUAL, D3D11_STENCIL_OP_INCR, 0xf0, 0xff);
    Com<ID3D11DepthStencilState> greater = make(D3D11_COMPARISON_GREATER, D3D11_STENCIL_OP_REPLACE, 0xff, 0xff);
    if (!fill || !masked_write || !masked_read_equal || !greater) return false;
    ID3D11DeviceContext* c = r.ctx;
    c->OMSetRenderTargets(0, nullptr, depth.view.get());
    c->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    r.viewport(0, 0, 128, 128);
    c->RSSetState(rs.get());
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.get(), nullptr, 0);
    c->PSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* cb = zb.get();
    c->VSSetConstantBuffers(0, 1, &cb);
    const float z[4] = {0.5f, 0, 0, 0};
    c->UpdateSubresource(zb.get(), 0, nullptr, z, 0, 0);
    auto draw = [&](const D3D11_RECT& rect, ID3D11DepthStencilState* s, UINT ref) {
        c->RSSetScissorRects(1, &rect);
        c->OMSetDepthStencilState(s, ref);
        c->Draw(3, 0);
    };
    draw({0, 0, 128, 128}, fill.get(), 0xA5);
    draw({0, 0, 64, 64}, masked_write.get(), 0x3C);
    draw({64, 0, 128, 64}, masked_read_equal.get(), 0xA0);
    draw({0, 64, 64, 128}, masked_read_equal.get(), 0x50);
    draw({64, 64, 128, 128}, greater.get(), 0xB0);
    rr::Image stencil;
    if (!r.kit.read_stencil(depth, stencil, r.error)) return false;
    out.add("stencil", std::move(stencil));
    return true;
});

const Add kForced(spec("rs", "forced_sample_count_uav_only_rendering").variant(1).tolerance(rr::Tolerance::coverage_edges()).uniform_ok("counts"), [](Rig& r, Cap& out) {
    ID3D11Device1* raw = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&raw));
    Com<ID3D11Device1> dev1(raw);
    if (!dev1) return r.fail("ID3D11Device1 is unavailable");
    Com<ID3D11Texture2D> tex = r.tex(64, 64, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS);
    D3D11_RASTERIZER_DESC1 d{};
    d.FillMode = D3D11_FILL_SOLID;
    d.CullMode = D3D11_CULL_NONE;
    d.DepthClipEnable = TRUE;
    d.ForcedSampleCount = 4;
    Com<ID3D11RasterizerState1> rs;
    Basic basic;
    const char* ps = "RWTexture2D<uint> u : register(u0);\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\nvoid main(VO i, uint cov : SV_Coverage) { u[uint2(i.p.xy)] = countbits(cov) + 1; }\n";
    if (!tex || !r.ok(dev1->CreateRasterizerState1(&d, rs.put()), "CreateRasterizerState1") || !basic.make(r, ps)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(5, 0.85f, 0, 0, 0.2f));
    if (!uav || !vb) return false;
    const UINT zero[4] = {};
    r.ctx->ClearUnorderedAccessViewUint(uav.get(), zero);
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &u, &initial);
    r.viewport(0, 0, 64, 64);
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(15, 0);
    TexelImage texels;
    if (!r.read_texels(tex.get(), 0, texels)) return false;
    rr::Image counts(64, 64, rr::PixelKind::R8Uint);
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x) {
            std::uint32_t v;
            std::memcpy(&v, texels.data.data() + (y * 64 + x) * 4, 4);
            counts.row(y)[x] = static_cast<std::uint8_t>(v);
        }
    out.add("counts", std::move(counts));
    return true;
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
    return SUCCEEDED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof o)) && o.UAVOnlyRenderingForcedSampleCount ? std::string() : std::string("UAV-only rendering with a forced sample count");
});

const Add kForwardPlus(spec("scene", "forward_plus_tiled_light_culling_compute").size(256, 256).tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    constexpr int kLights = 24;
    std::vector<float> lights;
    for (int i = 0; i < kLights; ++i) {
        std::uint32_t s = 100 + i * 31;
        lights.insert(lights.end(), {rr::Pattern::unit_random(s) * 256.0f, rr::Pattern::unit_random(s) * 256.0f, 18.0f + rr::Pattern::unit_random(s) * 30.0f, rr::Pattern::unit_random(s)});
    }
    Com<ID3D11Buffer> light_buffer = r.buf(kLights * 16, D3D11_BIND_SHADER_RESOURCE, lights.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    Com<ID3D11Buffer> list = r.buf(16 * 16 * (kLights + 1) * 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11ComputeShader> cull = r.cs(
        "StructuredBuffer<float4> lights : register(t0);\nRWStructuredBuffer<uint> list : register(u0);\ngroupshared uint count;\n"
        "[numthreads(16, 1, 1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID) { uint tile = g.y * 16 + g.x; if (t.x == 0) count = 0; GroupMemoryBarrierWithGroupSync();\n"
        "  float2 lo = float2(g.xy) * 16, hi = lo + 16; for (uint i = t.x; i < 24; i += 16) { float4 l = lights[i]; float2 c = clamp(l.xy, lo, hi); if (length(c - l.xy) < l.z) { uint slot; InterlockedAdd(count, 1, slot); list[tile * 25 + 1 + slot] = i; } }\n"
        "  GroupMemoryBarrierWithGroupSync(); if (t.x == 0) list[tile * 25] = count; }\n");
    Com<ID3D11PixelShader> shade = r.ps(std::string(kFsHead) +
        "StructuredBuffer<float4> lights : register(t0);\nStructuredBuffer<uint> list : register(t1);\n"
        "float4 main(VO i) : SV_Target { uint2 tile = uint2(i.p.xy) / 16; uint base = (tile.y * 16 + tile.x) * 25; uint n = list[base]; float3 c = float3(0.05, 0.06, 0.09) + 0.04 * (((uint(i.p.x) >> 3) ^ (uint(i.p.y) >> 3)) & 1);\n"
        "  for (uint k = 0; k < n; ++k) { float4 l = lights[list[base + 1 + k]]; float d = length(i.p.xy - l.xy) / l.z; float a = saturate(1 - d); c += a * a * float3(0.5 + 0.5 * sin(l.w * 6.28), 0.5 + 0.5 * sin(l.w * 6.28 + 2.1), 0.5 + 0.5 * sin(l.w * 6.28 + 4.2)) * 1.4; }\n"
        "  return float4(c, 1); }\n");
    Target target;
    if (!light_buffer || !list || !cull || !shade || !r.color(target, 256, 256)) return false;
    Com<ID3D11ShaderResourceView> lights_srv = D3d11Tools::structured_srv(r.kit.dev, light_buffer.get(), 0, kLights);
    Com<ID3D11UnorderedAccessView> list_uav = D3d11Tools::structured_uav(r.kit.dev, list.get(), 16 * 16 * 25);
    Com<ID3D11ShaderResourceView> list_srv = D3d11Tools::structured_srv(r.kit.dev, list.get(), 0, 16 * 16 * 25);
    if (!lights_srv || !list_uav || !list_srv) return r.fail("view creation failed");
    ID3D11ShaderResourceView* ls = lights_srv.get();
    ID3D11UnorderedAccessView* lu = list_uav.get();
    r.ctx->CSSetShader(cull.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &ls);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &lu, nullptr);
    r.ctx->Dispatch(16, 16, 1);
    ID3D11UnorderedAccessView* none = nullptr;
    ID3D11ShaderResourceView* nones = nullptr;
    r.ctx->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
    r.ctx->CSSetShaderResources(0, 1, &nones);
    pass(r, target, shade.get(), {lights_srv.get(), list_srv.get()});
    return r.shot(out, target);
});

const Add kSsao(spec("scene", "ssao_hemisphere_kernel_depth_and_normal_buffers").size(256, 256).tolerance(rr::Tolerance::blend(4).with_edge(1)), [](Rig& r, Cap& out) {
    Stage st;
    Target gbuf_depth, gbuf_normal, ao, final_target;
    Depth depth;
    if (!st.make(r) || !r.color(gbuf_depth, 256, 256, DXGI_FORMAT_R32_FLOAT) || !r.color(gbuf_normal, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM) || !r.color(ao, 256, 256) || !r.color(final_target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11PixelShader> g_ps = r.ps(std::string(Hlsl3::kLitIn) +
        "struct PO { float4 d : SV_Target0; float4 n : SV_Target1; };\nPO main(VO i) { PO o; o.d = float4(i.pos.w, 0, 0, 1); o.n = float4(normalize(i.n) * 0.5 + 0.5, 1); return o; }\n");
    Com<ID3D11PixelShader> ao_ps = r.ps(std::string(kFsHead) +
        "Texture2D<float> zt : register(t0);\nTexture2D<float4> nt : register(t1);\n"
        "float4 main(VO i) : SV_Target { int2 c = int2(i.p.xy); float z = zt.Load(int3(c, 0)); if (z > 100) return 1; float3 n = nt.Load(int3(c, 0)).xyz * 2 - 1; float occ = 0;\n"
        "  [unroll] for (int k = 0; k < 16; ++k) { float a = k * 2.399963; float rad = (0.2 + 0.8 * frac(k * 0.618034)) * 14.0; int2 o = int2(cos(a) * rad, sin(a) * rad); float s = zt.Load(int3(clamp(c + o, 0, 255), 0));\n"
        "    float diff = z - s; occ += (diff > 0.15 && diff < 2.5) ? (1.0 - diff / 2.5) : 0; }\n"
        "  float v = 1 - occ / 16.0 * 1.6; return float4(v, v, v, 1); }\n");
    Com<ID3D11PixelShader> comp = r.ps(std::string(kFsHead) +
        "Texture2D<float4> ao : register(t0);\nTexture2D<float4> nt : register(t1);\nfloat4 main(VO i) : SV_Target { float3 n = nt.Load(int3(i.p.xy, 0)).xyz * 2 - 1; float l = saturate(dot(n, normalize(float3(0.4, 0.8, 0.5)))); float a = ao.Load(int3(i.p.xy, 0)).r;\n"
        "  return float4(float3(0.8, 0.75, 0.7) * (0.15 * a + 0.45 * l * a + 0.15), 1); }\n");
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!g_ps || !ao_ps || !comp || !ds) return false;
    ID3D11RenderTargetView* views[2] = {gbuf_depth.view.get(), gbuf_normal.view.get()};
    const float far_value[4] = {1000.0f, 0, 0, 0};
    const float zero[4] = {0.5f, 0.5f, 0.5f, 1};
    r.ctx->OMSetRenderTargets(2, views, depth.view.get());
    r.ctx->ClearRenderTargetView(views[0], far_value);
    r.ctx->ClearRenderTargetView(views[1], zero);
    r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    r.viewport(0, 0, 256, 256);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)).get());
    r.ctx->PSSetShader(g_ps.get(), nullptr, 0);
    st.draw(r, proj_matrix() * view_matrix());
    ID3D11RenderTargetView* none[2] = {};
    r.ctx->OMSetRenderTargets(2, none, nullptr);
    r.ctx->OMSetDepthStencilState(nullptr, 0);
    r.ctx->IASetInputLayout(nullptr);
    Com<ID3D11ShaderResourceView> zs = r.srv(gbuf_depth.texture.get());
    Com<ID3D11ShaderResourceView> ns = r.srv(gbuf_normal.texture.get());
    Com<ID3D11ShaderResourceView> as = r.srv(ao.texture.get());
    if (!zs || !ns || !as) return false;
    pass(r, ao, ao_ps.get(), {zs.get(), ns.get()});
    pass(r, final_target, comp.get(), {as.get(), ns.get()});
    return r.shot(out, final_target) && r.shot(out, ao, "ao");
});

const Add kFxaa(spec("scene", "fxaa_like_luma_edge_blur_post_process").size(256, 256).tolerance(rr::Tolerance::blend(4).with_edge(1)), [](Rig& r, Cap& out) {
    Stage st;
    Target scene, result;
    Depth depth;
    if (!st.make(r) || !r.color(scene, 256, 256) || !r.color(result, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11PixelShader> flat = r.ps(std::string(Hlsl3::kLitIn) + "float4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.3)))); float3 a = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.2)) : 1.0); return float4(a * (0.2 + 0.8 * l), 1); }\n");
    Com<ID3D11PixelShader> post = r.ps(std::string(kFsHead) +
        "Texture2D t : register(t0);\nSamplerState s : register(s0);\nfloat luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
        "float4 main(VO i) : SV_Target { float2 px = 1.0 / 256.0; float3 m = t.Sample(s, i.uv).rgb; float lm = luma(m);\n"
        "  float ln = luma(t.Sample(s, i.uv + float2(0, -px.y)).rgb), ls = luma(t.Sample(s, i.uv + float2(0, px.y)).rgb), le = luma(t.Sample(s, i.uv + float2(px.x, 0)).rgb), lw = luma(t.Sample(s, i.uv + float2(-px.x, 0)).rgb);\n"
        "  float lo = min(lm, min(min(ln, ls), min(le, lw))), hi = max(lm, max(max(ln, ls), max(le, lw))); float range = hi - lo; if (range < max(0.05, hi * 0.125)) return float4(m, 1);\n"
        "  bool horizontal = abs(ln + ls - 2 * lm) >= abs(le + lw - 2 * lm); float2 dir = horizontal ? float2(px.x, 0) : float2(0, px.y); float3 a = t.Sample(s, i.uv - dir * 0.5).rgb, b = t.Sample(s, i.uv + dir * 0.5).rgb, c = t.Sample(s, i.uv - dir * 1.5).rgb, d = t.Sample(s, i.uv + dir * 1.5).rgb;\n"
        "  return float4((a + b) * 0.35 + (c + d) * 0.15, 1); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!flat || !post || !smp || !ds) return false;
    const float sky[4] = {0.55f, 0.7f, 0.85f, 1};
    r.bind(scene, &depth, sky);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)).get());
    r.ctx->PSSetShader(flat.get(), nullptr, 0);
    st.draw(r, proj_matrix() * view_matrix());
    r.ctx->OMSetDepthStencilState(nullptr, 0);
    r.ctx->IASetInputLayout(nullptr);
    Com<ID3D11ShaderResourceView> srv = r.srv(scene.texture.get());
    if (!srv) return false;
    pass(r, result, post.get(), {srv.get()}, smp.get());
    return r.shot(out, result) && r.shot(out, scene, "aliased");
});

const Add kFog(spec("scene", "volumetric_fog_3d_texture_ray_march").size(256, 256).tolerance(rr::Tolerance::blend(4)), [](Rig& r, Cap& out) {
    D3D11_TEXTURE3D_DESC d{};
    d.Width = d.Height = d.Depth = 32;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8_UNORM;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture3D> volume;
    if (!r.ok(r.dev->CreateTexture3D(&d, nullptr, volume.put()), "CreateTexture3D")) return false;
    Com<ID3D11ComputeShader> fill = r.cs(
        "RWTexture3D<unorm float> u : register(u0);\n[numthreads(4, 4, 4)] void main(uint3 id : SV_DispatchThreadID) { float3 p = (float3(id) + 0.5) / 32.0; float v = 0;\n"
        "  v += saturate(1 - length(p - float3(0.3, 0.4, 0.5)) * 3.5); v += saturate(1 - length(p - float3(0.7, 0.55, 0.35)) * 4.5) * 0.9; v += 0.25 * saturate(1 - abs(p.y - 0.2) * 6.0); u[id] = saturate(v); }\n");
    Com<ID3D11PixelShader> march = r.ps(std::string(kFsHead) +
        "Texture3D<float> vol : register(t0);\nSamplerState s : register(s0);\n"
        "float4 main(VO i) : SV_Target { float3 bg = lerp(float3(0.1, 0.12, 0.2), float3(0.5, 0.6, 0.8), 1 - i.uv.y); float3 dir = normalize(float3(i.uv.x - 0.5, 0.5 - i.uv.y, 0.9)); float3 pos = float3(0.5, 0.5, -0.2); float trans = 1; float3 col = 0;\n"
        "  [loop] for (int k = 0; k < 24; ++k) { pos += dir * 0.045; if (all(pos > 0) && all(pos < 1)) { float dens = vol.SampleLevel(s, pos, 0); float a = 1 - exp(-dens * 0.35 * 4.0); col += trans * a * lerp(float3(1.0, 0.55, 0.3), float3(0.9, 0.9, 1.0), pos.y); trans *= 1 - a; } }\n"
        "  return float4(bg * trans + col, 1); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Target target;
    if (!fill || !march || !smp || !r.color(target, 256, 256)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(volume.get());
    Com<ID3D11ShaderResourceView> srv = r.srv(volume.get());
    if (!uav || !srv) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(fill.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(8, 8, 8);
    ID3D11UnorderedAccessView* none = nullptr;
    r.ctx->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
    pass(r, target, march.get(), {srv.get()}, smp.get());
    return r.shot(out, target);
});

const Add kDecals(spec("scene", "decals_projected_from_reconstructed_depth_position").size(256, 256).tolerance(rr::Tolerance::blend(4).with_edge(1)), [](Rig& r, Cap& out) {
    Stage st;
    Target color, view_z, result;
    Depth depth;
    if (!st.make(r) || !r.color(color, 256, 256) || !r.color(view_z, 256, 256, DXGI_FORMAT_R32_FLOAT) || !r.color(result, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11PixelShader> g_ps = r.ps(std::string(Hlsl3::kLitIn) +
        "struct PO { float4 c : SV_Target0; float4 z : SV_Target1; };\nPO main(VO i) { PO o; float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.3)))); float3 a = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.4)) : 1.0); o.c = float4(a * (0.25 + 0.75 * l), 1); o.z = float4(i.pos.w, 0, 0, 1); return o; }\n");
    const rr::Mat4 inv_view = rr::Mat4::translate(0.0f, 3.0f, 8.5f) * rr::Mat4::rotate_x(-0.5f);
    struct Decal {
        rr::Mat4 inv_view;
        float centre[4];
        float params[4];
    } cb{inv_view, {-0.6f, 0.0f, 1.8f, 0}, {1.0f / std::tan(0.5f), 0, 0, 0}};
    Com<ID3D11Buffer> decal_cb = r.cb(cb);
    Com<ID3D11PixelShader> decal = r.ps(std::string(kFsHead) +
        "Texture2D<float> zt : register(t0);\ncbuffer D : register(b0) { column_major float4x4 inv_view; float4 centre; float4 params; };\n"
        "float4 main(VO i) : SV_Target { float z = zt.Load(int3(i.p.xy, 0)); if (z > 100) discard; float2 ndc = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2); float3 vpos = float3(ndc.x * z / params.x, ndc.y * z / params.x, -z);\n"
        "  float3 w = mul(inv_view, float4(vpos, 1)).xyz; float3 local = (w - centre.xyz) / float3(1.6, 1.0, 1.6); if (any(abs(local) > 1)) discard;\n"
        "  float2 q = local.xz * 0.5 + 0.5; float ring = abs(length(local.xz) - 0.65); float chk = (int(floor(q.x * 6)) + int(floor(q.y * 6))) & 1; float a = (ring < 0.12 ? 1 : 0) * 0.85 + chk * 0.25 * step(length(local.xz), 0.5);\n"
        "  return float4(lerp(float3(0.9, 0.1, 0.1), float3(1, 0.9, 0.2), chk), a); }\n");
    D3D11_BLEND_DESC bd = bdesc();
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Com<ID3D11BlendState> blend = r.blend(bd);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!g_ps || !decal_cb || !decal || !blend || !ds) return false;
    ID3D11RenderTargetView* views[2] = {color.view.get(), view_z.view.get()};
    const float sky[4] = {0.55f, 0.7f, 0.85f, 1};
    const float far_value[4] = {1000.0f, 0, 0, 0};
    r.ctx->OMSetRenderTargets(2, views, depth.view.get());
    r.ctx->ClearRenderTargetView(views[0], sky);
    r.ctx->ClearRenderTargetView(views[1], far_value);
    r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    r.viewport(0, 0, 256, 256);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)).get());
    r.ctx->PSSetShader(g_ps.get(), nullptr, 0);
    st.draw(r, proj_matrix() * view_matrix());
    ID3D11RenderTargetView* none[2] = {};
    r.ctx->OMSetRenderTargets(2, none, nullptr);
    r.ctx->OMSetDepthStencilState(nullptr, 0);
    r.ctx->IASetInputLayout(nullptr);
    Com<ID3D11ShaderResourceView> zs = r.srv(view_z.texture.get());
    if (!zs) return false;
    r.ctx->CopyResource(result.texture.get(), color.texture.get());
    r.unbind_srvs();
    ID3D11RenderTargetView* rv = result.view.get();
    r.ctx->OMSetRenderTargets(1, &rv, nullptr);
    r.viewport(0, 0, 256, 256);
    ID3D11ShaderResourceView* v = zs.get();
    ID3D11Buffer* b = decal_cb.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetConstantBuffers(0, 1, &b);
    const float factor[4] = {};
    r.ctx->OMSetBlendState(blend.get(), factor, 0xffffffffu);
    r.draw_fs(decal.get());
    return r.shot(out, result) && r.shot(out, color, "scene");
});

const Add kOmni(spec("scene", "omnidirectional_shadow_cube_geometry_shader_six_faces").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Stage st;
    Target target;
    Depth depth;
    if (!st.make(r) || !r.color(target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11Texture2D> dist = r.tex(256, 256, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 6, 1, nullptr, D3D11_RESOURCE_MISC_TEXTURECUBE);
    Com<ID3D11Texture2D> face_depth = r.tex(256, 256, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL, 1, 6);
    if (!dist || !face_depth) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R32_FLOAT;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rd.Texture2DArray.ArraySize = 6;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dd.Texture2DArray.ArraySize = 6;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    sd.TextureCube.MipLevels = 1;
    Com<ID3D11RenderTargetView> rtv = r.rtv(dist.get(), &rd);
    Com<ID3D11DepthStencilView> dsv = r.dsv(face_depth.get(), &dd);
    Com<ID3D11ShaderResourceView> srv = r.srv(dist.get(), &sd);
    const float light[3] = {0.5f, 4.2f, 0.2f};
    const rr::Mat4 proj = rr::Mat4::perspective_d3d(1.5707963f, 1.0f, 0.2f, 30.0f);
    const rr::Mat4 faces[6] = {rr::Mat4::rotate_y(1.5707963f), rr::Mat4::rotate_y(-1.5707963f), rr::Mat4::rotate_x(-1.5707963f), rr::Mat4::rotate_x(1.5707963f), rr::Mat4::rotate_y(3.1415926f), rr::Mat4::identity()};
    struct Cube {
        rr::Mat4 vp[6];
        float light[4];
    } cube_data{};
    for (int f = 0; f < 6; ++f) cube_data.vp[f] = proj * faces[f] * rr::Mat4::translate(-light[0], -light[1], -light[2]);
    cube_data.light[0] = light[0];
    cube_data.light[1] = light[1];
    cube_data.light[2] = light[2];
    Com<ID3D11Buffer> cb = r.cb(cube_data);
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs("cbuffer Obj : register(b0) { column_major float4x4 vp; column_major float4x4 model; float4 color; float4 extra; };\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\nfloat4 main(VI i) : POSITION { return mul(model, float4(i.p, 1)); }\n", &code);
    Com<ID3D11GeometryShader> gs = r.gs(
        "cbuffer Cube : register(b1) { column_major float4x4 cvp[6]; float4 light; };\nstruct GO { float4 p : SV_Position; float3 w : WORLD; uint face : SV_RenderTargetArrayIndex; };\n"
        "[instance(6)][maxvertexcount(3)] void main(triangle float4 v[3] : POSITION, uint f : SV_GSInstanceID, inout TriangleStream<GO> s) { [unroll] for (int k = 0; k < 3; ++k) { GO o; o.w = v[k].xyz; o.p = mul(cvp[f], float4(v[k].xyz, 1)); o.face = f; s.Append(o); } }\n");
    Com<ID3D11PixelShader> dist_ps = r.ps("cbuffer Cube : register(b1) { column_major float4x4 cvp[6]; float4 light; };\nstruct GO { float4 p : SV_Position; float3 w : WORLD; uint face : SV_RenderTargetArrayIndex; };\nfloat4 main(GO i) : SV_Target { return length(i.w - light.xyz); }\n");
    Com<ID3D11PixelShader> lit_ps = r.ps(std::string(Hlsl3::kLitIn) +
        "TextureCube<float> shadow : register(t0);\nSamplerState s : register(s0);\ncbuffer Cube : register(b1) { column_major float4x4 cvp[6]; float4 light; };\n"
        "float4 main(VO i) : SV_Target { float3 L = light.xyz - i.w; float d = length(L); float occ = shadow.SampleLevel(s, -L, 0); float lit = d - 0.08 <= occ ? 1 : 0; float ndl = saturate(dot(normalize(i.n), L / d));\n"
        "  float3 a = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.4)) : 1.0); return float4(a * (0.12 + 1.3 * ndl * lit / (1 + 0.05 * d * d)), 1); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!rtv || !dsv || !srv || !cb || !vs || !gs || !dist_ps || !lit_ps || !smp || !ds) return false;
    st.pipe.bind(r);
    ID3D11RenderTargetView* v = rtv.get();
    const float far_value[4] = {100.0f, 0, 0, 0};
    r.ctx->OMSetRenderTargets(1, &v, dsv.get());
    r.ctx->ClearRenderTargetView(v, far_value);
    r.ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    r.viewport(0, 0, 256, 256);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)).get());
    ID3D11Buffer* c1 = cb.get();
    r.ctx->GSSetConstantBuffers(1, 1, &c1);
    r.ctx->PSSetConstantBuffers(1, 1, &c1);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->GSSetShader(gs.get(), nullptr, 0);
    r.ctx->PSSetShader(dist_ps.get(), nullptr, 0);
    Com<ID3D11InputLayout> layout = r.layout(lit_layout(), code);
    if (!layout) return false;
    r.ctx->IASetInputLayout(layout.get());
    const rr::Mat4 identity = rr::Mat4::identity();
    auto draw_scene = [&](const rr::Mat4& vp) {
        st.ground.bind(r);
        st.pipe.set(r, vp, identity, 0.7f, 0.7f, 0.65f, 1.0f);
        st.ground.draw(r);
        st.cube.bind(r);
        st.pipe.set(r, vp, trs(-2.2f, 1.0f, -0.5f, 1.0f, 1.0f, 1.0f, 0.5f), 0.85f, 0.3f, 0.25f);
        st.cube.draw(r);
        st.pipe.set(r, vp, trs(1.6f, 0.8f, 1.2f, 0.8f, 1.6f, 0.8f, -0.4f), 0.25f, 0.5f, 0.85f);
        st.cube.draw(r);
        st.sphere.bind(r);
        st.pipe.set(r, vp, trs(0.4f, 1.1f, -1.8f, 1.1f, 1.1f, 1.1f), 0.3f, 0.8f, 0.35f);
        st.sphere.draw(r);
    };
    draw_scene(identity);
    ID3D11RenderTargetView* none = nullptr;
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    r.ctx->OMSetRenderTargets(1, &none, nullptr);
    const float sky[4] = {0.05f, 0.06f, 0.1f, 1};
    r.bind(target, &depth, sky);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    ID3D11ShaderResourceView* sv = srv.get();
    ID3D11SamplerState* sp = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &sv);
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.ctx->PSSetShader(lit_ps.get(), nullptr, 0);
    st.pipe.bind(r);
    draw_scene(proj_matrix() * view_matrix());
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
