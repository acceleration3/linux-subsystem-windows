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

struct Factor {
    const char* name;
    D3D11_BLEND color;
    D3D11_BLEND alpha;
    bool dual;
};

const Factor kFactors[] = {
    {"zero", D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, false},
    {"one", D3D11_BLEND_ONE, D3D11_BLEND_ONE, false},
    {"src_color", D3D11_BLEND_SRC_COLOR, D3D11_BLEND_SRC_ALPHA, false},
    {"inv_src_color", D3D11_BLEND_INV_SRC_COLOR, D3D11_BLEND_INV_SRC_ALPHA, false},
    {"src_alpha", D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, false},
    {"inv_src_alpha", D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, false},
    {"dest_alpha", D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_DEST_ALPHA, false},
    {"inv_dest_alpha", D3D11_BLEND_INV_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA, false},
    {"dest_color", D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_ALPHA, false},
    {"inv_dest_color", D3D11_BLEND_INV_DEST_COLOR, D3D11_BLEND_INV_DEST_ALPHA, false},
    {"src_alpha_sat", D3D11_BLEND_SRC_ALPHA_SAT, D3D11_BLEND_SRC_ALPHA_SAT, false},
    {"blend_factor", D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_BLEND_FACTOR, false},
    {"inv_blend_factor", D3D11_BLEND_INV_BLEND_FACTOR, D3D11_BLEND_INV_BLEND_FACTOR, false},
    {"src1_color", D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_SRC1_ALPHA, true},
    {"inv_src1_color", D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_INV_SRC1_ALPHA, true},
    {"src1_alpha", D3D11_BLEND_SRC1_ALPHA, D3D11_BLEND_SRC1_ALPHA, true},
    {"inv_src1_alpha", D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_INV_SRC1_ALPHA, true},
};

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
        for (const Factor& f : kFactors) {
            Add(spec("om", (std::string("blend_factor_") + f.name).c_str()).tolerance(rr::Tolerance::blend(2)),
                [&f](Rig& r, Cap& out) {
                    HalfState l{f.color, D3D11_BLEND_ONE, f.alpha, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
                    const bool sat = f.color == D3D11_BLEND_SRC_ALPHA_SAT;
                    HalfState rt{D3D11_BLEND_ONE, sat ? D3D11_BLEND_ZERO : f.color, D3D11_BLEND_ONE, sat ? D3D11_BLEND_ZERO : f.alpha,
                                 D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
                    return run_blend(r, out, l, rt, f.dual);
                });
        }
        struct OpRow {
            const char* name;
            D3D11_BLEND_OP op;
        };
        static const OpRow ops[] = {{"add", D3D11_BLEND_OP_ADD},
                                    {"subtract", D3D11_BLEND_OP_SUBTRACT},
                                    {"rev_subtract", D3D11_BLEND_OP_REV_SUBTRACT},
                                    {"min", D3D11_BLEND_OP_MIN},
                                    {"max", D3D11_BLEND_OP_MAX}};
        for (const OpRow& op : ops) {
            Add(spec("om", (std::string("blend_op_") + op.name).c_str()).tolerance(rr::Tolerance::blend(2)), [&op](Rig& r, Cap& out) {
                const bool minmax = op.op == D3D11_BLEND_OP_MIN || op.op == D3D11_BLEND_OP_MAX;
                const D3D11_BLEND s = minmax ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
                const D3D11_BLEND d = minmax ? D3D11_BLEND_ONE : D3D11_BLEND_ONE;
                HalfState l{s, d, D3D11_BLEND_ONE, D3D11_BLEND_ONE, op.op, op.op};
                HalfState rt{D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, op.op, D3D11_BLEND_OP_ADD};
                return run_blend(r, out, l, rt, false);
            });
        }
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
        for (UINT mask = 1; mask < 16; ++mask) {
            Add(spec("om", ("write_mask_" + std::to_string(mask)).c_str()).tolerance(rr::Tolerance::blend(2)), [mask](Rig& r, Cap& out) {
                HalfState l{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD,
                            static_cast<UINT8>(mask)};
                HalfState rt{D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, static_cast<UINT8>(mask)};
                return run_blend(r, out, l, rt, false);
            });
        }
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

struct LogicRow {
    const char* name;
    D3D11_LOGIC_OP op;
};

const LogicRow kLogic[] = {{"clear", D3D11_LOGIC_OP_CLEAR},           {"set", D3D11_LOGIC_OP_SET},
                           {"copy", D3D11_LOGIC_OP_COPY},             {"copy_inverted", D3D11_LOGIC_OP_COPY_INVERTED},
                           {"noop", D3D11_LOGIC_OP_NOOP},             {"invert", D3D11_LOGIC_OP_INVERT},
                           {"and", D3D11_LOGIC_OP_AND},               {"nand", D3D11_LOGIC_OP_NAND},
                           {"or", D3D11_LOGIC_OP_OR},                 {"nor", D3D11_LOGIC_OP_NOR},
                           {"xor", D3D11_LOGIC_OP_XOR},               {"equiv", D3D11_LOGIC_OP_EQUIV},
                           {"and_reverse", D3D11_LOGIC_OP_AND_REVERSE}, {"and_inverted", D3D11_LOGIC_OP_AND_INVERTED},
                           {"or_reverse", D3D11_LOGIC_OP_OR_REVERSE}, {"or_inverted", D3D11_LOGIC_OP_OR_INVERTED}};

bool run_logic(Rig& r, Cap& out, D3D11_LOGIC_OP op) {
    Target target;
    if (!r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UINT)) return false;
    const char* back_source =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "uint4 main(VO i) : SV_Target { uint x = (uint)i.p.x; uint y = (uint)i.p.y; return uint4((x * 2) & 255, (y * 2) & 255, (x ^ y) & 255, 255 - ((x + y) & 127)); }\n";
    const char* front_source =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "uint4 main(VO i) : SV_Target { uint x = (uint)i.p.x; uint y = (uint)i.p.y; return uint4((x * 5 + y) & 255, (x + y * 3) & 255, 0xA5 ^ (x & 255), (x * y) & 255); }\n";
    Com<ID3D11PixelShader> back = r.ps(back_source);
    Com<ID3D11PixelShader> front = r.ps(front_source);
    ID3D11Device1* raw_dev1 = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&raw_dev1));
    Com<ID3D11Device1> dev1(raw_dev1);
    if (!back || !front || !dev1) return r.fail("ID3D11Device1 is unavailable");
    D3D11_BLEND_DESC1 bd{};
    bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].LogicOpEnable = TRUE;
    bd.RenderTarget[0].LogicOp = op;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    Com<ID3D11BlendState1> state;
    if (!r.ok(dev1->CreateBlendState1(&bd, state.put()), "CreateBlendState1")) return false;
    r.bind(target);
    r.draw_fs(back.get());
    const float factor[4] = {};
    r.ctx->OMSetBlendState(state.get(), factor, 0xffffffffu);
    r.draw_fs(front.get());
    rr::Image image;
    if (!r.read_image(target.texture.get(), DXGI_FORMAT_R8G8B8A8_UINT, 0, image)) return false;
    out.add("", std::move(image));
    return true;
}

