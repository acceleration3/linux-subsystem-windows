#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kBackPs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return float4(0.8 * i.uv.y, 0.3 + 0.4 * i.uv.x, 0.5 + 0.4 * sin(i.uv.x * 6.0), 0.4 + 0.5 * i.uv.x); }\n";
const char* const kFrontPs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return float4(i.uv.x, i.uv.y, 1.0 - i.uv.x * 0.5, 0.2 + 0.7 * i.uv.y); }\n";
const char* const kDualPs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "struct PO { float4 a : SV_Target0; float4 b : SV_Target1; };\n"
    "PO main(VO i) { PO o; o.a = float4(i.uv.x, i.uv.y, 1.0 - i.uv.x * 0.5, 0.2 + 0.7 * i.uv.y);\n"
    "  o.b = float4(1.0 - i.uv.y, i.uv.x, 0.5, 0.25 + 0.5 * i.uv.x); return o; }\n";

struct HalfState {
    D3D11_BLEND src, dst, src_a, dst_a;
    D3D11_BLEND_OP op, op_a;
    UINT8 mask = D3D11_COLOR_WRITE_ENABLE_ALL;
};

bool run_blend(Rig& r, Cap& out, const HalfState& left, const HalfState& right, bool dual) {
    Target target;
    if (!r.color(target)) return false;
    Com<ID3D11PixelShader> back = r.ps(kBackPs);
    Com<ID3D11PixelShader> front = r.ps(dual ? kDualPs : kFrontPs);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!back || !front || !rs) return false;
    Com<ID3D11BlendState> states[2];
    const HalfState* halves[2] = {&left, &right};
    for (int h = 0; h < 2; ++h) {
        D3D11_BLEND_DESC bd = bdesc();
        auto& t = bd.RenderTarget[0];
        t.BlendEnable = TRUE;
        t.SrcBlend = halves[h]->src;
        t.DestBlend = halves[h]->dst;
        t.BlendOp = halves[h]->op;
        t.SrcBlendAlpha = halves[h]->src_a;
        t.DestBlendAlpha = halves[h]->dst_a;
        t.BlendOpAlpha = halves[h]->op_a;
        t.RenderTargetWriteMask = halves[h]->mask;
        states[h] = r.blend(bd);
        if (!states[h]) return false;
    }
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    const D3D11_RECT all{0, 0, 128, 128};
    r.ctx->RSSetScissorRects(1, &all);
    r.draw_fs(back.get());
    const float factor[4] = {0.25f, 0.5f, 0.75f, 0.4f};
    for (int h = 0; h < 2; ++h) {
        const D3D11_RECT half{h == 0 ? 0 : 64, 0, h == 0 ? 64 : 128, 128};
        r.ctx->RSSetScissorRects(1, &half);
        r.ctx->OMSetBlendState(states[h].get(), factor, 0xffffffffu);
        r.draw_fs(front.get());
    }
    return r.shot(out, target);
}

