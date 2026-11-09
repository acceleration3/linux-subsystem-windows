#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const D3D10_COMPARISON_FUNC kFuncs[8] = {D3D10_COMPARISON_NEVER, D3D10_COMPARISON_LESS, D3D10_COMPARISON_EQUAL, D3D10_COMPARISON_LESS_EQUAL,
                                         D3D10_COMPARISON_GREATER, D3D10_COMPARISON_NOT_EQUAL, D3D10_COMPARISON_GREATER_EQUAL, D3D10_COMPARISON_ALWAYS};
const char* kFuncNames[8] = {"never", "less", "equal", "less_equal", "greater", "not_equal", "greater_equal", "always"};
const D3D10_STENCIL_OP kOps[8] = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_ZERO, D3D10_STENCIL_OP_REPLACE, D3D10_STENCIL_OP_INCR_SAT,
                                  D3D10_STENCIL_OP_DECR_SAT, D3D10_STENCIL_OP_INVERT, D3D10_STENCIL_OP_INCR, D3D10_STENCIL_OP_DECR};
const char* kOpNames[8] = {"keep", "zero", "replace", "incr_sat", "decr_sat", "invert", "incr", "decr"};

rr::CaseSpec dspec(const std::string& name) { return spec("om", name).uniform_ok("stencil").uniform_ok("depth").uniform_ok(""); }

struct DsHarness {
    explicit DsHarness(Rig& rig, DXGI_FORMAT format) : r(rig) {
        r.target(r.main, 128, 128);
        r.depth_target(r.depth_main, 128, 128, format);
        if (!r.ok()) return;
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.05f, 0.05f, 0.1f, 1);
        r.clear_depth(r.depth_main, 1.0f, 0);
        r.viewport(128, 128);
        r.use_std();
        r.no_cull();
    }

    void state(const D3D10_DEPTH_STENCIL_DESC& desc, UINT ref) {
        Com<ID3D10DepthStencilState> s = r.depth(desc);
        if (r.ok()) r.d->OMSetDepthStencilState(s.get(), ref);
    }

    Rig& r;
};

std::vector<Vtx> ramp_layer(float d0, float d1, float d2, unsigned colour, unsigned) {
    std::vector<Vtx> out;
    const float deltas[3] = {d0, d1, d2};
    for (int k = 0; k < 3; ++k) {
        const float x0 = -0.9f + 0.6f * k, x1 = x0 + 0.6f;
        std::vector<Vtx> q = Geo::quad(x0, -0.8f, x1, 0.8f, 0.5f, palette(colour * 3 + k)[0], palette(colour * 3 + k)[1], palette(colour * 3 + k)[2]);
        for (auto& v : q) v.z = 0.3f + 0.25f * (v.x + 0.9f) + deltas[k];
        out.insert(out.end(), q.begin(), q.end());
    }
    return out;
}

struct DsState {
    std::unique_ptr<DsHarness> h;
    std::unique_ptr<ScenePipe> pipe;
    MeshBuffers m, t;
};

void func_setup(Rig& r, DsState& s, DXGI_FORMAT format, bool spheres, unsigned seed) {
    s.h = std::make_unique<DsHarness>(r, format);
    if (!r.ok()) return;
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    dd.DepthFunc = D3D10_COMPARISON_ALWAYS;
    if (!spheres) {
        s.h->state(dd, 0);
        r.draw_std(ramp_layer(0.0f, 0.0f, 0.0f, 0, 0));
    } else {
        dd.DepthFunc = D3D10_COMPARISON_LESS;
        s.pipe = std::make_unique<ScenePipe>(r);
        s.m = upload(r, Geo::sphere(14, 20, 1.0f));
        s.t = upload(r, Geo::torus(20, 10, 1.0f, 0.4f));
        s.h->state(dd, 0);
        s.pipe->bind(r);
        s.pipe->draw(r, s.m, camera(0.4f + 0.12f * static_cast<float>(seed), 0.3f, 4.5f) * rr::Mat4::translate(-0.35f, 0, 0));
    }
}

