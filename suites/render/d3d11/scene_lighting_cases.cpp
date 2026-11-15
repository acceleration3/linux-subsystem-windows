#include "render/d3d11/scene3d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

rr::Mat4 camera_vp() { return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 60.0f) * rr::Mat4::rotate_x(0.5f) * rr::Mat4::translate(0.0f, -3.0f, -8.5f); }

struct World {
    LitPipe pipe;
    GpuMesh cube, sphere, ground;
    bool make(Rig& r) {
        return pipe.make(r) && cube.make(r, LitMesh::cube()) && sphere.make(r, LitMesh::sphere(24, 16)) && ground.make(r, LitMesh::plane(6.0f));
    }
    template <class F>
    void each(Rig& r, const rr::Mat4& vp, F&& fn) {
        pipe.bind(r);
        ground.bind(r);
        pipe.set(r, vp, rr::Mat4::identity(), 0.7f, 0.7f, 0.65f, 1.0f);
        fn(0);
        ground.draw(r);
        cube.bind(r);
        pipe.set(r, vp, trs(-2.2f, 1.0f, -0.5f, 1.0f, 1.0f, 1.0f, 0.5f), 0.85f, 0.3f, 0.25f);
        fn(1);
        cube.draw(r);
        pipe.set(r, vp, trs(1.6f, 0.8f, 1.2f, 0.8f, 1.6f, 0.8f, -0.4f), 0.25f, 0.5f, 0.85f);
        fn(2);
        cube.draw(r);
        sphere.bind(r);
        pipe.set(r, vp, trs(0.4f, 1.1f, -1.8f, 1.1f, 1.1f, 1.1f), 0.3f, 0.8f, 0.35f);
        fn(3);
        sphere.draw(r);
    }
};

rr::Mat4 light_vp(float yaw, float pitch, float half) { return Light::ortho(half, 1.0f, 30.0f) * rr::Mat4::translate(0, 0, -14.0f) * rr::Mat4::rotate_x(pitch) * rr::Mat4::rotate_y(yaw); }

const char* const kShadowPs =
    "Texture2D<float> shadow : register(t0);\nSamplerComparisonState cmp : register(s0);\n"
    "cbuffer Light : register(b1) { column_major float4x4 lvp; float4 ldir; };\n"
    "float4 main(VO i) : SV_Target { float4 lp = mul(lvp, float4(i.w, 1)); float3 sc = float3(lp.xy * float2(0.5, -0.5) + 0.5, lp.z - 0.0015);\n"
    "  float lit = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) lit += shadow.SampleCmpLevelZero(cmp, sc.xy + float2(x, y) / 1024.0, sc.z); lit /= 9.0;\n"
    "  float3 albedo = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.5)) : 1.0);\n"
    "  float ndl = saturate(dot(normalize(i.n), ldir.xyz)); return float4(albedo * (0.18 + 0.82 * ndl * lit), 1); }\n";

