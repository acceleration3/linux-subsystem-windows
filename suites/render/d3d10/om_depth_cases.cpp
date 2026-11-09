#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const D3D10_COMPARISON_FUNC kFuncs[8] = {D3D10_COMPARISON_NEVER, D3D10_COMPARISON_LESS, D3D10_COMPARISON_EQUAL, D3D10_COMPARISON_LESS_EQUAL,
                                         D3D10_COMPARISON_GREATER, D3D10_COMPARISON_NOT_EQUAL, D3D10_COMPARISON_GREATER_EQUAL, D3D10_COMPARISON_ALWAYS};
const char* kFuncNames[8] = {"never", "less", "equal", "less_equal", "greater", "not_equal", "greater_equal", "always"};
const D3D10_STENCIL_OP kOps[8] = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_ZERO, D3D10_STENCIL_OP_REPLACE, D3D10_STENCIL_OP_INCR_SAT,
                                  D3D10_STENCIL_OP_DECR_SAT, D3D10_STENCIL_OP_INVERT, D3D10_STENCIL_OP_INCR, D3D10_STENCIL_OP_DECR};
const char* kOpNames[8] = {"keep", "zero", "replace", "incr_sat", "decr_sat", "invert", "incr", "decr"};

rr::CaseSpec dspec(const std::string& name) { return spec("om", name).uniform_ok("stencil").uniform_ok("depth"); }

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

void func_scene(Rig& r, rr::CaptureSet& o, DXGI_FORMAT format, D3D10_COMPARISON_FUNC func, bool spheres) {
    DsHarness h(r, format);
    if (!r.ok()) return;
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    if (!spheres) {
        h.state(dd, 0);
        std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.9f, 0.4f, 0.2f);
        std::vector<Vtx> b = Geo::quad(-0.7f, -0.8f, 0.8f, 0.7f, 0.5f, 0.2f, 0.6f, 0.95f);
        for (auto& v : b) v.z = 0.2f + 0.6f * (v.x + 0.7f) / 1.5f;
        r.draw_std(a);
        dd.DepthFunc = func;
        h.state(dd, 0);
        r.draw_std(b);
    } else {
        ScenePipe pipe(r);
        MeshBuffers m = upload(r, Geo::sphere(14, 20, 1.0f));
        MeshBuffers t = upload(r, Geo::torus(20, 10, 1.0f, 0.4f));
        h.state(dd, 0);
        pipe.bind(r);
        pipe.draw(r, m, camera(0.4f, 0.3f, 4.5f) * rr::Mat4::translate(-0.35f, 0, 0));
        dd.DepthFunc = func;
        h.state(dd, 0);
        pipe.draw(r, t, camera(0.4f, 0.3f, 4.5f) * rr::Mat4::translate(0.45f, 0.1f, 0.2f) * rr::Mat4::rotate_x(1.2f));
    }
    r.cap_main(o);
    r.cap_depth(o, "depth", r.depth_main);
}

void stencil_op_scene(Rig& r, rr::CaptureSet& o, D3D10_STENCIL_OP pass_op, D3D10_STENCIL_OP fail_op, D3D10_STENCIL_OP zfail_op, D3D10_COMPARISON_FUNC func,
                      bool second_overlap) {
    DsHarness h(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
    if (!r.ok()) return;
    r.clear_depth(r.depth_main, 1.0f, 5);
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    dd.StencilEnable = TRUE;
    dd.FrontFace = {fail_op, zfail_op, pass_op, func};
    dd.BackFace = dd.FrontFace;
    h.state(dd, 3);
    std::vector<Vtx> a = Geo::quad(-0.9f, -0.6f, 0.4f, 0.9f, 0.4f, 0.9f, 0.4f, 0.2f);
    std::vector<Vtx> b = Geo::quad(-0.3f, -0.9f, 0.9f, 0.5f, 0.6f, 0.2f, 0.6f, 0.95f);
    if (second_overlap) {
        for (auto& v : a) v.z = 0.6f;
        for (auto& v : b) v.z = 0.3f;
    }
    r.draw_std(a);
    r.draw_std(b);
    r.cap_main(o);
    r.cap_stencil(o, "stencil", r.depth_main);
}

void stencil_func_scene(Rig& r, rr::CaptureSet& o, D3D10_COMPARISON_FUNC func) {
    DsHarness h(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
    if (!r.ok()) return;
    auto dd = Rig::depth_desc();
    dd.StencilEnable = TRUE;
    dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
    dd.BackFace = dd.FrontFace;
    for (int band = 0; band < 4; ++band) {
        h.state(dd, static_cast<UINT>(band + 1));
        r.draw_std(Geo::quad(-1, 0.5f - 0.5f * band, 1, 1.0f - 0.5f * band, 0.5f, 0, 0, 0));
    }
    dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, func};
    dd.BackFace = dd.FrontFace;
    h.state(dd, 2);
    std::vector<Vtx> q = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.3f, 0.9f, 0.5f);
    for (auto& v : q) v.r = 0.3f + 0.6f * (v.x * 0.5f + 0.5f);
    r.draw_std(q);
    r.cap_main(o);
    r.cap_stencil(o, "stencil", r.depth_main);
}

