#include "render/d3d11/scene3d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

struct SwapLoop {
    Depth depth;
    CubePipe cubes;
    Com<ID3D11DepthStencilState> ds;
    Com<ID3D11Texture2D> last;
    std::string needs(Rig& r) { return r.kit.swap.chain() == nullptr ? std::string("a swap chain") : std::string(); }
    bool init(Rig& r) {
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        return ds && r.depth(depth) && cubes.make(r);
    }
    bool step(Rig& r, unsigned frame) {
        Com<ID3D11Texture2D> back;
        if (!r.ok(r.kit.swap.chain()->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()), "GetBuffer")) return false;
        Com<ID3D11RenderTargetView> rtv = r.rtv(back.get());
        if (!rtv) return false;
        ID3D11RenderTargetView* v = rtv.get();
        const float clear[4] = {0.05f * frame, 0.08f, 0.14f, 1};
        r.ctx->OMSetRenderTargets(1, &v, depth.view.get());
        r.ctx->ClearRenderTargetView(v, clear);
        r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        r.viewport(0, 0, 128, 128);
        cubes.bind(r);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.7f, 0, 0) * rr::Mat4::rotate_y(0.5f * frame) * rr::Mat4::rotate_x(0.4f));
        cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(0.8f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.6f * frame) * rr::Mat4::rotate_z(0.5f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f));
        last = std::move(back);
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) {
        rr::Image image;
        if (!r.read_image(last.get(), DXGI_FORMAT_B8G8R8A8_UNORM, 0, image)) return false;
        out.add("", std::move(image));
        return true;
    }
};
const AddState<SwapLoop> kSwap(spec("scene", "flip_model_swap_chain_back_buffer_render_loop").frames(6, {0, 2, 5}).swapchain().tolerance(rr::Tolerance::raster()));