const Add kShadow(spec("scene", "shadow_mapping_pcf_comparison_sampler").size(256, 256).tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    World w;
    Target target;
    Depth depth, shadow;
    if (!w.make(r) || !r.color(target, 256, 256) || !r.depth(depth, 256, 256) || !r.depth(shadow, 1024, 1024)) return false;
    const rr::Mat4 lvp = light_vp(0.6f, 0.95f, 8.0f);
    struct LightData {
        rr::Mat4 m;
        float dir[4];
    } ld{lvp, {0.0f, 0.0f, 0.0f, 0}};
    const rr::Mat4 inv = rr::Mat4::rotate_y(-0.6f) * rr::Mat4::rotate_x(-0.95f);
    ld.dir[0] = inv.m[8];
    ld.dir[1] = inv.m[9];
    ld.dir[2] = inv.m[10];
    Com<ID3D11Buffer> lb = r.cb(ld);
    D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK);
    rd.DepthBias = 40;
    rd.SlopeScaledDepthBias = 1.5f;
    Com<ID3D11RasterizerState> depth_rs = r.rast(rd);
    Com<ID3D11RasterizerState> main_rs = r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    D3D11_SAMPLER_DESC cmp_desc = sdesc(D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER);
    cmp_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    cmp_desc.BorderColor[0] = cmp_desc.BorderColor[1] = cmp_desc.BorderColor[2] = cmp_desc.BorderColor[3] = 1.0f;
    Com<ID3D11SamplerState> cmp = r.smp(cmp_desc);
    Com<ID3D11PixelShader> ps = r.ps(std::string(Hlsl3::kLitIn) + kShadowPs);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    Com<ID3D11ShaderResourceView> srv = r.srv(shadow.texture.get(), &sd);
    if (!lb || !depth_rs || !main_rs || !ds || !cmp || !ps || !srv) return false;
    r.ctx->OMSetRenderTargets(0, nullptr, shadow.view.get());
    r.ctx->ClearDepthStencilView(shadow.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    r.viewport(0, 0, 1024, 1024);
    r.ctx->RSSetState(depth_rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    w.each(r, lvp, [](int) {});
    const float sky[4] = {0.45f, 0.6f, 0.8f, 1};
    r.bind(target, &depth, sky);
    r.ctx->RSSetState(main_rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    ID3D11Buffer* l = lb.get();
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = cmp.get();
    r.ctx->PSSetConstantBuffers(1, 1, &l);
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    w.each(r, camera_vp(), [](int) {});
    return r.shot(out, target);
});

const Add kCascades(spec("scene", "cascaded_shadow_maps_texture_array").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    World w;
    Target target;
    Depth depth;
    if (!w.make(r) || !r.color(target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11Texture2D> array = r.tex(512, 512, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 1, 3);
    if (!array) return false;
    struct Cascades {
        rr::Mat4 m[3];
        float dir[4];
        float split[4];
    } cd{};
    const float halves[3] = {4.0f, 7.0f, 12.0f};
    for (int c = 0; c < 3; ++c) cd.m[c] = light_vp(0.6f, 0.95f, halves[c]);
    const rr::Mat4 inv = rr::Mat4::rotate_y(-0.6f) * rr::Mat4::rotate_x(-0.95f);
    cd.dir[0] = inv.m[8];
    cd.dir[1] = inv.m[9];
    cd.dir[2] = inv.m[10];
    cd.split[0] = 8.0f;
    cd.split[1] = 11.0f;
    Com<ID3D11Buffer> cb = r.cb(cd);
    D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK);
    rd.DepthBias = 40;
    rd.SlopeScaledDepthBias = 1.5f;
    Com<ID3D11RasterizerState> depth_rs = r.rast(rd);
    Com<ID3D11RasterizerState> main_rs = r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    D3D11_SAMPLER_DESC cmp_desc = sdesc(D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER);
    cmp_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    cmp_desc.BorderColor[0] = cmp_desc.BorderColor[1] = cmp_desc.BorderColor[2] = cmp_desc.BorderColor[3] = 1.0f;
    Com<ID3D11SamplerState> cmp = r.smp(cmp_desc);
    const char* ps_source =
        "Texture2DArray<float> shadow : register(t0);\nSamplerComparisonState cmp : register(s0);\n"
        "cbuffer Cas : register(b1) { column_major float4x4 lvp[3]; float4 ldir; float4 split; };\n"
        "float4 main(VO i) : SV_Target { float view_z = i.pos.w; uint c = view_z < split.x ? 0 : (view_z < split.y ? 1 : 2);\n"
        "  float4 lp = mul(lvp[c], float4(i.w, 1)); float3 sc = float3(lp.xy * float2(0.5, -0.5) + 0.5, lp.z - 0.002);\n"
        "  float lit = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) lit += shadow.SampleCmpLevelZero(cmp, float3(sc.xy + float2(x, y) / 512.0, c), sc.z); lit /= 9.0;\n"
        "  float3 tint = c == 0 ? float3(1, 0.9, 0.9) : (c == 1 ? float3(0.9, 1, 0.9) : float3(0.9, 0.9, 1));\n"
        "  float3 albedo = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.5)) : 1.0) * tint;\n"
        "  float ndl = saturate(dot(normalize(i.n), ldir.xyz)); return float4(albedo * (0.18 + 0.82 * ndl * lit), 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(std::string(Hlsl3::kLitIn) + ps_source);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels = 1;
    sd.Texture2DArray.ArraySize = 3;
    Com<ID3D11ShaderResourceView> srv = r.srv(array.get(), &sd);
    if (!cb || !depth_rs || !main_rs || !ds || !cmp || !ps || !srv) return false;
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    for (UINT c = 0; c < 3; ++c) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dd.Texture2DArray.FirstArraySlice = c;
        dd.Texture2DArray.ArraySize = 1;
        Com<ID3D11DepthStencilView> dsv = r.dsv(array.get(), &dd);
        if (!dsv) return false;
        r.ctx->OMSetRenderTargets(0, nullptr, dsv.get());
        r.ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        r.viewport(0, 0, 512, 512);
        r.ctx->RSSetState(depth_rs.get());
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        w.each(r, cd.m[c], [](int) {});
    }
    const float sky[4] = {0.45f, 0.6f, 0.8f, 1};
    r.bind(target, &depth, sky);
    r.ctx->RSSetState(main_rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    ID3D11Buffer* l = cb.get();
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = cmp.get();
    r.ctx->PSSetConstantBuffers(1, 1, &l);
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    w.each(r, camera_vp(), [](int) {});
    return r.shot(out, target);
});

const Add kDeferred(spec("scene", "deferred_shading_gbuffer_stencil_masked_lights").size(256, 256).tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    World w;
    Target albedo, normal, position, final_target;
    Depth depth;
    if (!w.make(r) || !r.color(albedo, 256, 256) || !r.color(normal, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(position, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !r.color(final_target, 256, 256) || !r.depth(depth, 256, 256, DXGI_FORMAT_D24_UNORM_S8_UINT))
        return false;
    const char* gps =
        "struct PO { float4 a : SV_Target0; float4 n : SV_Target1; float4 p : SV_Target2; };\n"
        "PO main(VO i) { PO o; float3 albedo = color.rgb * (extra.x > 0.5 ? (0.6 + 0.4 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.5)) : 1.0); o.a = float4(albedo, 1); o.n = float4(normalize(i.n), 1); o.p = float4(i.w, 1); return o; }\n";
    Com<ID3D11PixelShader> g_ps = r.ps(std::string(Hlsl3::kLitIn) + gps);
    const char* lps =
        "Texture2D<float4> albedo : register(t0);\nTexture2D<float4> nrm : register(t1);\nTexture2D<float4> wpos : register(t2);\n"
        "cbuffer L : register(b0) { float4 pos; float4 col; };\n"
        "float4 main(float4 p : SV_Position) : SV_Target { int3 c = int3(int2(p.xy), 0); float4 a = albedo.Load(c); float3 n = nrm.Load(c).xyz; float3 wp = wpos.Load(c).xyz;\n"
        "  float3 L = pos.xyz - wp; float d = length(L); float att = saturate(1.0 - d / pos.w); att *= att; float ndl = saturate(dot(n, L / d));\n"
        "  return float4(a.rgb * col.rgb * ndl * att * 2.4, 1); }\n";
    Com<ID3D11PixelShader> l_ps = r.ps(lps);
    Com<ID3D11PixelShader> amb_ps = r.ps("Texture2D<float4> albedo : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return float4(albedo.Load(int3(p.xy, 0)).rgb * 0.08, 1); }\n");
    if (!g_ps || !l_ps || !amb_ps) return false;
    D3D11_DEPTH_STENCIL_DESC gd = ddesc(true, D3D11_COMPARISON_LESS, true);
    gd.StencilEnable = TRUE;
    gd.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    gd.BackFace = gd.FrontFace;
    Com<ID3D11DepthStencilState> geometry_ds = r.dss(gd);
    D3D11_DEPTH_STENCIL_DESC ld = ddesc(false, D3D11_COMPARISON_ALWAYS, false);
    ld.StencilEnable = TRUE;
    ld.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL};
    ld.BackFace = ld.FrontFace;
    Com<ID3D11DepthStencilState> light_ds = r.dss(ld);
    D3D11_BLEND_DESC add = bdesc();
    add.RenderTarget[0].BlendEnable = TRUE;
    add.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    Com<ID3D11BlendState> additive = r.blend(add);
    D3D11_RASTERIZER_DESC sc = rdesc();
    sc.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> scissor_rs = r.rast(sc);
    Com<ID3D11RasterizerState> main_rs = r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK));
    Com<ID3D11ShaderResourceView> a_srv = r.srv(albedo.texture.get());
    Com<ID3D11ShaderResourceView> n_srv = r.srv(normal.texture.get());
    Com<ID3D11ShaderResourceView> p_srv = r.srv(position.texture.get());
    Com<ID3D11Buffer> cb = r.buf(32, D3D11_BIND_CONSTANT_BUFFER);
    if (!geometry_ds || !light_ds || !additive || !scissor_rs || !main_rs || !a_srv || !n_srv || !p_srv || !cb) return false;
    const float dark[4] = {0, 0, 0, 0};
    ID3D11RenderTargetView* gv[3] = {albedo.view.get(), normal.view.get(), position.view.get()};
    r.ctx->OMSetRenderTargets(3, gv, depth.view.get());
    for (ID3D11RenderTargetView* v : gv) r.ctx->ClearRenderTargetView(v, dark);
    r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    r.viewport(0, 0, 256, 256);
    r.ctx->RSSetState(main_rs.get());
    r.ctx->OMSetDepthStencilState(geometry_ds.get(), 1);
    r.ctx->PSSetShader(g_ps.get(), nullptr, 0);
    const rr::Mat4 vp = camera_vp();
    w.each(r, vp, [](int) {});
    ID3D11RenderTargetView* none[3] = {};
    r.ctx->OMSetRenderTargets(3, none, nullptr);
    const float sky[4] = {0.03f, 0.04f, 0.07f, 1};
    ID3D11RenderTargetView* fv = final_target.view.get();
    r.ctx->OMSetRenderTargets(1, &fv, depth.view.get());
    r.ctx->ClearRenderTargetView(fv, sky);
    ID3D11ShaderResourceView* views[3] = {a_srv.get(), n_srv.get(), p_srv.get()};
    r.ctx->PSSetShaderResources(0, 3, views);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->RSSetState(scissor_rs.get());
    const D3D11_RECT full{0, 0, 256, 256};
    r.ctx->RSSetScissorRects(1, &full);
    r.ctx->OMSetDepthStencilState(light_ds.get(), 1);
    r.draw_fs(amb_ps.get());
    const float lights[4][8] = {{-3.2f, 2.0f, 1.0f, 5.5f, 1.0f, 0.4f, 0.3f, 1}, {2.8f, 1.6f, 2.8f, 5.0f, 0.3f, 0.5f, 1.0f, 1}, {0.2f, 1.2f, -3.0f, 5.5f, 0.4f, 1.0f, 0.4f, 1}, {-0.5f, 4.0f, 0.0f, 6.0f, 1.0f, 0.9f, 0.6f, 1}};
    ID3D11Buffer* b = cb.get();
    r.ctx->PSSetConstantBuffers(0, 1, &b);
    const float blend_factor[4] = {};
    for (const auto& l : lights) {
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, l, 0, 0);
        float cx = vp.m[0] * l[0] + vp.m[4] * l[1] + vp.m[8] * l[2] + vp.m[12];
        float cy = vp.m[1] * l[0] + vp.m[5] * l[1] + vp.m[9] * l[2] + vp.m[13];
        const float cw = vp.m[3] * l[0] + vp.m[7] * l[1] + vp.m[11] * l[2] + vp.m[15];
        cx = (cx / cw * 0.5f + 0.5f) * 256.0f;
        cy = (1.0f - (cy / cw * 0.5f + 0.5f)) * 256.0f;
        const float rad = l[3] * vp.m[0] / cw * 128.0f;
        const D3D11_RECT rect{std::max(0, static_cast<int>(cx - rad)), std::max(0, static_cast<int>(cy - rad)), std::min(256, static_cast<int>(cx + rad)), std::min(256, static_cast<int>(cy + rad))};
        if (rect.right <= rect.left || rect.bottom <= rect.top) continue;
        r.ctx->RSSetScissorRects(1, &rect);
        r.ctx->OMSetBlendState(additive.get(), blend_factor, 0xffffffffu);
        r.draw_fs(l_ps.get());
    }
    return r.shot(out, final_target);
});

