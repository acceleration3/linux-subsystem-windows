#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kPlanePs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "struct PO { float4 c : SV_Target; float d : SV_Depth; };\n"
    "cbuffer K : register(b0) { float4 k; };\n"
    "PO main(VO i) { PO o; float za = 0.2 + 0.6 * i.uv.x;\n"
    "  bool same = k.x < 0.5 || i.uv.y < 0.5; o.d = same ? za : 0.8 - 0.6 * i.uv.y;\n"
    "  if (k.x < 0.5) { o.c = float4(0.2, 0.4 + 0.4 * i.uv.y, 0.9, 1); }\n"
    "  else { o.c = float4(0.9, 0.5 * i.uv.x, 0.2 + 0.3 * i.uv.y, 1); }\n"
    "  return o; }\n";

bool depth_scene(Rig& r, Cap& out, DXGI_FORMAT format, D3D11_COMPARISON_FUNC func, bool write) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth, 128, 128, format)) return false;
    Com<ID3D11PixelShader> ps = r.ps(kPlanePs);
    Com<ID3D11DepthStencilState> first = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    Com<ID3D11DepthStencilState> second = r.dss(ddesc(true, func, write));
    Com<ID3D11DepthStencilState> third = r.dss(ddesc(true, D3D11_COMPARISON_LESS, false));
    const float background[4] = {0.9f, 0.9f, 0.9f, 1};
    Com<ID3D11Buffer> k = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    if (!ps || !first || !second || !third || !k) return false;
    r.bind(target, &depth, background);
    ID3D11Buffer* cb = k.get();
    r.ctx->PSSetConstantBuffers(0, 1, &cb);
    const float one[4] = {1, 0, 0, 0};
    const float zero[4] = {0, 0, 0, 0};
    r.ctx->UpdateSubresource(k.get(), 0, nullptr, zero, 0, 0);
    r.ctx->OMSetDepthStencilState(first.get(), 0);
    r.draw_fs(ps.get());
    r.ctx->UpdateSubresource(k.get(), 0, nullptr, one, 0, 0);
    r.ctx->OMSetDepthStencilState(second.get(), 0);
    r.draw_fs(ps.get());
    if (!write) {
        r.ctx->UpdateSubresource(k.get(), 0, nullptr, zero, 0, 0);
        r.ctx->OMSetDepthStencilState(third.get(), 0);
        r.draw_fs(ps.get());
    }
    rr::Image depth_image;
    if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
    out.add("depth", std::move(depth_image));
    return true;
}

struct DepthRegistrar {
    DepthRegistrar() {
        struct Fmt {
            const char* name;
            DXGI_FORMAT format;
        };
        static const Fmt formats[] = {{"d16_unorm", DXGI_FORMAT_D16_UNORM},
                                      {"d24_unorm_s8_uint", DXGI_FORMAT_D24_UNORM_S8_UINT},
                                      {"d32_float", DXGI_FORMAT_D32_FLOAT},
                                      {"d32_float_s8x24_uint", DXGI_FORMAT_D32_FLOAT_S8X24_UINT}};
        for (const Fmt& f : formats) {
            Add(spec("om", (std::string("depth_format_") + f.name).c_str()).tolerance(rr::Tolerance::blend(2).with_edge(2)),
                [&f](Rig& r, Cap& out) { return depth_scene(r, out, f.format, D3D11_COMPARISON_LESS_EQUAL, true); },
                [&f](Rig& r) { return need_format(r, f.format, D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, "depth stencil use"); });
        }
    }
};
const DepthRegistrar kDepthRegistrar;

const Add kReadOnlyDsv(spec("om", "depth_readonly_dsv_sampled").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth)) return false;
    CubePipe cubes;
    const char* sample_source =
        "Texture2D<float> zbuf : register(t0);\n"
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i) : SV_Target { float scene = zbuf.Load(int3(i.pos.xy, 0));\n"
        "  float fade = saturate((scene - i.pos.z) * 60.0); return float4(i.col * fade + float3(0.1, 0.1, 0.3) * (1 - fade), 1); }\n";
    CubePipe soft;
    if (!cubes.make(r) || !soft.make(r, sample_source, D3D11_CULL_NONE)) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    Com<ID3D11ShaderResourceView> srv = r.srv(depth.texture.get(), &sd);
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dd.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    Com<ID3D11DepthStencilView> readonly = r.dsv(depth.texture.get(), &dd);
    Com<ID3D11DepthStencilState> write = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    Com<ID3D11DepthStencilState> test = r.dss(ddesc(true, D3D11_COMPARISON_LESS_EQUAL, false));
    if (!srv || !readonly || !write || !test) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    r.ctx->OMSetDepthStencilState(write.get(), 0);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.7f, 0, 0) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.3f));
    ID3D11RenderTargetView* rtv = target.view.get();
    r.ctx->OMSetRenderTargets(1, &rtv, readonly.get());
    soft.bind(r);
    ID3D11ShaderResourceView* view = srv.get();
    r.ctx->PSSetShaderResources(0, 1, &view);
    r.ctx->OMSetDepthStencilState(test.get(), 0);
    soft.draw(r, CubePipe::camera() * rr::Mat4::translate(0.2f, 0.1f, 0.5f) * rr::Mat4::rotate_y(-0.5f) * rr::Mat4::rotate_z(0.4f) * rr::Mat4::scale(1.3f, 1.3f, 1.3f));
    return r.shot(out, target);
});

const Add kPrepass(spec("om", "depth_prepass_then_equal").tolerance(rr::Tolerance::blend(2).with_edge(2)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth)) return false;
    CubePipe cubes;
    const char* shade =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i) : SV_Target { float v = 0; [unroll] for (int k = 0; k < 8; ++k) v += sin(i.pos.x * 0.2 + k) * cos(i.pos.y * 0.25 + k);\n"
        "  return float4(i.col * (0.75 + 0.03 * v), 1); }\n";
    if (!cubes.make(r, shade)) return false;
    Com<ID3D11DepthStencilState> pre = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    Com<ID3D11DepthStencilState> equal = r.dss(ddesc(true, D3D11_COMPARISON_EQUAL, false));
    if (!pre || !equal) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    const rr::Mat4 a = CubePipe::camera() * rr::Mat4::translate(-0.6f, 0, 0) * rr::Mat4::rotate_y(0.7f) * rr::Mat4::rotate_x(0.4f);
    const rr::Mat4 b = CubePipe::camera() * rr::Mat4::translate(0.6f, 0.2f, 0.3f) * rr::Mat4::rotate_y(-0.8f) * rr::Mat4::rotate_z(0.5f);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    r.ctx->OMSetDepthStencilState(pre.get(), 0);
    cubes.draw(r, a);
    cubes.draw(r, b);
    r.ctx->PSSetShader(cubes.ps_.get(), nullptr, 0);
    r.ctx->OMSetDepthStencilState(equal.get(), 0);
    cubes.draw(r, a);
    cubes.draw(r, b);
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