struct CubeReflect {
    Com<ID3D11Texture2D> cube;
    Com<ID3D11ShaderResourceView> srv;
    Com<ID3D11RenderTargetView> faces[6];
    Depth face_depth;
    Target target;
    Depth depth;
    CubePipe cubes;
    LitPipe lit;
    GpuMesh sphere;
    Com<ID3D11PixelShader> reflect;
    Com<ID3D11SamplerState> smp;
    Com<ID3D11DepthStencilState> ds;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        cube = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 6, 1, nullptr, D3D11_RESOURCE_MISC_TEXTURECUBE);
        if (!cube) return false;
        for (UINT f = 0; f < 6; ++f) {
            D3D11_RENDER_TARGET_VIEW_DESC d{};
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.FirstArraySlice = f;
            d.Texture2DArray.ArraySize = 1;
            faces[f] = r.rtv(cube.get(), &d);
            if (!faces[f]) return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sd.TextureCube.MipLevels = 1;
        srv = r.srv(cube.get(), &sd);
        reflect = r.ps(std::string(Hlsl3::kLitIn) + "TextureCube env : register(t0);\nSamplerState s : register(s0);\nfloat4 main(VO i) : SV_Target { float3 v = normalize(i.w - float3(0, 1.5, 7)); float3 r = reflect(v, normalize(i.n)); return float4(env.Sample(s, r).rgb * 0.9 + 0.05, 1); }\n");
        smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        return srv && reflect && smp && ds && r.depth(face_depth, 64, 64) && r.color(target) && r.depth(depth) && cubes.make(r) && lit.make(r) && sphere.make(r, LitMesh::sphere(24, 16));
    }
    void scene(Rig& r, const rr::Mat4& view_proj, unsigned frame) {
        cubes.bind(r);
        const float t = 0.5f * frame;
        cubes.draw(r, view_proj * rr::Mat4::translate(3.0f * std::cos(t), 0.4f * std::sin(t * 1.5f), 3.0f * std::sin(t)) * rr::Mat4::rotate_y(t) * rr::Mat4::scale(0.7f, 0.7f, 0.7f));
        cubes.draw(r, view_proj * rr::Mat4::translate(-4.0f, 1.0f, -3.0f) * rr::Mat4::rotate_y(0.7f) * rr::Mat4::scale(0.9f, 1.8f, 0.9f));
        cubes.draw(r, view_proj * rr::Mat4::translate(2.0f, -1.2f, -4.5f) * rr::Mat4::rotate_x(0.5f));
        cubes.draw(r, view_proj * rr::Mat4::translate(0.0f, 3.0f, 0.0f) * rr::Mat4::rotate_z(t) * rr::Mat4::scale(0.6f, 0.6f, 0.6f));
    }
    bool step(Rig& r, unsigned frame) {
        const rr::Mat4 proj = rr::Mat4::perspective_d3d(1.5707963f, 1.0f, 0.3f, 40.0f);
        const rr::Mat4 views[6] = {rr::Mat4::rotate_y(1.5707963f), rr::Mat4::rotate_y(-1.5707963f), rr::Mat4::rotate_x(-1.5707963f), rr::Mat4::rotate_x(1.5707963f), rr::Mat4::rotate_y(3.1415926f), rr::Mat4::identity()};
        for (int f = 0; f < 6; ++f) {
            ID3D11RenderTargetView* v = faces[f].get();
            const float clear[4] = {0.08f + 0.02f * f, 0.1f, 0.16f, 1};
            r.ctx->OMSetRenderTargets(1, &v, face_depth.view.get());
            r.ctx->ClearRenderTargetView(v, clear);
            r.ctx->ClearDepthStencilView(face_depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            r.viewport(0, 0, 64, 64);
            r.ctx->OMSetDepthStencilState(ds.get(), 0);
            scene(r, proj * views[f], frame);
        }
        ID3D11RenderTargetView* none = nullptr;
        r.ctx->OMSetRenderTargets(1, &none, nullptr);
        const float bg[4] = {0.12f, 0.12f, 0.18f, 1};
        r.bind(target, &depth, bg);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        const rr::Mat4 vp = rr::Mat4::perspective_d3d(0.9f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, -1.5f, -7.0f);
        scene(r, vp, frame);
        lit.bind(r);
        sphere.bind(r);
        lit.set(r, vp, trs(0, 1.5f, 0, 1.4f, 1.4f, 1.4f), 1, 1, 1);
        ID3D11ShaderResourceView* v = srv.get();
        ID3D11SamplerState* s = smp.get();
        r.ctx->PSSetShaderResources(0, 1, &v);
        r.ctx->PSSetSamplers(0, 1, &s);
        r.ctx->PSSetShader(reflect.get(), nullptr, 0);
        sphere.draw(r);
        r.unbind_srvs();
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<CubeReflect> kReflect(spec("scene", "dynamic_cube_map_reflections_six_faces_per_frame").frames(6, {0, 3, 5}).tolerance(rr::Tolerance::psnr(24)));

struct Skinned {
    Target target;
    Depth depth;
    GpuMesh tube;
    Com<ID3D11Buffer> bones, cb;
    Com<ID3D11ShaderResourceView> bone_srv;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11InputLayout> il;
    Com<ID3D11DepthStencilState> ds;
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        LitMesh m;
        const int segs = 16, rings = 24;
        for (int y = 0; y <= rings; ++y)
            for (int x = 0; x <= segs; ++x) {
                const float a = x * 6.2831853f / segs;
                const float h = y / static_cast<float>(rings);
                LitVertex v{};
                v.p[0] = 0.5f * std::cos(a);
                v.p[1] = h * 4.0f - 2.0f;
                v.p[2] = 0.5f * std::sin(a);
                v.n[0] = std::cos(a);
                v.n[2] = std::sin(a);
                v.uv[0] = x / static_cast<float>(segs);
                v.uv[1] = h;
                m.vertices.push_back(v);
            }
        for (int y = 0; y < rings; ++y)
            for (int x = 0; x < segs; ++x) {
                const std::uint32_t a = y * (segs + 1) + x, b = a + segs + 1;
                m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        Bytes code;
        vs = r.vs("cbuffer F : register(b0) { column_major float4x4 vp; };\nStructuredBuffer<float4x4> bones : register(t0);\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                  "struct VO { float4 pos : SV_Position; float3 n : NORMAL; float h : H; };\n"
                  "VO main(VI i) { VO o; float w = smoothstep(0.35, 0.65, i.uv.y); float4 a = mul(bones[0], float4(i.p, 1)); float4 b = mul(bones[1], float4(i.p, 1)); float3 na = mul((float3x3)bones[0], i.n); float3 nb = mul((float3x3)bones[1], i.n);\n"
                  "  o.pos = mul(vp, lerp(a, b, w)); o.n = lerp(na, nb, w); o.h = i.uv.y; return o; }\n", &code);
        ps = r.ps("struct VO { float4 pos : SV_Position; float3 n : NORMAL; float h : H; };\nfloat4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.6, 0.7)))); return float4(lerp(float3(0.9, 0.35, 0.25), float3(0.25, 0.5, 0.9), i.h) * (0.25 + 0.75 * l), 1); }\n");
        il = r.layout(lit_layout(), code);
        bones = r.buf(128, D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 64);
        const rr::Mat4 vp = rr::Mat4::perspective_d3d(0.9f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -7.0f) * rr::Mat4::rotate_y(0.5f);
        cb = r.cbuf(vp.m, 64);
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        if (!tube.make(r, m) || !vs || !ps || !il || !bones || !cb || !ds || !r.color(target) || !r.depth(depth)) return false;
        bone_srv = D3d11Tools::structured_srv(r.kit.dev, bones.get(), 0, 2);
        return static_cast<bool>(bone_srv);
    }
    bool step(Rig& r, unsigned frame) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!r.ok(r.ctx->Map(bones.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map bones")) return false;
        const float bend = 0.18f * static_cast<float>(frame);
        const rr::Mat4 b0 = rr::Mat4::identity();
        const rr::Mat4 b1 = rr::Mat4::translate(0.6f * std::sin(bend), 0.0f, 0.0f) * rr::Mat4::translate(0, 0.6f, 0) * rr::Mat4::rotate_z(-bend) * rr::Mat4::translate(0, -0.6f, 0);
        std::memcpy(mapped.pData, b0.m, 64);
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + 64, b1.m, 64);
        r.ctx->Unmap(bones.get(), 0);
        const float bg[4] = {0.1f, 0.1f, 0.14f, 1};
        r.bind(target, &depth, bg);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
        r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_NONE)).get());
        ID3D11Buffer* c = cb.get();
        ID3D11ShaderResourceView* v = bone_srv.get();
        r.ctx->IASetInputLayout(il.get());
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->VSSetConstantBuffers(0, 1, &c);
        r.ctx->VSSetShaderResources(0, 1, &v);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        tube.bind(r);
        tube.draw(r);
        r.unbind_srvs();
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<Skinned> kSkinned(spec("scene", "skinned_mesh_bone_matrices_dynamic_structured_buffer").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::raster()));

