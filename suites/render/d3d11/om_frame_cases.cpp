#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kBackPs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return float4(0.8 * i.uv.y, 0.3 + 0.4 * i.uv.x, 0.5 + 0.4 * sin(i.uv.x * 6.0), 0.4 + 0.5 * i.uv.x); }\n";
const char* const kFrontPs =
    "cbuffer K : register(b0) { float4 k; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return float4(frac(i.uv.x + 0.21 * k.x), frac(i.uv.y * (1.0 + 0.5 * k.x)), 1.0 - i.uv.x * 0.5 - 0.1 * k.x, 0.25 + 0.6 * frac(i.uv.y + 0.3 * k.x)); }\n";
const char* const kDualPs =
    "cbuffer K : register(b0) { float4 k; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "struct PO { float4 a : SV_Target0; float4 b : SV_Target1; };\n"
    "PO main(VO i) { PO o; o.a = float4(frac(i.uv.x + 0.21 * k.x), i.uv.y, 1.0 - i.uv.x * 0.5, 0.2 + 0.7 * i.uv.y);\n"
    "  o.b = float4(1.0 - i.uv.y, frac(i.uv.x + 0.13 * k.x), 0.5, 0.25 + 0.5 * i.uv.x); return o; }\n";

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

struct Half {
    D3D11_BLEND src, dst, src_a, dst_a;
    D3D11_BLEND_OP op, op_a;
    UINT8 mask = D3D11_COLOR_WRITE_ENABLE_ALL;
};

struct BlendMf {
    Half left, right;
    bool dual = false;
    Target target;
    Com<ID3D11PixelShader> back, front;
    Com<ID3D11RasterizerState> rs;
    Com<ID3D11Buffer> cb;
    Com<ID3D11BlendState> states[2];