const Add kBloom(spec("scene", "hdr_bloom_downsample_chain_tonemap").size(256, 256).tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    World w;
    Target hdr, half, quarter, eighth, blur_a, blur_b, result;
    Depth depth;
    if (!w.make(r) || !r.color(hdr, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(half, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(quarter, 64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !r.color(eighth, 32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(blur_a, 32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT) || !r.color(blur_b, 32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !r.color(result, 256, 256) || !r.depth(depth, 256, 256))
        return false;
    const char* emissive =
        "float4 main(VO i) : SV_Target { float3 n = normalize(i.n); float l = saturate(dot(n, normalize(float3(0.3, 0.8, 0.5)))); float glow = extra.y; return float4(color.rgb * (0.2 + 0.8 * l) + color.rgb * glow * 6.0, 1); }\n";
    Com<ID3D11PixelShader> scene_ps = r.ps(std::string(Hlsl3::kLitIn) + emissive);
    Com<ID3D11PixelShader> down = r.ps(
        "Texture2D t : register(t0);\nSamplerState s : register(s0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float2 o = float2(ddx(uv.x), ddy(uv.y)) * 0.5; float4 a = t.Sample(s, uv + o * float2(-1, -1)) + t.Sample(s, uv + o * float2(1, -1)) + t.Sample(s, uv + o * float2(-1, 1)) + t.Sample(s, uv + o * float2(1, 1)); return a * 0.25; }\n");
    Com<ID3D11PixelShader> bright = r.ps("Texture2D t : register(t0);\nSamplerState s : register(s0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float4 c = t.Sample(s, uv); return float4(max(c.rgb - 1.0, 0), 1); }\n");
    Com<ID3D11PixelShader> blur = r.ps(
        "Texture2D t : register(t0);\nSamplerState s : register(s0);\ncbuffer C : register(b0) { float4 dir; };\n"
        "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { static const float w[5] = {0.227, 0.194, 0.121, 0.054, 0.016}; float4 a = t.Sample(s, uv) * w[0];\n"
        "  [unroll] for (int k = 1; k < 5; ++k) a += (t.Sample(s, uv + dir.xy * k) + t.Sample(s, uv - dir.xy * k)) * w[k]; return a; }\n");
    Com<ID3D11PixelShader> tone = r.ps(
        "Texture2D hdr : register(t0);\nTexture2D bloom : register(t1);\nSamplerState s : register(s0);\n"
        "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float3 c = hdr.Sample(s, uv).rgb + bloom.Sample(s, uv).rgb * 1.5; c = c / (1.0 + c) * 1.35; return float4(pow(saturate(c), 1.0 / 2.2), 1); }\n");
    Com<ID3D11SamplerState> linear = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11Buffer> dir = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    Com<ID3D11DepthStencilState> off = r.dss(ddesc(false));
    if (!scene_ps || !down || !bright || !blur || !tone || !linear || !dir || !ds || !off) return false;
    const float black[4] = {0.02f, 0.02f, 0.03f, 1};
    r.bind(hdr, &depth, black);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->PSSetShader(scene_ps.get(), nullptr, 0);
    ID3D11SamplerState* sp = linear.get();
    r.ctx->PSSetSamplers(0, 1, &sp);
    const rr::Mat4 vp = camera_vp();
    w.pipe.bind(r);
    w.ground.bind(r);
    w.pipe.set(r, vp, rr::Mat4::identity(), 0.4f, 0.4f, 0.45f, 0, 0);
    w.ground.draw(r);
    w.sphere.bind(r);
    for (int k = 0; k < 4; ++k) {
        w.pipe.set(r, vp, trs(-3.0f + 2.0f * k, 0.9f, -0.5f + 0.4f * k, 0.8f, 0.8f, 0.8f), 0.9f, 0.5f + 0.1f * k, 0.9f - 0.2f * k, 0, k == 1 || k == 3 ? 1.0f : 0.0f);
        w.sphere.draw(r);
    }
    r.ctx->OMSetDepthStencilState(off.get(), 0);
    r.ctx->IASetInputLayout(nullptr);
    auto pass = [&](Target& to, ID3D11ShaderResourceView* from, ID3D11PixelShader* ps, UINT size) {
        r.unbind_srvs();
        r.bind(to);
        r.viewport(0, 0, static_cast<float>(size), static_cast<float>(size));
        r.ctx->PSSetShaderResources(0, 1, &from);
        r.draw_fs(ps);
    };
    Com<ID3D11ShaderResourceView> s_hdr = r.srv(hdr.texture.get());
    Com<ID3D11ShaderResourceView> s_half = r.srv(half.texture.get());
    Com<ID3D11ShaderResourceView> s_quarter = r.srv(quarter.texture.get());
    Com<ID3D11ShaderResourceView> s_eighth = r.srv(eighth.texture.get());
    Com<ID3D11ShaderResourceView> s_a = r.srv(blur_a.texture.get());
    Com<ID3D11ShaderResourceView> s_b = r.srv(blur_b.texture.get());
    if (!s_hdr || !s_half || !s_quarter || !s_eighth || !s_a || !s_b) return false;
    pass(half, s_hdr.get(), bright.get(), 128);
    pass(quarter, s_half.get(), down.get(), 64);
    pass(eighth, s_quarter.get(), down.get(), 32);
    ID3D11Buffer* b = dir.get();
    r.ctx->PSSetConstantBuffers(0, 1, &b);
    const float horizontal[4] = {1.0f / 32.0f, 0, 0, 0};
    const float vertical[4] = {0, 1.0f / 32.0f, 0, 0};
    r.ctx->UpdateSubresource(dir.get(), 0, nullptr, horizontal, 0, 0);
    pass(blur_a, s_eighth.get(), blur.get(), 32);
    r.ctx->UpdateSubresource(dir.get(), 0, nullptr, vertical, 0, 0);
    pass(blur_b, s_a.get(), blur.get(), 32);
    r.unbind_srvs();
    r.bind(result);
    r.viewport(0, 0, 256, 256);
    ID3D11ShaderResourceView* both[2] = {s_hdr.get(), s_b.get()};
    r.ctx->PSSetShaderResources(0, 2, both);
    r.draw_fs(tone.get());
    return r.shot(out, result);
});

const Add kWater(spec("scene", "water_refraction_copy_back_buffer_then_sample").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    World w;
    Target target;
    Depth depth;
    if (!w.make(r) || !r.color(target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Com<ID3D11Texture2D> copy = r.tex(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11PixelShader> lit = r.ps(std::string(Hlsl3::kLitIn) + "float4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.4)))); float3 a = color.rgb * (extra.x > 0.5 ? (0.5 + 0.5 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.4)) : 1.0); return float4(a * (0.25 + 0.75 * l), 1); }\n");
    Com<ID3D11PixelShader> water = r.ps(std::string(Hlsl3::kLitIn) +
        "Texture2D scene : register(t0);\nSamplerState s : register(s0);\n"
        "float4 main(VO i) : SV_Target { float2 uv = i.pos.xy / 256.0; float2 wave = float2(sin(i.w.x * 3.1 + i.w.z * 1.7), cos(i.w.z * 2.9 - i.w.x * 1.3)) * 0.018;\n"
        "  float3 refr = scene.Sample(s, uv + wave).rgb; float fres = pow(1.0 - saturate(normalize(float3(0, 1, 0.9)).y), 2.0); return float4(lerp(refr * float3(0.45, 0.75, 0.85), float3(0.5, 0.7, 0.9), 0.25 + fres), 1); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    GpuMesh pool;
    if (!lit || !water || !smp || !ds || !copy || !pool.make(r, LitMesh::plane(4.0f, 0.55f))) return false;
    Com<ID3D11ShaderResourceView> srv = r.srv(copy.get());
    if (!srv) return false;
    const float sky[4] = {0.5f, 0.65f, 0.85f, 1};
    r.bind(target, &depth, sky);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->PSSetShader(lit.get(), nullptr, 0);
    w.each(r, camera_vp(), [](int) {});
    r.ctx->CopyResource(copy.get(), target.texture.get());
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* sp = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.ctx->PSSetShader(water.get(), nullptr, 0);
    w.pipe.bind(r);
    pool.bind(r);
    w.pipe.set(r, camera_vp(), rr::Mat4::identity(), 0.2f, 0.4f, 0.6f);
    pool.draw(r);
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
