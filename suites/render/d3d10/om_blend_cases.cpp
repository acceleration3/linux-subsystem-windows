#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const char* kDstPs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  return float4(0.15 + 0.8 * uv.x, 0.9 - 0.7 * uv.y, 0.3 + 0.4 * uv.x * uv.y, 0.25 + 0.7 * uv.y); }\n";
const char* kSrcPs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  return float4(uv.x * 0.9 + 0.05, uv.y * 0.9 + 0.05, 1 - uv.x * 0.8, 0.2 + 0.75 * (1 - uv.x) * (0.3 + 0.7 * uv.y)); }\n";
const char* kDualPs =
    "struct PO { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
    "PO main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) {\n"
    "  PO o; o.c0 = float4(uv.x * 0.9 + 0.05, uv.y * 0.9 + 0.05, 1 - uv.x * 0.8, 0.2 + 0.75 * (1 - uv.x) * (0.3 + 0.7 * uv.y));\n"
    "  o.c1 = float4(0.7 - 0.5 * uv.y, 0.3 + 0.6 * uv.x, 0.5, 0.6 - 0.4 * uv.x); return o; }\n";
const char* kHdrSrcPs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  return float4(uv.x * 3.0, uv.y * 2.0, 4.0 * (1 - uv.x), 0.2 + 0.75 * (1 - uv.x) * (0.3 + 0.7 * uv.y)); }\n";

struct BlendCfg {
    D3D10_BLEND src = D3D10_BLEND_ONE;
    D3D10_BLEND dst = D3D10_BLEND_ZERO;
    D3D10_BLEND_OP op = D3D10_BLEND_OP_ADD;
    D3D10_BLEND sa = D3D10_BLEND_ONE;
    D3D10_BLEND da = D3D10_BLEND_ZERO;
    D3D10_BLEND_OP aop = D3D10_BLEND_OP_ADD;
    UINT8 mask = 0xf;
    bool enable = true;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool hdr = false;
};

bool uses_dual(D3D10_BLEND b) { return b >= D3D10_BLEND_SRC1_COLOR; }

D3D10_BLEND alpha_of(D3D10_BLEND b) {
    switch (b) {
        case D3D10_BLEND_SRC_COLOR: return D3D10_BLEND_SRC_ALPHA;
        case D3D10_BLEND_INV_SRC_COLOR: return D3D10_BLEND_INV_SRC_ALPHA;
        case D3D10_BLEND_DEST_COLOR: return D3D10_BLEND_DEST_ALPHA;
        case D3D10_BLEND_INV_DEST_COLOR: return D3D10_BLEND_INV_DEST_ALPHA;
        case D3D10_BLEND_SRC1_COLOR: return D3D10_BLEND_SRC1_ALPHA;
        case D3D10_BLEND_INV_SRC1_COLOR: return D3D10_BLEND_INV_SRC1_ALPHA;
        default: return b;
    }
}

struct BlendRun {
    BlendCfg cfg;
    Com<ID3D10PixelShader> src_ps;
    Com<ID3D10BlendState> bs;
};

void blend_setup(Rig& r, BlendRun& run) {
    const BlendCfg& cfg = run.cfg;
    r.begin(128, 128, cfg.format, false);
    r.use_std();
    const bool dual = uses_dual(cfg.src) || uses_dual(cfg.dst) || uses_dual(cfg.sa) || uses_dual(cfg.da);
    Com<ID3D10PixelShader> dst_ps = r.ps(kDstPs);
    run.src_ps = r.ps(dual ? kDualPs : (cfg.hdr ? kHdrSrcPs : kSrcPs));
    auto bd = Rig::blend_desc();
    bd.BlendEnable[0] = cfg.enable ? TRUE : FALSE;
    bd.SrcBlend = cfg.src;
    bd.DestBlend = cfg.dst;
    bd.BlendOp = cfg.op;
    bd.SrcBlendAlpha = cfg.sa;
    bd.DestBlendAlpha = cfg.da;
    bd.BlendOpAlpha = cfg.aop;
    bd.RenderTargetWriteMask[0] = cfg.mask;
    run.bs = r.blend(bd);
    Com<ID3D10BlendState> off = r.blend(Rig::blend_desc());
    if (!r.ok()) return;
    const float none[4] = {1, 1, 1, 1};
    r.d->PSSetShader(dst_ps.get());
    r.d->OMSetBlendState(off.get(), none, 0xffffffffu);
    r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
}