    BlendMf(Half l, Half r, bool d) : left(l), right(r), dual(d) {}
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        back = r.ps(kBackPs);
        front = r.ps(dual ? kDualPs : kFrontPs);
        D3D11_RASTERIZER_DESC rd = rdesc();
        rd.ScissorEnable = TRUE;
        rs = r.rast(rd);
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        const Half* halves[2] = {&left, &right};
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
        return back && front && rs && cb && r.color(target);
    }
    bool step(Rig& r, unsigned frame) {
        ID3D11RenderTargetView* v = target.view.get();
        r.ctx->OMSetRenderTargets(1, &v, nullptr);
        r.viewport(0, 0, 128, 128);
        r.ctx->RSSetState(rs.get());
        const D3D11_RECT all{0, 0, 128, 128};
        r.ctx->RSSetScissorRects(1, &all);
        if (frame == 0) {
            const float clear[4] = {0.05f, 0.06f, 0.1f, 1.0f};
            r.ctx->ClearRenderTargetView(v, clear);
            r.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
            r.draw_fs(back.get());
        }
        const float k[4] = {static_cast<float>(frame), 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        r.ctx->PSSetConstantBuffers(0, 1, &b);
        const float factor[4] = {0.25f, 0.5f, 0.75f, 0.4f};
        for (int h = 0; h < 2; ++h) {
            const D3D11_RECT half{h == 0 ? 0 : 64, 0, h == 0 ? 64 : 128, 128};
            r.ctx->RSSetScissorRects(1, &half);
            r.ctx->OMSetBlendState(states[h].get(), factor, 0xffffffffu);
            r.draw_fs(front.get());
        }
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};

struct Registrar {
    Registrar() {
        const rr::Tolerance tolerance = rr::Tolerance::blend(8);
        for (const Factor& f : kFactors) {
            const bool sat = f.color == D3D11_BLEND_SRC_ALPHA_SAT;
            const Half l{f.color, D3D11_BLEND_ONE, f.alpha, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            const Half rt{D3D11_BLEND_ONE, sat ? D3D11_BLEND_ZERO : f.color, D3D11_BLEND_ONE, sat ? D3D11_BLEND_ZERO : f.alpha, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD};
            const bool dual = f.dual;
            AddState<BlendMf>(spec("om", (std::string("blend_factor_") + f.name).c_str()).frames(4, {1, 3}).tolerance(tolerance), [l, rt, dual] { return BlendMf(l, rt, dual); });
        }
        struct OpRow {
            const char* name;
            D3D11_BLEND_OP op;
        };
        static const OpRow ops[] = {{"add", D3D11_BLEND_OP_ADD}, {"subtract", D3D11_BLEND_OP_SUBTRACT}, {"rev_subtract", D3D11_BLEND_OP_REV_SUBTRACT},
                                    {"min", D3D11_BLEND_OP_MIN}, {"max", D3D11_BLEND_OP_MAX}};
        for (const OpRow& op : ops) {
            const bool minmax = op.op == D3D11_BLEND_OP_MIN || op.op == D3D11_BLEND_OP_MAX;
            const Half l{minmax ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, op.op, op.op};
            const Half rt{D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, op.op, D3D11_BLEND_OP_ADD};
            AddState<BlendMf>(spec("om", (std::string("blend_op_") + op.name).c_str()).frames(4, {1, 3}).tolerance(tolerance), [l, rt] { return BlendMf(l, rt, false); });
        }
        for (UINT mask = 1; mask < 16; ++mask) {
            const Half l{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, static_cast<UINT8>(mask)};
            const Half rt{D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_ADD, static_cast<UINT8>(mask)};
            AddState<BlendMf>(spec("om", ("write_mask_" + std::to_string(mask)).c_str()).frames(4, {1, 3}).tolerance(tolerance), [l, rt] { return BlendMf(l, rt, false); });
        }
    }
};
const Registrar kRegistrar;

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

struct LogicMf {
    D3D11_LOGIC_OP op;
    Target target;
    Com<ID3D11PixelShader> back, front;
    Com<ID3D11Buffer> cb;
    Com<ID3D11BlendState1> state;

    explicit LogicMf(D3D11_LOGIC_OP o) : op(o) {}
    std::string needs(Rig& r) {
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        if (FAILED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof options)) || !options.OutputMergerLogicOp) return "logic operations in the output merger";
        return need_format(r, DXGI_FORMAT_R8G8B8A8_UINT, D3D11_FORMAT_SUPPORT_RENDER_TARGET, "R8G8B8A8_UINT render targets");
    }
    bool init(Rig& r) {
        back = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                    "uint4 main(VO i) : SV_Target { uint x = (uint)i.p.x; uint y = (uint)i.p.y; return uint4((x * 2) & 255, (y * 2) & 255, (x ^ y) & 255, 255 - ((x + y) & 127)); }\n");
        front = r.ps("cbuffer K : register(b0) { uint4 k; };\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                     "uint4 main(VO i) : SV_Target { uint x = (uint)i.p.x + k.x * 37; uint y = (uint)i.p.y; return uint4((x * 5 + y) & 255, (x + y * 3) & 255, 0xA5 ^ (x & 255), (x * y + k.x * 11) & 255); }\n");
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        ID3D11Device1* raw = nullptr;
        r.dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&raw));
        Com<ID3D11Device1> dev1(raw);
        if (!dev1) return r.fail("ID3D11Device1 is unavailable");
        D3D11_BLEND_DESC1 bd{};
        auto& t = bd.RenderTarget[0];
        t.SrcBlend = t.SrcBlendAlpha = D3D11_BLEND_ONE;
        t.DestBlend = t.DestBlendAlpha = D3D11_BLEND_ZERO;
        t.BlendOp = t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.LogicOpEnable = TRUE;
        t.LogicOp = op;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        return back && front && cb && r.ok(dev1->CreateBlendState1(&bd, state.put()), "CreateBlendState1") && r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UINT);
    }
    bool step(Rig& r, unsigned frame) {
        r.bind(target);
        if (frame == 0) r.draw_fs(back.get());
        const UINT k[4] = {frame, 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        r.ctx->PSSetConstantBuffers(0, 1, &b);
        const float factor[4] = {};
        r.ctx->OMSetBlendState(state.get(), factor, 0xffffffffu);
        r.draw_fs(front.get());
        r.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) {
        rr::Image image;
        if (!r.read_image(target.texture.get(), DXGI_FORMAT_R8G8B8A8_UINT, 0, image)) return false;
        out.add("", std::move(image));
        return true;
    }
};

struct LogicRegistrar {
    LogicRegistrar() {
        for (const LogicRow& row : kLogic) {
            const D3D11_LOGIC_OP op = row.op;
            AddState<LogicMf>(spec("om", (std::string("logic_op_") + row.name).c_str()).variant(1).frames(4, {1, 3}).uniform_ok(), [op] { return LogicMf(op); });
        }
    }
};
const LogicRegistrar kLogicRegistrar;