const Add kShadowVolume(spec("scene", "stencil_shadow_volume_zfail").size(256, 256).tolerance(rr::Tolerance::raster(3, 1)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    LitPipe pipe;
    GpuMesh ground, cube;
    LitMesh cube_mesh = LitMesh::cube();
    if (!r.color(target, 256, 256) || !r.depth(depth, 256, 256, DXGI_FORMAT_D24_UNORM_S8_UINT) || !pipe.make(r) || !ground.make(r, LitMesh::plane(7.0f)) || !cube.make(r, cube_mesh)) return false;
    const float light[3] = {0.45f, -0.8f, 0.35f};
    const float ll = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    const float L[3] = {light[0] / ll, light[1] / ll, light[2] / ll};
    const rr::Mat4 model = trs(-0.5f, 1.2f, 0.0f, 1.0f, 1.2f, 1.0f, 0.6f);
    auto world = [&](const LitVertex& v, float out_p[3]) {
        for (int k = 0; k < 3; ++k) out_p[k] = model.m[k] * v.p[0] + model.m[4 + k] * v.p[1] + model.m[8 + k] * v.p[2] + model.m[12 + k];
    };
    struct Tri {
        float p[3][3];
        float n[3];
        bool lit;
    };
    std::vector<Tri> tris;
    for (std::size_t i = 0; i + 2 < cube_mesh.indices.size(); i += 3) {
        Tri t{};
        for (int k = 0; k < 3; ++k) world(cube_mesh.vertices[cube_mesh.indices[i + k]], t.p[k]);
        float e1[3], e2[3];
        for (int k = 0; k < 3; ++k) {
            e1[k] = t.p[1][k] - t.p[0][k];
            e2[k] = t.p[2][k] - t.p[0][k];
        }
        t.n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        t.n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        t.n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        t.lit = t.n[0] * -L[0] + t.n[1] * -L[1] + t.n[2] * -L[2] > 0;
        tris.push_back(t);
    }
    constexpr float extrude = 14.0f;
    const float centre[3] = {-0.5f + L[0] * extrude * 0.5f, 1.2f + L[1] * extrude * 0.5f, L[2] * extrude * 0.5f};
    std::vector<LitVertex> volume;
    auto emit = [&](const float a[3], const float b[3], const float c[3]) {
        float e1[3], e2[3], n[3], m[3];
        for (int k = 0; k < 3; ++k) {
            e1[k] = b[k] - a[k];
            e2[k] = c[k] - a[k];
            m[k] = (a[k] + b[k] + c[k]) / 3 - centre[k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const bool flip = n[0] * m[0] + n[1] * m[1] + n[2] * m[2] < 0;
        const float* order[3] = {a, flip ? c : b, flip ? b : c};
        for (const float* p : order) {
            LitVertex v{};
            for (int k = 0; k < 3; ++k) v.p[k] = p[k];
            volume.push_back(v);
        }
    };
    auto far_point = [&](const float p[3], float out_p[3]) {
        for (int k = 0; k < 3; ++k) out_p[k] = p[k] + L[k] * extrude;
    };
    auto same = [](const float a[3], const float b[3]) { return std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]) < 1e-4f; };
    for (const Tri& t : tris) {
        if (!t.lit) continue;
        emit(t.p[0], t.p[1], t.p[2]);
        float f[3][3];
        for (int k = 0; k < 3; ++k) far_point(t.p[k], f[k]);
        emit(f[0], f[1], f[2]);
        for (int e = 0; e < 3; ++e) {
            const float* a = t.p[e];
            const float* b = t.p[(e + 1) % 3];
            bool shared_with_lit = false;
            for (const Tri& o : tris) {
                if (&o == &t || !o.lit) continue;
                for (int k = 0; k < 3; ++k)
                    if (same(o.p[k], b) && same(o.p[(k + 1) % 3], a)) shared_with_lit = true;
            }
            if (shared_with_lit) continue;
            float fa[3], fb[3];
            far_point(a, fa);
            far_point(b, fb);
            emit(a, b, fb);
            emit(a, fb, fa);
        }
    }
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs("cbuffer F : register(b0) { column_major float4x4 vp; };\nfloat4 main(float3 p : POSITION) : SV_Position { return mul(vp, float4(p, 1)); }\n", &code);
    Com<ID3D11InputLayout> volume_layout = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0)}, code);
    Com<ID3D11Buffer> vb = r.buf(static_cast<UINT>(volume.size() * sizeof(LitVertex)), D3D11_BIND_VERTEX_BUFFER, volume.data());
    const rr::Mat4 vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 60.0f) * rr::Mat4::rotate_x(0.55f) * rr::Mat4::translate(0.0f, -3.2f, -9.5f);
    Com<ID3D11Buffer> vcb = r.cbuf(vp.m, 64);
    const char* ambient = "float4 main(VO i) : SV_Target { float3 a = color.rgb * (extra.x > 0.5 ? (0.7 + 0.3 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.6)) : 1.0); return float4(a * 0.22, 1); }\n";
    const char* lit_source = "cbuffer L : register(b1) { float4 ldir; };\nfloat4 main(VO i) : SV_Target { float3 a = color.rgb * (extra.x > 0.5 ? (0.7 + 0.3 * (((int(floor(i.w.x)) + int(floor(i.w.z))) & 1) ? 1 : 0.6)) : 1.0); return float4(a * 0.85 * saturate(dot(normalize(i.n), -ldir.xyz)), 1); }\n";
    Com<ID3D11PixelShader> ambient_ps = r.ps(std::string(Hlsl3::kLitIn) + ambient);
    Com<ID3D11PixelShader> lit_ps = r.ps(std::string(Hlsl3::kLitIn) + lit_source);
    Com<ID3D11Buffer> lb = r.cbuf(L, 12);
    D3D11_DEPTH_STENCIL_DESC volume_desc = ddesc(true, D3D11_COMPARISON_LESS, false);
    volume_desc.StencilEnable = TRUE;
    volume_desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_DECR_SAT, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    volume_desc.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR_SAT, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    D3D11_DEPTH_STENCIL_DESC lit_desc = ddesc(true, D3D11_COMPARISON_LESS_EQUAL, false);
    lit_desc.StencilEnable = TRUE;
    lit_desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL};
    lit_desc.BackFace = lit_desc.FrontFace;
    D3D11_DEPTH_STENCIL_DESC base_desc = ddesc(true, D3D11_COMPARISON_LESS, true);
    Com<ID3D11DepthStencilState> base_ds = r.dss(base_desc);
    Com<ID3D11DepthStencilState> volume_ds = r.dss(volume_desc);
    Com<ID3D11DepthStencilState> lit_ds = r.dss(lit_desc);
    D3D11_BLEND_DESC masked = bdesc();
    masked.RenderTarget[0].RenderTargetWriteMask = 0;
    Com<ID3D11BlendState> no_color = r.blend(masked);
    D3D11_BLEND_DESC add = bdesc();
    add.RenderTarget[0].BlendEnable = TRUE;
    add.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    Com<ID3D11BlendState> additive = r.blend(add);
    Com<ID3D11RasterizerState> none_rs = r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_NONE));
    Com<ID3D11RasterizerState> back_rs = r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK));
    if (!vs || !volume_layout || !vb || !vcb || !ambient_ps || !lit_ps || !lb || !base_ds || !volume_ds || !lit_ds || !no_color || !additive || !none_rs || !back_rs) return false;
    const float sky[4] = {0.0f, 0.0f, 0.0f, 1};
    r.bind(target, &depth, sky);
    r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    r.ctx->RSSetState(back_rs.get());
    auto scene = [&](ID3D11PixelShader* ps) {
        r.ctx->PSSetShader(ps, nullptr, 0);
        pipe.bind(r);
        ground.bind(r);
        pipe.set(r, vp, rr::Mat4::identity(), 0.75f, 0.75f, 0.7f, 1.0f);
        ground.draw(r);
        cube.bind(r);
        pipe.set(r, vp, model, 0.85f, 0.35f, 0.3f);
        cube.draw(r);
    };
    r.ctx->OMSetDepthStencilState(base_ds.get(), 0);
    scene(ambient_ps.get());
    ID3D11Buffer* v = vcb.get();
    const UINT stride = sizeof(LitVertex), offset = 0;
    ID3D11Buffer* b = vb.get();
    r.ctx->OMSetBlendState(no_color.get(), nullptr, 0xffffffffu);
    r.ctx->OMSetDepthStencilState(volume_ds.get(), 0);
    r.ctx->RSSetState(none_rs.get());
    r.ctx->IASetInputLayout(volume_layout.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &v);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    r.ctx->Draw(static_cast<UINT>(volume.size()), 0);
    ID3D11Buffer* l = lb.get();
    r.ctx->PSSetConstantBuffers(1, 1, &l);
    const float factor[4] = {};
    r.ctx->OMSetBlendState(additive.get(), factor, 0xffffffffu);
    r.ctx->OMSetDepthStencilState(lit_ds.get(), 0);
    r.ctx->RSSetState(back_rs.get());
    scene(lit_ps.get());
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