void func_frame(Rig& r, DsState& s, D3D10_COMPARISON_FUNC func, bool spheres, unsigned f, unsigned seed) {
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    dd.DepthFunc = func;
    s.h->state(dd, 0);
    if (!spheres) {
        const float deltas[3][3] = {{0.0f, 0.06f, -0.06f}, {-0.06f, 0.0f, 0.06f}, {0.06f, -0.06f, 0.0f}};
        r.draw_std(ramp_layer(deltas[f % 3][0], deltas[f % 3][1], deltas[f % 3][2], f + 1, f));
    } else {
        s.pipe->bind(r);
        s.pipe->draw(r, s.t, camera(0.4f + 0.12f * static_cast<float>(seed), 0.3f, 4.5f) * rr::Mat4::translate(0.45f - 0.2f * 0.18f * static_cast<float>(f), 0.1f, 0.2f) * rr::Mat4::rotate_x(1.2f + 0.3f * static_cast<float>(f)));
    }
}

void func_case(const std::string& name, DXGI_FORMAT format, D3D10_COMPARISON_FUNC func, bool spheres, float depth_tol, Caps caps = nullptr, unsigned seed = 0) {
    MultiBody<DsState> body;
    body.setup = [format, spheres, seed](Rig& r, DsState& s) { func_setup(r, s, format, spheres, seed); };
    body.frame = [func, spheres, seed](Rig& r, DsState& s, unsigned f) { func_frame(r, s, (!spheres && func == D3D10_COMPARISON_EQUAL && r.mutate("om_equal")) ? D3D10_COMPARISON_NEVER : func, spheres, f, seed); };
    body.capture = [](Rig& r, DsState&, unsigned, rr::CaptureSet& o) {
        r.cap_main(o);
        r.cap_depth(o, "depth", r.depth_main);
    };
    rr::CaseSpec cs = dspec(name);
    if (!spheres) cs.audit_ok("depth captures coincide when an equal-test pass writes the depth that is already stored; the colour captures differ");
    multi<DsState>(cs.frames(func == D3D10_COMPARISON_NEVER && !spheres ? 1 : 3, func == D3D10_COMPARISON_NEVER && !spheres ? std::vector<unsigned>{0} : std::vector<unsigned>{0, 2}).tolerance(spheres ? scene_tol(12) : rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(depth_tol, 0.0f).with_outliers(spheres ? 1e-3 : 2e-4)), body, std::move(caps));
}

struct StencilState {
    std::unique_ptr<DsHarness> h;
};