void blend_frame(Rig& r, BlendRun& run, unsigned f) {
    const float factor[4] = {0.25f + 0.1f * static_cast<float>(f), 0.6f, 0.85f - 0.1f * static_cast<float>(f), 0.5f};
    r.d->PSSetShader(run.src_ps.get());
    r.d->OMSetBlendState(run.bs.get(), factor, 0xffffffffu);
    const float shift = 0.08f * static_cast<float>(f);
    r.draw_std(Geo::quad_uv(-0.85f + shift, -0.85f + shift * 0.5f, 0.85f - 0.1f * shift, 0.85f - shift, 0.5f, 0, 0, 1, 1));
}

void blend_case(const std::string& name, const BlendCfg& cfg, rr::Tolerance tol, Caps caps = nullptr) {
    if (name.find("dest_") != std::string::npos || name.find("_dest") != std::string::npos) {
        tol = rr::Tolerance::blend(10);
    }
    MultiBody<BlendRun> body;
    body.setup = [cfg](Rig& r, BlendRun& run) {
        run.cfg = cfg;
        blend_setup(r, run);
    };
    body.frame = [](Rig& r, BlendRun& run, unsigned f) { blend_frame(r, run, f); };
    multi<BlendRun>(spec("om", name).frames(4, {0, 3}).tolerance(tol), body, std::move(caps));
}

std::string blend_name(D3D10_BLEND b) {
    static const char* names[] = {"", "zero", "one", "src_color", "inv_src_color", "src_alpha", "inv_src_alpha", "dest_alpha", "inv_dest_alpha", "dest_color",
                                  "inv_dest_color", "src_alpha_sat", "", "", "blend_factor", "inv_blend_factor", "src1_color", "inv_src1_color", "src1_alpha", "inv_src1_alpha"};
    return names[b];
}

const char* op_name(D3D10_BLEND_OP op) {
    static const char* names[] = {"", "add", "subtract", "rev_subtract", "min", "max"};
    return names[op];
}

std::string float_tol_name(DXGI_FORMAT f) { return std::to_string(static_cast<int>(f)); }