const char* const kPlanePs =
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "struct PO { float4 c : SV_Target; float d : SV_Depth; };\n"
    "cbuffer K : register(b0) { float4 k; };\n"
    "PO main(VO i) { PO o; float za = 0.2 + 0.6 * i.uv.x; float zb = 0.8 - 0.6 * i.uv.y; float zc = 0.35 + 0.4 * i.uv.y;\n"
    "  bool same = k.x < 0.5 || (k.x < 1.5 && i.uv.y < 0.5); o.d = k.x < 2.5 ? (same ? za : zb) : zc;\n"
    "  o.c = k.x < 0.5 ? float4(0.2, 0.4 + 0.4 * i.uv.y, 0.9, 1) : (k.x < 1.5 ? float4(0.9, 0.5 * i.uv.x, 0.2 + 0.3 * i.uv.y, 1) : (k.x < 2.5 ? float4(0.2, 0.9, 0.4 * i.uv.x, 1) : float4(1, 1, 0.2, 1)));\n"
    "  return o; }\n";

struct DepthMf {
    D3D11_COMPARISON_FUNC func;
    bool write;
    Target target;
    Depth depth;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11Buffer> cb;
    Com<ID3D11DepthStencilState> base, test, probe;

    DepthMf(D3D11_COMPARISON_FUNC f, bool w) : func(f), write(w) {}
    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        ps = r.ps(kPlanePs);
        cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        base = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        test = r.dss(ddesc(true, func, write));
        probe = r.dss(ddesc(true, D3D11_COMPARISON_LESS, false));
        return ps && cb && base && test && probe && r.color(target) && r.depth(depth);
    }
    bool step(Rig& r, unsigned frame) {
        const float background[4] = {0.9f, 0.9f, 0.9f, 1};
        if (frame == 0) r.bind(target, &depth, background);
        else {
            ID3D11RenderTargetView* v = target.view.get();
            r.ctx->OMSetRenderTargets(1, &v, depth.view.get());
            r.viewport(0, 0, 128, 128);
        }
        const float k[4] = {static_cast<float>(frame), 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
        ID3D11Buffer* b = cb.get();
        r.ctx->PSSetConstantBuffers(0, 1, &b);
        r.ctx->OMSetDepthStencilState(frame == 0 ? base.get() : (frame == 3 ? probe.get() : test.get()), 0);
        r.draw_fs(ps.get());
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) {
        rr::Image depth_image;
        if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
        out.add("depth", std::move(depth_image));
        return true;
    }
};

rr::Tolerance tie_tolerance() {
    rr::Tolerance t = rr::Tolerance::blend(2).with_edge(2);
    t.outlier_fraction = 0.0005;
    t.outlier_max_delta = 255;
    t.max_outliers = 8;
    return t;
}

struct DepthRegistrar {
    DepthRegistrar() {
        struct F {
            const char* name;
            D3D11_COMPARISON_FUNC func;
        };
        static const F funcs[] = {{"never", D3D11_COMPARISON_NEVER},         {"less", D3D11_COMPARISON_LESS},
                                  {"equal", D3D11_COMPARISON_EQUAL},         {"less_equal", D3D11_COMPARISON_LESS_EQUAL},
                                  {"greater", D3D11_COMPARISON_GREATER},     {"not_equal", D3D11_COMPARISON_NOT_EQUAL},
                                  {"greater_equal", D3D11_COMPARISON_GREATER_EQUAL}, {"always", D3D11_COMPARISON_ALWAYS}};
        for (const F& f : funcs) {
            const D3D11_COMPARISON_FUNC func = f.func;
            AddState<DepthMf>(spec("om", (std::string("depth_func_") + f.name).c_str()).frames(4, {1, 3}).tolerance(tie_tolerance()), [func] { return DepthMf(func, true); });
        }
        AddState<DepthMf>(spec("om", "depth_write_disabled").frames(4, {1, 3}).tolerance(tie_tolerance()), [] { return DepthMf(D3D11_COMPARISON_GREATER, false); });
    }
};
const DepthRegistrar kDepthRegistrar;

struct StencilOpRow {
    const char* name;
    D3D11_STENCIL_OP op;
};

const StencilOpRow kStencilOps[] = {{"keep", D3D11_STENCIL_OP_KEEP},       {"zero", D3D11_STENCIL_OP_ZERO},
                                    {"replace", D3D11_STENCIL_OP_REPLACE}, {"incr_sat", D3D11_STENCIL_OP_INCR_SAT},
                                    {"decr_sat", D3D11_STENCIL_OP_DECR_SAT}, {"invert", D3D11_STENCIL_OP_INVERT},
                                    {"incr", D3D11_STENCIL_OP_INCR},       {"decr", D3D11_STENCIL_OP_DECR}};