const Registrar kDepth{[] {
    for (int f = 0; f < 8; ++f) {
        single(dspec(std::string("depth_func_") + kFuncNames[f]).tolerance(rr::Tolerance::blend()).tolerance_for("depth", rr::Tolerance::floats(1e-4f, 1e-4f)),
               [f](Rig& r, rr::CaptureSet& o) { func_scene(r, o, DXGI_FORMAT_D32_FLOAT, kFuncs[f], false); });
    }
    struct Fmt {
        const char* name;
        DXGI_FORMAT format;
        float depth_tol;
    };
    for (const Fmt& fm : {Fmt{"d16", DXGI_FORMAT_D16_UNORM, 3e-3f}, Fmt{"d24s8", DXGI_FORMAT_D24_UNORM_S8_UINT, 3e-4f}, Fmt{"d32f", DXGI_FORMAT_D32_FLOAT, 1e-4f},
                          Fmt{"d32fs8x24", DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1e-4f}}) {
        const Fmt copy = fm;
        single(dspec(std::string("depth_format_") + fm.name + "_intersecting_meshes").tolerance(rr::Tolerance::raster(2, 1)).tolerance_for("depth", rr::Tolerance::floats(fm.depth_tol, fm.depth_tol)),
               [copy](Rig& r, rr::CaptureSet& o) { func_scene(r, o, copy.format, D3D10_COMPARISON_LESS, true); },
               [copy](Rig& r) -> std::string { return (r.format_support(copy.format) & D3D10_FORMAT_SUPPORT_DEPTH_STENCIL) != 0 ? "" : std::string("depth format ") + copy.name; });
    }
    for (int i = 0; i < 8; ++i) {
        single(dspec(std::string("stencil_pass_op_") + kOpNames[i]).tolerance(rr::Tolerance::blend()),
               [i](Rig& r, rr::CaptureSet& o) { stencil_op_scene(r, o, kOps[i], D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS, false); });
    }
    single(dspec("stencil_fail_op_replace").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { stencil_op_scene(r, o, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_NEVER, false); });
    single(dspec("stencil_depth_fail_op_incr").tolerance(rr::Tolerance::blend()),
           [](Rig& r, rr::CaptureSet& o) { stencil_op_scene(r, o, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_INCR, D3D10_COMPARISON_ALWAYS, true); });
    for (int f = 0; f < 8; ++f) {
        single(dspec(std::string("stencil_func_") + kFuncNames[f]).tolerance(rr::Tolerance::blend()), [f](Rig& r, rr::CaptureSet& o) { stencil_func_scene(r, o, kFuncs[f]); });
    }
    single(dspec("stencil_two_sided_sphere_count").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, rr::CaptureSet& o) {
        DsHarness h(r, DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.StencilEnable = TRUE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_INCR, D3D10_COMPARISON_ALWAYS};
        dd.BackFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_DECR, D3D10_COMPARISON_ALWAYS};
        h.state(dd, 0);
        ScenePipe pipe(r);
        MeshBuffers m = upload(r, Geo::sphere(12, 18, 1.0f));
        pipe.bind(r);
        pipe.draw(r, m, camera(0.3f, 0.2f, 4.0f));
        pipe.draw(r, m, camera(0.3f, 0.2f, 4.0f) * rr::Mat4::translate(0.6f, 0.2f, 0.4f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f));
        r.cap_main(o);
        r.cap_stencil(o, "stencil", r.depth_main);
    });
    for (int which = 0; which < 2; ++which) {
        single(dspec(which ? "stencil_write_mask" : "stencil_read_mask").tolerance(rr::Tolerance::blend()), [which](Rig& r, rr::CaptureSet& o) {
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