void stencil_op_case(const std::string& name, D3D10_STENCIL_OP pass_op, D3D10_STENCIL_OP fail_op, D3D10_STENCIL_OP zfail_op, D3D10_COMPARISON_FUNC func, bool overlap) {
    MultiBody<StencilState> body;
    const bool up = pass_op == D3D10_STENCIL_OP_INCR || pass_op == D3D10_STENCIL_OP_INCR_SAT;
    const bool down = pass_op == D3D10_STENCIL_OP_DECR || pass_op == D3D10_STENCIL_OP_DECR_SAT;
    const int base = up ? 252 : down ? 3 : 5;
    const int step = down ? -1 : 1;
    body.setup = [=](Rig& r, StencilState& s) {
        s.h = std::make_unique<DsHarness>(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.StencilEnable = TRUE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
        dd.BackFace = dd.FrontFace;
        for (int k = 0; k < 4; ++k) {
            s.h->state(dd, static_cast<UINT>(base + step * k));
            r.draw_std(Geo::quad(-1.0f + 0.5f * k, -1, -0.5f + 0.5f * k, 1, 0.9f, 0, 0, 0));
        }
    };
    body.frame = [=](Rig& r, StencilState& s, unsigned f) {
        auto dd = Rig::depth_desc();
        dd.DepthEnable = overlap ? TRUE : FALSE;
        dd.StencilEnable = TRUE;
        D3D10_STENCIL_OP effective = pass_op;
        if (r.mutate("om_sat")) {
            effective = pass_op == D3D10_STENCIL_OP_INCR_SAT ? D3D10_STENCIL_OP_INCR : pass_op == D3D10_STENCIL_OP_DECR_SAT ? D3D10_STENCIL_OP_DECR : pass_op;
        }
        dd.FrontFace = {fail_op, zfail_op, effective, func};
        dd.BackFace = dd.FrontFace;
        s.h->state(dd, 3);
        const float shift = 0.25f * static_cast<float>(f);
        std::vector<Vtx> a = Geo::quad(-0.9f + shift, -0.6f, 0.4f + shift, 0.9f - shift, 0.4f, 0.9f, 0.4f, 0.2f);
        std::vector<Vtx> b = Geo::quad(-0.3f - shift, -0.9f + shift, 0.9f - shift, 0.5f, 0.6f, 0.2f, 0.6f, 0.95f);
        if (overlap) {
            for (auto& v : a) v.z = 0.3f;
            for (auto& v : b) v.z = 0.6f;
        }
        r.draw_std(a);
        r.draw_std(b);
    };
    body.capture = [](Rig& r, StencilState&, unsigned, rr::CaptureSet& o) { r.cap_stencil(o, "stencil", r.depth_main); };
    rr::CaseSpec cs = dspec(name);
    if (name == "stencil_pass_op_keep") cs.audit_ok("KEEP leaves the seeded bands untouched in every frame by design");
    if (name == "stencil_pass_op_replace" || name == "stencil_fail_op_replace") cs.audit_ok("REPLACE through the pass op under ALWAYS and through the fail op under NEVER write the same reference everywhere by design");
    multi<StencilState>(cs.frames(3, {0, 2}).tolerance(rr::Tolerance::blend()), body);
}

void stencil_func_case(const std::string& name, D3D10_COMPARISON_FUNC func) {
    MultiBody<StencilState> body;
    body.setup = [](Rig& r, StencilState& s) {
        s.h = std::make_unique<DsHarness>(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.StencilEnable = TRUE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
        dd.BackFace = dd.FrontFace;
        for (int band = 0; band < 4; ++band) {
            s.h->state(dd, static_cast<UINT>(band + 1));
            const float* c = palette(static_cast<unsigned>(band));
            r.draw_std(Geo::quad(-1, 0.5f - 0.5f * band, 1, 1.0f - 0.5f * band, 0.5f, c[0] * 0.5f, c[1] * 0.5f, c[2] * 0.5f));
        }
    };
    body.frame = [func](Rig& r, StencilState& s, unsigned f) {
        auto dd = Rig::depth_desc();
        dd.StencilEnable = TRUE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, func};
        dd.BackFace = dd.FrontFace;
        s.h->state(dd, 2 + f);
        std::vector<Vtx> q = Geo::quad(-0.9f + 0.4f * static_cast<float>(f), -0.9f, -0.3f + 0.4f * static_cast<float>(f), 0.9f, 0.5f, 0.9f - 0.3f * static_cast<float>(f), 0.9f, 0.3f + 0.3f * static_cast<float>(f));
        r.draw_std(q);
    };
    body.capture = [](Rig& r, StencilState&, unsigned, rr::CaptureSet& o) { r.cap_main(o); };
    const bool never = func == D3D10_COMPARISON_NEVER;
    multi<StencilState>(dspec(name).frames(never ? 1 : 3, never ? std::vector<unsigned>{0} : std::vector<unsigned>{0, 2}).tolerance(rr::Tolerance::blend()), body);
}

const Registrar kDepth{[] {
    for (int f = 0; f < 8; ++f) {
        func_case(std::string("depth_func_") + kFuncNames[f], DXGI_FORMAT_D32_FLOAT, kFuncs[f], false, 1e-6f);
    }
    struct Fmt {
        const char* name;
        DXGI_FORMAT format;
        float depth_tol;
    };
    unsigned depth_seed = 1;
    for (const Fmt& fm : {Fmt{"d16", DXGI_FORMAT_D16_UNORM, 2e-5f}, Fmt{"d24s8", DXGI_FORMAT_D24_UNORM_S8_UINT, 3e-6f}, Fmt{"d32f", DXGI_FORMAT_D32_FLOAT, 3e-6f},
                          Fmt{"d32fs8x24", DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 3e-6f}}) {
        const Fmt copy = fm;
        func_case(std::string("depth_format_") + fm.name + "_intersecting_meshes", fm.format, D3D10_COMPARISON_LESS, true, fm.depth_tol,
                  [copy](Rig& r) -> std::string { return (r.format_support(copy.format) & D3D10_FORMAT_SUPPORT_DEPTH_STENCIL) != 0 ? "" : std::string("depth format ") + copy.name; }, depth_seed++);
    }
    for (int i = 0; i < 8; ++i) {
        stencil_op_case(std::string("stencil_pass_op_") + kOpNames[i], kOps[i], D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS, false);
    }
    stencil_op_case("stencil_fail_op_replace", D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_NEVER, false);
    stencil_op_case("stencil_depth_fail_op_incr", D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_INCR, D3D10_COMPARISON_ALWAYS, true);
    for (int f = 0; f < 8; ++f) {
        stencil_func_case(std::string("stencil_func_") + kFuncNames[f], kFuncs[f]);
    }
    single(dspec("stencil_two_sided_front_incr_back_replace_order").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, rr::CaptureSet& o) {
        DsHarness h(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.StencilEnable = TRUE;
        const D3D10_STENCIL_OP front = r.mutate("om_two_sided") ? D3D10_STENCIL_OP_REPLACE : D3D10_STENCIL_OP_INCR_SAT;
        const D3D10_STENCIL_OP back = r.mutate("om_two_sided") ? D3D10_STENCIL_OP_INCR_SAT : D3D10_STENCIL_OP_REPLACE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, front, D3D10_COMPARISON_ALWAYS};
        dd.BackFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, back, D3D10_COMPARISON_ALWAYS};
        h.state(dd, 9);
        ScenePipe pipe(r);
        MeshBuffers m = upload(r, Geo::sphere(12, 18, 1.0f));
        pipe.bind(r);
        pipe.draw(r, m, camera(0.3f, 0.2f, 4.0f));
        pipe.draw(r, m, camera(0.3f, 0.2f, 4.0f) * rr::Mat4::translate(0.6f, 0.2f, 0.4f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f));
        r.cap_main(o);
        r.cap_stencil(o, "stencil", r.depth_main);
    });
    for (int which = 0; which < 2; ++which) {
        single(dspec(which ? "stencil_write_mask" : "stencil_read_mask").audit_ok("the read-mask and write-mask scenes reach the same final picture through different state by design").tolerance(rr::Tolerance::blend()), [which](Rig& r, rr::CaptureSet& o) {
            DsHarness h(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
            if (!r.ok()) return;
            r.clear_depth(r.depth_main, 1.0f, 0xf0);
            auto dd = Rig::depth_desc();
            dd.StencilEnable = TRUE;
            dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
            dd.BackFace = dd.FrontFace;
            dd.StencilWriteMask = which ? 0x0f : 0xff;
            h.state(dd, 0x5a);
            r.draw_std(Geo::quad(-0.9f, -0.2f, 0.9f, 0.9f, 0.5f, 0.9f, 0.4f, 0.2f));
            dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_EQUAL};
            dd.BackFace = dd.FrontFace;
            dd.StencilReadMask = which ? 0xff : 0x0f;
            h.state(dd, which ? 0xfa : 0x0a);
            r.draw_std(Geo::quad(-1, -1, 1, 1, 0.5f, 0.2f, 0.6f, 0.9f));
            r.cap_main(o);
            r.cap_stencil(o, "stencil", r.depth_main);
        });
    }
    single(dspec("depth_write_mask_zero").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        DsHarness h(r, DXGI_FORMAT_D32_FLOAT);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ZERO;
        h.state(dd, 0);
        r.draw_std(Geo::quad(-0.9f, -0.9f, 0.5f, 0.5f, 0.3f, 0.9f, 0.4f, 0.2f));
        r.draw_std(Geo::quad(-0.5f, -0.5f, 0.9f, 0.9f, 0.6f, 0.2f, 0.6f, 0.95f));
        r.draw_std(Geo::quad(-0.7f, -0.2f, 0.7f, 0.2f, 0.45f, 0.9f, 0.9f, 0.3f));
        r.cap_main(o);
        r.cap_depth(o, "depth", r.depth_main);
    });
    single(dspec("depth_test_disabled_draw_order").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        DsHarness h(r, DXGI_FORMAT_D32_FLOAT);
        if (!r.ok()) return;
        h.state(Rig::depth_desc(), 0);
        r.draw_std(Geo::quad(-0.9f, -0.9f, 0.5f, 0.5f, 0.9f, 0.9f, 0.4f, 0.2f));
        r.draw_std(Geo::quad(-0.5f, -0.5f, 0.9f, 0.9f, 0.1f, 0.2f, 0.6f, 0.95f));
        r.cap_main(o);
        r.cap_depth(o, "depth", r.depth_main);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
