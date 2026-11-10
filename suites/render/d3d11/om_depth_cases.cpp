#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {
rr::Tolerance depth_tolerance_for(DXGI_FORMAT format, float absolute) {
    rr::Tolerance t = rr::Tolerance::floats(absolute);
    if (format == DXGI_FORMAT_D16_UNORM) {
        t.absolute = 1.6e-5f;
        t = t.with_edge(1).with_outliers(0.005);
    }
    return t;
}


const char* const kPlanePs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "struct PO { float4 c : SV_Target; float d : SV_Depth; };\n"
    "cbuffer K : register(b0) { float4 k; };\n"
    "PO main(VO i) { PO o; float za = 0.2 + 0.6 * i.uv.x;\n"
    "  bool same = k.x < 0.5 || i.uv.y < 0.5; o.d = same ? za : 0.8 - 0.6 * i.uv.y;\n"
    "  if (k.x < 0.5) { o.c = float4(0.2, 0.4 + 0.4 * i.uv.y, 0.9, 1); }\n"
    "  else { o.c = float4(0.9, 0.5 * i.uv.x, 0.2 + 0.3 * i.uv.y, 1); }\n"
    "  return o; }\n";

bool depth_scene(Rig& r, Cap& out, DXGI_FORMAT format, D3D11_COMPARISON_FUNC func, bool write, bool stencil = false) {
    Target target;
    Depth depth;
    const bool shrink = r.broken("depth_16bit") && format != DXGI_FORMAT_D16_UNORM;
    if (!r.color(target) || !r.depth(depth, 128, 128, shrink ? DXGI_FORMAT_D16_UNORM : format)) return false;
    Com<ID3D11PixelShader> ps = r.ps(kPlanePs);
    D3D11_DEPTH_STENCIL_DESC d1 = ddesc(true, D3D11_COMPARISON_LESS, true);
    D3D11_DEPTH_STENCIL_DESC d2 = ddesc(true, func, write);
    if (stencil) {
        const D3D11_DEPTH_STENCILOP_DESC op = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, r.broken("stencil_lost") ? D3D11_STENCIL_OP_KEEP : D3D11_STENCIL_OP_REPLACE,
                                              D3D11_COMPARISON_ALWAYS};
        for (D3D11_DEPTH_STENCIL_DESC* d : {&d1, &d2}) {
            d->StencilEnable = TRUE;
            d->StencilReadMask = 0xFF;
            d->StencilWriteMask = 0xFF;
            d->FrontFace = op;
            d->BackFace = op;
        }
    }
    Com<ID3D11DepthStencilState> first = r.dss(d1);
    Com<ID3D11DepthStencilState> second = r.dss(d2);
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
    r.ctx->OMSetDepthStencilState(first.get(), 37);
    r.draw_fs(ps.get());
    r.ctx->UpdateSubresource(k.get(), 0, nullptr, one, 0, 0);
    r.ctx->OMSetDepthStencilState(second.get(), 201);
    r.draw_fs(ps.get());
    if (!write) {
        r.ctx->UpdateSubresource(k.get(), 0, nullptr, zero, 0, 0);
        r.ctx->OMSetDepthStencilState(third.get(), 0);
        r.draw_fs(ps.get());
    }
    rr::Image depth_image;
    if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
    out.add("depth", std::move(depth_image));
    if (stencil) {
        rr::Image stencil_image;
        if (!r.kit.read_stencil(depth, stencil_image, r.error)) return false;
        out.add("stencil", std::move(stencil_image));
    }
    return true;
}

struct DepthRegistrar {
    DepthRegistrar() {
        struct Fmt {
            const char* name;
            DXGI_FORMAT format;
            bool stencil;
            float depth_tolerance;
            std::vector<std::string> same;
        };
        static const Fmt formats[] = {{"d16_unorm", DXGI_FORMAT_D16_UNORM, false, 7.6e-6f, {"om.depth_format_d24_unorm_s8_uint", "om.depth_format_d32_float", "om.depth_format_d32_float_s8x24_uint", "om.depth_func_less_equal"}},
                                      {"d24_unorm_s8_uint", DXGI_FORMAT_D24_UNORM_S8_UINT, true, 2e-7f, {"om.depth_format_d16_unorm", "om.depth_format_d32_float", "om.depth_format_d32_float_s8x24_uint", "om.depth_func_less_equal"}},
                                      {"d32_float", DXGI_FORMAT_D32_FLOAT, false, 2e-7f, {"om.depth_format_d16_unorm", "om.depth_format_d24_unorm_s8_uint", "om.depth_format_d32_float_s8x24_uint", "om.depth_func_less", "om.depth_func_less_equal"}},
                                      {"d32_float_s8x24_uint", DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true, 2e-7f, {"om.depth_format_d16_unorm", "om.depth_format_d24_unorm_s8_uint", "om.depth_format_d32_float", "om.depth_func_less", "om.depth_func_less_equal"}}};
        for (const Fmt& f : formats) {
            rr::CaseSpec format_spec = spec("om", (std::string("depth_format_") + f.name).c_str()).tolerance(rr::Tolerance::blend(2).with_edge(2)).tolerance_for("depth", depth_tolerance_for(f.format, f.depth_tolerance)).tolerance_for("stencil", rr::Tolerance::exact().with_outliers(0.001));
            if (!f.same.empty()) {
                format_spec.same_as(f.same, std::string("draws the same plane pair as ") + join_names(f.same) + " in a different depth format, so the colour image is identical by design while the depth and stencil captures carry the format; a depth format backed by fewer bits shows in those captures");
            }
            Add(std::move(format_spec),
                [&f](Rig& r, Cap& out) { return depth_scene(r, out, f.format, D3D11_COMPARISON_LESS_EQUAL, true, f.stencil); },
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