struct StencilMf {
    D3D11_STENCIL_OP op;
    Depth depth;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11Buffer> zb;
    Com<ID3D11RasterizerState> rs;
    Com<ID3D11DepthStencilState> init_state, prefill, on_pass, on_fail, on_zfail;

    explicit StencilMf(D3D11_STENCIL_OP o) : op(o) {}
    std::string needs(Rig&) { return {}; }
    Com<ID3D11DepthStencilState> make(Rig& r, bool depth_on, D3D11_COMPARISON_FUNC depth_func, bool depth_write, D3D11_COMPARISON_FUNC sfunc, D3D11_STENCIL_OP fail, D3D11_STENCIL_OP zfail, D3D11_STENCIL_OP pass) {
        D3D11_DEPTH_STENCIL_DESC d = ddesc(depth_on, depth_func, depth_write);
        d.StencilEnable = TRUE;
        d.FrontFace = {fail, zfail, pass, sfunc};
        d.BackFace = d.FrontFace;
        return r.dss(d);
    }
    bool init(Rig& r) {
        vs = r.vs(Hlsl3d::kFsVsZ);
        zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
        D3D11_RASTERIZER_DESC rd = rdesc();
        rd.ScissorEnable = TRUE;
        rs = r.rast(rd);
        const auto K = D3D11_STENCIL_OP_KEEP;
        const auto R = D3D11_STENCIL_OP_REPLACE;
        init_state = make(r, false, D3D11_COMPARISON_ALWAYS, false, D3D11_COMPARISON_ALWAYS, R, R, R);
        prefill = make(r, true, D3D11_COMPARISON_ALWAYS, true, D3D11_COMPARISON_ALWAYS, K, K, K);
        on_pass = make(r, false, D3D11_COMPARISON_ALWAYS, false, D3D11_COMPARISON_ALWAYS, K, K, op);
        on_fail = make(r, false, D3D11_COMPARISON_ALWAYS, false, D3D11_COMPARISON_NEVER, op, K, K);
        on_zfail = make(r, true, D3D11_COMPARISON_LESS, false, D3D11_COMPARISON_ALWAYS, K, op, K);
        return vs && zb && rs && init_state && prefill && on_pass && on_fail && on_zfail && r.depth(depth, 128, 128, DXGI_FORMAT_D24_UNORM_S8_UINT);
    }
    void fullscreen(Rig& r, float z, const D3D11_RECT& rect, ID3D11DepthStencilState* s, UINT ref, int times) {
        const float zc[4] = {z, 0, 0, 0};
        r.ctx->UpdateSubresource(zb.get(), 0, nullptr, zc, 0, 0);
        r.ctx->RSSetScissorRects(1, &rect);
        r.ctx->OMSetDepthStencilState(s, ref);
        for (int i = 0; i < times; ++i) r.ctx->Draw(3, 0);
    }
    bool step(Rig& r, unsigned frame) {
        ID3D11DeviceContext* c = r.ctx;
        c->OMSetRenderTargets(0, nullptr, depth.view.get());
        r.viewport(0, 0, 128, 128);
        c->RSSetState(rs.get());
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.get(), nullptr, 0);
        c->PSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* cb = zb.get();
        c->VSSetConstantBuffers(0, 1, &cb);
        const D3D11_RECT bands[3] = {{0, 0, 128, 42}, {0, 42, 128, 85}, {0, 85, 128, 128}};
        if (frame == 0) {
            c->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 250);
            fullscreen(r, 0.5f, {0, 0, 64, 128}, init_state.get(), 2, 1);
            fullscreen(r, 0.2f, bands[2], prefill.get(), 0, 1);
            return true;
        }
        fullscreen(r, 0.5f, bands[0], on_pass.get(), 77, 3);
        fullscreen(r, 0.5f, bands[1], on_fail.get(), 77, 3);
        fullscreen(r, 0.8f, bands[2], on_zfail.get(), 77, 3);
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) {
        rr::Image stencil;
        if (!r.kit.read_stencil(depth, stencil, r.error)) return false;
        out.add("stencil", std::move(stencil));
        return true;
    }
};

struct StencilRegistrar {
    StencilRegistrar() {
        for (const StencilOpRow& row : kStencilOps) {
            const D3D11_STENCIL_OP op = row.op;
            AddState<StencilMf>(spec("om", (std::string("stencil_op_") + row.name).c_str()).frames(4, {0, 1, 3}).uniform_ok("stencil"), [op] { return StencilMf(op); });
        }
    }
};
const StencilRegistrar kStencilRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d11