struct BlendRegistrar {
    BlendRegistrar() {
        Add(spec("om", "blend_separate_alpha").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
            HalfState l{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            HalfState rt{D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_REV_SUBTRACT};
            return run_blend(r, out, l, rt, false);
        });
        Add(spec("om", "blend_separate_alpha_min_max").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
            HalfState l{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_MAX};
            HalfState rt{D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_INV_BLEND_FACTOR, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_MIN};
            return run_blend(r, out, l, rt, false);
        });
        Add(spec("om", "blend_premultiplied_over").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
            HalfState l{D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            HalfState rt{D3D11_BLEND_DEST_COLOR, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            return run_blend(r, out, l, rt, false);
        });
        Add(spec("om", "blend_dual_source_classic").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
            HalfState l{D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_SRC1_ALPHA, D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            HalfState rt{D3D11_BLEND_ONE, D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_ONE, D3D11_BLEND_SRC1_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            return run_blend(r, out, l, rt, true);
        });
    }
};
const BlendRegistrar kBlendRegistrar;

const Add kIndependent(spec("om", "blend_independent_eight_targets").tolerance(rr::Tolerance::blend(2)),
    [](Rig& r, Cap& out) {
        Target targets[8];
        for (Target& t : targets) {
            if (!r.color(t)) return false;
        }
        const char* ps_source =
            "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
            "struct PO { float4 c[8] : SV_Target; };\n"
            "PO main(VO i) { PO o; [unroll] for (int k = 0; k < 8; ++k) o.c[k] = float4(frac(i.uv.x + 0.1 * k), i.uv.y, 1.0 - frac(i.uv.x * 2 + 0.2 * k), 0.3 + 0.08 * k); return o; }\n";
        Com<ID3D11PixelShader> ps = r.ps(ps_source);
        if (!ps) return false;
        D3D11_BLEND_DESC bd = bdesc();
        bd.IndependentBlendEnable = TRUE;
        static const D3D11_BLEND src[8] = {D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA};
        static const D3D11_BLEND dst[8] = {D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE};
        static const D3D11_BLEND_OP op[8] = {D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_MAX, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_OP_SUBTRACT};
        for (int k = 0; k < 8; ++k) {
            auto& t = bd.RenderTarget[k];
            t.BlendEnable = k != 0;
            t.SrcBlend = src[k];
            t.DestBlend = dst[k];
            t.BlendOp = op[k];
            t.SrcBlendAlpha = D3D11_BLEND_ONE;
            t.DestBlendAlpha = D3D11_BLEND_ONE;
            t.BlendOpAlpha = k % 2 ? D3D11_BLEND_OP_MAX : D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = k == 3 ? 0x7 : 0xF;
        }
        Com<ID3D11BlendState> state = r.blend(bd);
        Com<ID3D11PixelShader> back = r.ps(kBackPs);
        if (!state || !back) return false;
        ID3D11RenderTargetView* views[8];
        const float clear[4] = {0.3f, 0.25f, 0.2f, 0.5f};
        for (int k = 0; k < 8; ++k) {
            views[k] = targets[k].view.get();
            r.ctx->ClearRenderTargetView(views[k], clear);
        }
        r.ctx->OMSetRenderTargets(8, views, nullptr);
        r.viewport(0, 0, 128, 128);
        const float factor[4] = {1, 1, 1, 1};
        r.ctx->OMSetBlendState(state.get(), factor, 0xffffffffu);
        r.draw_fs(ps.get());
        r.draw_fs(ps.get());
        for (int k = 0; k < 8; ++k) {
            if (!r.shot(out, targets[k], ("rt" + std::to_string(k)).c_str())) return false;
        }
        return true;
    });

bool run_msaa_blend(Rig& r, Cap& out, bool coverage, UINT sample_mask) {
    Target target;
    if (!r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4)) return false;
    const char* ps_source =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { return float4(i.uv.x, 0.8 * i.uv.y, 1.0 - i.uv.x, i.uv.x * 0.9 + 0.05); }\n";
    const char* back_source =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { return float4(0.1, 0.2 + 0.4 * i.uv.y, 0.1, 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Com<ID3D11PixelShader> back = r.ps(back_source);
    D3D11_BLEND_DESC bd = bdesc();
    bd.AlphaToCoverageEnable = coverage;
    Com<ID3D11BlendState> state = r.blend(bd);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!ps || !back || !state || !rs) return false;
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    r.draw_fs(back.get());
    const float factor[4] = {};
    r.ctx->OMSetBlendState(state.get(), factor, sample_mask);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

const Add kAlphaToCoverage(spec("om", "alpha_to_coverage_4x").tolerance(rr::Tolerance::psnr(24)), [](Rig& r, Cap& out) { return run_msaa_blend(r, out, true, 0xffffffffu); },
                           [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kSampleMask(spec("om", "sample_mask_4x").tolerance(rr::Tolerance::psnr(30)), [](Rig& r, Cap& out) { return run_msaa_blend(r, out, false, 0x5u); },
                      [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kAlphaToCoverageMask(spec("om", "alpha_to_coverage_with_sample_mask").tolerance(rr::Tolerance::psnr(24)),
                               [](Rig& r, Cap& out) { return run_msaa_blend(r, out, true, 0xAu); },
                               [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

}  // namespace

}  // namespace lsw::suites::render::d3d11