struct LogicRegistrar {
    LogicRegistrar() {
        for (const LogicRow& row : kLogic) {
            Add(spec("om", (std::string("logic_op_") + row.name).c_str()).variant(1).uniform_ok(),
                [&row](Rig& r, Cap& out) { return run_logic(r, out, row.op); },
                [](Rig& r) -> std::string {
                    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
                    if (FAILED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof options)) || !options.OutputMergerLogicOp) {
                        return "logic operations in the output merger";
                    }
                    return need_format(r, DXGI_FORMAT_R8G8B8A8_UINT, D3D11_FORMAT_SUPPORT_RENDER_TARGET, "R8G8B8A8_UINT render targets");
                });
        }
    }
};
const LogicRegistrar kLogicRegistrar;

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

const Add kAlphaToCoverage(spec("om", "alpha_to_coverage_4x").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return run_msaa_blend(r, out, true, 0xffffffffu); },
                           [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kSampleMask(spec("om", "sample_mask_4x").tolerance(rr::Tolerance::psnr(30)), [](Rig& r, Cap& out) { return run_msaa_blend(r, out, false, 0x5u); },
                      [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kAlphaToCoverageMask(spec("om", "alpha_to_coverage_with_sample_mask").tolerance(rr::Tolerance::psnr(24)),
                               [](Rig& r, Cap& out) { return run_msaa_blend(r, out, true, 0xAu); },
                               [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

}  // namespace

}  // namespace lsw::suites::render::d3d11