const Registrar kBlend{[] {
    const D3D10_BLEND all[] = {D3D10_BLEND_ZERO, D3D10_BLEND_ONE, D3D10_BLEND_SRC_COLOR, D3D10_BLEND_INV_SRC_COLOR, D3D10_BLEND_SRC_ALPHA, D3D10_BLEND_INV_SRC_ALPHA,
                               D3D10_BLEND_DEST_ALPHA, D3D10_BLEND_INV_DEST_ALPHA, D3D10_BLEND_DEST_COLOR, D3D10_BLEND_INV_DEST_COLOR, D3D10_BLEND_SRC_ALPHA_SAT,
                               D3D10_BLEND_BLEND_FACTOR, D3D10_BLEND_INV_BLEND_FACTOR, D3D10_BLEND_SRC1_COLOR, D3D10_BLEND_INV_SRC1_COLOR, D3D10_BLEND_SRC1_ALPHA,
                               D3D10_BLEND_INV_SRC1_ALPHA};
    for (D3D10_BLEND f : all) {
        BlendCfg cfg;
        cfg.src = f;
        cfg.dst = D3D10_BLEND_INV_SRC_ALPHA;
        cfg.sa = alpha_of(f);
        cfg.da = D3D10_BLEND_INV_SRC_ALPHA;
        if (uses_dual(f)) {
            cfg.dst = D3D10_BLEND_ONE;
            cfg.da = D3D10_BLEND_ONE;
        }
        blend_case("blend_src_" + blend_name(f), cfg, rr::Tolerance::blend(4));
        if (f == D3D10_BLEND_SRC_ALPHA_SAT) continue;
        BlendCfg d;
        d.src = D3D10_BLEND_SRC_ALPHA;
        d.sa = D3D10_BLEND_ONE;
        d.dst = f;
        d.da = alpha_of(f);
        if (uses_dual(f)) {
            d.src = D3D10_BLEND_ONE;
        }
        blend_case("blend_dest_" + blend_name(f), d, rr::Tolerance::blend(4));
    }
    const D3D10_BLEND_OP ops[] = {D3D10_BLEND_OP_ADD, D3D10_BLEND_OP_SUBTRACT, D3D10_BLEND_OP_REV_SUBTRACT, D3D10_BLEND_OP_MIN, D3D10_BLEND_OP_MAX};
    for (D3D10_BLEND_OP op : ops) {
        BlendCfg cfg;
        cfg.src = D3D10_BLEND_SRC_ALPHA;
        cfg.dst = D3D10_BLEND_INV_SRC_ALPHA;
        cfg.op = op;
        cfg.sa = D3D10_BLEND_ONE;
        cfg.da = D3D10_BLEND_ONE;
        cfg.aop = op;
        if (op == D3D10_BLEND_OP_MIN || op == D3D10_BLEND_OP_MAX) {
            cfg.src = D3D10_BLEND_ONE;
            cfg.dst = D3D10_BLEND_ONE;
        }
        blend_case(std::string("blend_op_") + op_name(op), cfg, rr::Tolerance::blend(4));
    }
    {
        BlendCfg cfg;
        cfg.src = D3D10_BLEND_SRC_ALPHA;
        cfg.dst = D3D10_BLEND_INV_SRC_ALPHA;
        cfg.op = D3D10_BLEND_OP_ADD;
        cfg.sa = D3D10_BLEND_ONE;
        cfg.da = D3D10_BLEND_ONE;
        cfg.aop = D3D10_BLEND_OP_MAX;
        blend_case("blend_separate_alpha_op_max", cfg, rr::Tolerance::blend(4));
        cfg.sa = D3D10_BLEND_ZERO;
        cfg.da = D3D10_BLEND_SRC_ALPHA;
        cfg.aop = D3D10_BLEND_OP_REV_SUBTRACT;
        blend_case("blend_separate_alpha_rev_subtract", cfg, rr::Tolerance::blend(4));
    }
    for (int mask = 1; mask < 16; ++mask) {
        BlendCfg cfg;
        cfg.enable = (mask % 2) == 0;
        cfg.src = D3D10_BLEND_SRC_ALPHA;
        cfg.dst = D3D10_BLEND_INV_SRC_ALPHA;
        cfg.sa = D3D10_BLEND_ONE;
        cfg.da = D3D10_BLEND_INV_SRC_ALPHA;
        cfg.mask = static_cast<UINT8>(mask);
        blend_case("write_mask_" + std::to_string(mask), cfg, rr::Tolerance::blend(4));
    }
    struct FormatBlend {
        const char* name;
        DXGI_FORMAT format;
        bool hdr;
        rr::Tolerance tol;
    };
    const FormatBlend formats[] = {
        {"blend_format_rgba16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, true, rr::Tolerance::floats(4e-3f, 4e-3f)},
        {"blend_format_rgba32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, true, rr::Tolerance::floats(1e-4f, 1e-4f)},
        {"blend_format_srgb", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false, rr::Tolerance::blend(3)},
        {"blend_format_bgra8", DXGI_FORMAT_B8G8R8A8_UNORM, false, rr::Tolerance::blend()},
        {"blend_format_rgb10a2", DXGI_FORMAT_R10G10B10A2_UNORM, false, rr::Tolerance::floats(3e-3f, 3e-3f)},
        {"blend_format_r11g11b10", DXGI_FORMAT_R11G11B10_FLOAT, true, rr::Tolerance::floats(2e-2f, 3e-2f)},
    };
    for (const FormatBlend& fb : formats) {
        BlendCfg cfg;
        cfg.format = fb.format;
        cfg.hdr = fb.hdr;
        cfg.src = D3D10_BLEND_SRC_ALPHA;
        cfg.dst = fb.hdr ? D3D10_BLEND_ONE : D3D10_BLEND_INV_SRC_ALPHA;
        cfg.sa = D3D10_BLEND_ONE;
        cfg.da = D3D10_BLEND_INV_SRC_ALPHA;
        const DXGI_FORMAT format = fb.format;
        blend_case(fb.name, cfg, fb.tol, [format](Rig& r) -> std::string {
            const UINT need = D3D10_FORMAT_SUPPORT_RENDER_TARGET | D3D10_FORMAT_SUPPORT_BLENDABLE;
            return (r.format_support(format) & need) == need ? "" : "blendable render target format " + float_tol_name(format);
        });
    }
    single(spec("om", "blend_factor_changes_between_draws").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_BLEND_FACTOR;
        bd.DestBlend = D3D10_BLEND_INV_BLEND_FACTOR;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_ZERO;
        Com<ID3D10BlendState> bs = r.blend(bd);
        Com<ID3D10PixelShader> src = r.ps(kSrcPs);
        if (!r.ok()) return;
        r.d->PSSetShader(src.get());
        for (int i = 0; i < 4; ++i) {
            const float f[4] = {0.2f * (i + 1), 0.9f - 0.2f * i, 0.5f, 1.0f};
            r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
            r.draw_std(Geo::quad_uv(-0.95f + 0.5f * i, -0.9f, -0.5f + 0.5f * i, 0.9f, 0.5f, 0, 0, 1, 1));
        }
        r.cap_main(o);
    });
    single(spec("om", "blend_enable_per_target_one_state").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        Target t[4];
        for (auto& x : t) r.target(x, 128, 128);
        Com<ID3D10PixelShader> dst = r.ps(
            "struct PO { float4 c[4] : SV_Target; };\n"
            "struct PO4 { float4 c0 : SV_Target0; float4 c1 : SV_Target1; float4 c2 : SV_Target2; float4 c3 : SV_Target3; };\n"
            "PO4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO4 o; float4 v = float4(0.15 + 0.8 * uv.x, 0.9 - 0.7 * uv.y, 0.3 + 0.4 * uv.x * uv.y, 0.25 + 0.7 * uv.y);\n"
            "  o.c0 = v; o.c1 = v.bgra; o.c2 = v.gbra; o.c3 = v.rrra; return o; }\n");
        Com<ID3D10PixelShader> src = r.ps(
            "struct PO4 { float4 c0 : SV_Target0; float4 c1 : SV_Target1; float4 c2 : SV_Target2; float4 c3 : SV_Target3; };\n"
            "PO4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO4 o; float4 v = float4(uv.x, uv.y, 1 - uv.x, 0.2 + 0.7 * uv.y);\n"
            "  o.c0 = v; o.c1 = v; o.c2 = v; o.c3 = v; return o; }\n");
        r.use_std();
        auto bd = Rig::blend_desc();
        for (int i = 0; i < 8; ++i) bd.BlendEnable[i] = (i % 2) == 0;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.RenderTargetWriteMask[2] = D3D10_COLOR_WRITE_ENABLE_RED | D3D10_COLOR_WRITE_ENABLE_BLUE;
        Com<ID3D10BlendState> bs = r.blend(bd);
        Com<ID3D10BlendState> off = r.blend(Rig::blend_desc());
        if (!r.ok()) return;
        ID3D10RenderTargetView* views[4] = {t[0].view.get(), t[1].view.get(), t[2].view.get(), t[3].view.get()};
        r.d->OMSetRenderTargets(4, views, nullptr);
        r.viewport(128, 128);
        r.no_cull();
        const float f[4] = {1, 1, 1, 1};
        r.d->PSSetShader(dst.get());
        r.d->OMSetBlendState(off.get(), f, 0xffffffffu);
        r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
        r.d->PSSetShader(src.get());
        r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
        r.draw_std(Geo::quad_uv(-0.85f, -0.85f, 0.85f, 0.85f, 0.5f, 0, 0, 1, 1));
        for (int i = 0; i < 4; ++i) r.cap(o, "target" + std::to_string(i), t[i]);
    });
    single(spec("om", "blend_independent_ops_per_target", 1).tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        Target t[4];
        for (auto& x : t) r.target(x, 128, 128);
        Com<ID3D10PixelShader> src = r.ps(
            "struct PO4 { float4 c0 : SV_Target0; float4 c1 : SV_Target1; float4 c2 : SV_Target2; float4 c3 : SV_Target3; };\n"
            "PO4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) { PO4 o; float4 v = float4(uv.x, uv.y, 1 - uv.x, 0.3 + 0.6 * uv.y);\n"
            "  o.c0 = v; o.c1 = v; o.c2 = v; o.c3 = v; return o; }\n");
        r.use_std();
        D3D10_BLEND_DESC1 bd{};
        bd.IndependentBlendEnable = TRUE;
        const D3D10_BLEND_OP ops[4] = {D3D10_BLEND_OP_ADD, D3D10_BLEND_OP_SUBTRACT, D3D10_BLEND_OP_MAX, D3D10_BLEND_OP_REV_SUBTRACT};
        const D3D10_BLEND srcs[4] = {D3D10_BLEND_SRC_ALPHA, D3D10_BLEND_ONE, D3D10_BLEND_ONE, D3D10_BLEND_SRC_ALPHA};
        const D3D10_BLEND dsts[4] = {D3D10_BLEND_INV_SRC_ALPHA, D3D10_BLEND_ONE, D3D10_BLEND_ONE, D3D10_BLEND_ONE};
        for (int i = 0; i < 4; ++i) {
            auto& rt = bd.RenderTarget[i];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = srcs[i];
            rt.DestBlend = dsts[i];
            rt.BlendOp = ops[i];
            rt.SrcBlendAlpha = D3D10_BLEND_ONE;
            rt.DestBlendAlpha = D3D10_BLEND_ONE;
            rt.BlendOpAlpha = D3D10_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D10_COLOR_WRITE_ENABLE_ALL;
        }
        Com<ID3D10BlendState1> bs = r.blend1(bd);
        if (!r.ok()) return;
        ID3D10RenderTargetView* views[4] = {t[0].view.get(), t[1].view.get(), t[2].view.get(), t[3].view.get()};
        r.d->OMSetRenderTargets(4, views, nullptr);
        for (int i = 0; i < 4; ++i) r.clear(t[i], 0.2f + 0.2f * i, 0.5f, 0.3f + 0.1f * i, 0.6f);
        r.viewport(128, 128);
        r.no_cull();
        const float f[4] = {1, 1, 1, 1};
        r.d->PSSetShader(src.get());
        r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
        r.draw_std(Geo::quad_uv(-0.85f, -0.85f, 0.85f, 0.85f, 0.5f, 0, 0, 1, 1));
        for (int i = 0; i < 4; ++i) r.cap(o, "target" + std::to_string(i), t[i]);
    });
    {
        BlendCfg cfg;
        cfg.src = D3D10_BLEND_SRC1_COLOR;
        cfg.dst = D3D10_BLEND_INV_SRC1_COLOR;
        cfg.sa = D3D10_BLEND_SRC1_ALPHA;
        cfg.da = D3D10_BLEND_INV_SRC1_ALPHA;
        cfg.mask = D3D10_COLOR_WRITE_ENABLE_RED | D3D10_COLOR_WRITE_ENABLE_ALPHA | D3D10_COLOR_WRITE_ENABLE_GREEN;
        blend_case("blend_dual_source_with_write_mask", cfg, rr::Tolerance::blend(4));
    }
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
