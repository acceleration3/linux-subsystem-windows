#include "render/d3d9/harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lsw::suites::render::d3d9 {

namespace {

void background(Ctx& c) {
    c.clear(Dx::kColor(10, 10, 10, 255));
    c.quad(0, 0, 128, 128, 0.9f, 0xE0FF4040, 0x60FFD040, 0xA040C0FF, 0xFF8040FF);
}

void overlay(Ctx& c) {
    c.quad(16, 16, 112, 112, 0.5f, 0xFF2090E0, 0x70E0E020, 0xB0E02080, 0x30FFFFFF);
}

struct BlendInfo {
    const char* name;
    DWORD factor;
};

const BlendInfo kSrc[] = {
    {"zero", D3DBLEND_ZERO}, {"one", D3DBLEND_ONE}, {"srccolor", D3DBLEND_SRCCOLOR}, {"invsrccolor", D3DBLEND_INVSRCCOLOR},
    {"srcalpha", D3DBLEND_SRCALPHA}, {"invsrcalpha", D3DBLEND_INVSRCALPHA}, {"destalpha", D3DBLEND_DESTALPHA}, {"invdestalpha", D3DBLEND_INVDESTALPHA},
    {"destcolor", D3DBLEND_DESTCOLOR}, {"invdestcolor", D3DBLEND_INVDESTCOLOR}, {"srcalphasat", D3DBLEND_SRCALPHASAT},
    {"bothsrcalpha", D3DBLEND_BOTHSRCALPHA}, {"bothinvsrcalpha", D3DBLEND_BOTHINVSRCALPHA}, {"blendfactor", D3DBLEND_BLENDFACTOR},
    {"invblendfactor", D3DBLEND_INVBLENDFACTOR},
};

Need blend_need() {
    return Needs::caps([](const D3DCAPS9& caps) { return (caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) != 0; }, "blend operations");
}

void blend_cases() {
    for (const BlendInfo& s : kSrc) {
        rr::CaseSpec src_spec = rr::CaseSpec("rs", std::string("blend_src_") + s.name).tolerance(rr::Tolerance::blend(3));
        if (std::string(s.name) == "srcalpha") {
            src_spec.same_as({"rs.blend_src_bothsrcalpha"}, "BOTHSRCALPHA is defined as SRCALPHA with INVSRCALPHA and the row sets exactly that destination, so the images are identical by design; ZERO or ONE for either factor would show");
        } else if (std::string(s.name) == "bothsrcalpha") {
            src_spec.same_as({"rs.blend_src_srcalpha"}, "BOTHSRCALPHA is defined as SRCALPHA with INVSRCALPHA and overrides the destination factor, so the image equals the SRCALPHA row by design; ZERO or ONE for either factor would show");
        }
        Cases::single(src_spec, [s](Ctx& c) {
            background(c);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_BLENDFACTOR, 0xFF4080C0);
            c.rs(D3DRS_SRCBLEND, s.factor);
            c.rs(D3DRS_DESTBLEND, s.factor == D3DBLEND_BOTHSRCALPHA || s.factor == D3DBLEND_BOTHINVSRCALPHA ? D3DBLEND_ZERO : D3DBLEND_INVSRCALPHA);
            overlay(c);
        }, {}, s.factor == D3DBLEND_BLENDFACTOR || s.factor == D3DBLEND_INVBLENDFACTOR
                   ? Needs::caps([](const D3DCAPS9& caps) { return (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0; }, "blend factor")
                   : Need());
    }
    for (const BlendInfo& d : kSrc) {
        if (d.factor == D3DBLEND_SRCALPHASAT || d.factor == D3DBLEND_BOTHSRCALPHA || d.factor == D3DBLEND_BOTHINVSRCALPHA || d.factor == D3DBLEND_INVSRCALPHA) {
            continue;
        }
        Cases::single(rr::CaseSpec("rs", std::string("blend_dest_") + d.name).tolerance(rr::Tolerance::blend(3)), [d](Ctx& c) {
            background(c);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_BLENDFACTOR, 0xFF4080C0);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCCOLOR);
            c.rs(D3DRS_DESTBLEND, d.factor);
            overlay(c);
        }, {}, d.factor == D3DBLEND_BLENDFACTOR || d.factor == D3DBLEND_INVBLENDFACTOR
                   ? Needs::caps([](const D3DCAPS9& caps) { return (caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0; }, "blend factor")
                   : Need());
    }
    struct Op {
        const char* name;
        DWORD op;
    };
    const Op ops[] = {{"add", D3DBLENDOP_ADD}, {"subtract", D3DBLENDOP_SUBTRACT}, {"revsubtract", D3DBLENDOP_REVSUBTRACT}, {"min", D3DBLENDOP_MIN}, {"max", D3DBLENDOP_MAX}};
    for (const Op& o : ops) {
        Cases::single(rr::CaseSpec("rs", std::string("blendop_") + o.name).tolerance(rr::Tolerance::blend(3)), [o](Ctx& c) {
            background(c);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_BLENDOP, o.op);
            overlay(c);
        }, {}, blend_need());
    }
    Cases::single(rr::CaseSpec("rs", "blend_separate_alpha").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        background(c);
        c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
        c.rs(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        c.rs(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        c.rs(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
        c.rs(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
        c.rs(D3DRS_BLENDOPALPHA, D3DBLENDOP_REVSUBTRACT);
        overlay(c);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) != 0; }, "separate alpha blend"));
    Cases::single(rr::CaseSpec("rs", "blend_premultiplied_over").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        background(c);
        c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
        c.rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
        c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        c.quad(10, 10, 118, 118, 0.5f, 0x80402010, 0x80105020, 0x80201040, 0x80404040);
        c.quad(40, 40, 100, 100, 0.4f, 0x40102030);
    });
}

void write_cases() {
    const DWORD masks[] = {0, 1, 2, 4, 8, 3, 6, 12, 7, 15};
    for (DWORD m : masks) {
        Cases::single(rr::CaseSpec("rs", "colorwrite_mask_" + std::to_string(m)), [m](Ctx& c) {
            background(c);
            c.rs(D3DRS_COLORWRITEENABLE, m);
            c.quad(20, 20, 108, 108, 0.5f, 0xFF30C0A0, 0xFFF0A020, 0xFF2080F0, 0xFFF020A0);
            c.rs(D3DRS_COLORWRITEENABLE, 15);
        });
    }
    Cases::single(rr::CaseSpec("rs", "srgb_write").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255));
        c.rs(D3DRS_SRGBWRITEENABLE, TRUE);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF202020, 0xFF808080, 0xFF404040, 0xFFE0E0E0);
        c.rs(D3DRS_SRGBWRITEENABLE, FALSE);
    }, {}, Needs::format(D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_SURFACE, D3DFMT_A8R8G8B8, "sRGB writes"));
    Cases::single(rr::CaseSpec("rs", "dither_enable_renders").tolerance(rr::Tolerance::blend(10)), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255));
        c.rs(D3DRS_DITHERENABLE, TRUE);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF102030, 0xFFF0E0D0, 0xFF405060, 0xFFA09080);
    });
}

const D3DCMPFUNC kFuncs[8] = {D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_NOTEQUAL, D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS};
const char* const kFuncNames[8] = {"never", "less", "equal", "lessequal", "greater", "notequal", "greaterequal", "always"};

void test_cases() {
    for (int i = 0; i < 8; ++i) {
        Cases::single(i == 0 ? rr::CaseSpec("rs", std::string("alphatest_") + kFuncNames[i]).uniform_ok() : rr::CaseSpec("rs", std::string("alphatest_") + kFuncNames[i]), [i](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_ALPHATESTENABLE, TRUE);
            c.rs(D3DRS_ALPHAREF, 0x80);
            c.rs(D3DRS_ALPHAFUNC, kFuncs[i]);
            const unsigned alphas[7] = {0x00, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF};
            for (int k = 0; k < 7; ++k) {
                c.quad(4, 4.0f + 17.0f * static_cast<float>(k), 124, 17.0f + 17.0f * static_cast<float>(k), 0.5f, (alphas[k] << 24) | 0x00E0A040u | (static_cast<unsigned>(k) * 30u << 8));
            }
        }, {}, Needs::caps([i](const D3DCAPS9& caps) { return (caps.AlphaCmpCaps & (1u << (static_cast<unsigned>(kFuncs[i]) - 1))) != 0; }, "alpha compare function"));
    }
    for (int i = 0; i < 8; ++i) {
        rr::CaseSpec func_spec = rr::CaseSpec("rs", std::string("depthfunc_") + kFuncNames[i]);
        if (std::string(kFuncNames[i]) == "never") {
            func_spec.same_as({"rs.persistent_depth_greater"}, "NEVER draws nothing, the plane alone that GREATER against the persistent depth plane shows in its first captured frames by design; a function that passes shows");
        }
        Cases::single(func_spec, [i](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            c.quad(0, 0, 128, 128, 0.5f, 0xFF3060C0, 0xFF60C030, 0xFFC03060, 0xFF30C0C0);
            c.rs(D3DRS_ZFUNC, kFuncs[i]);
            c.quad(4, 4, 44, 124, 0.3f, 0xFFFFA020);
            c.quad(44, 4, 84, 124, 0.5f, 0xFF40E060);
            c.quad(84, 4, 124, 124, 0.7f, 0xFFE040A0);
        }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128}, Needs::caps([i](const D3DCAPS9& caps) { return (caps.ZCmpCaps & (1u << (static_cast<unsigned>(kFuncs[i]) - 1))) != 0; }, "depth compare function"));
    }
    Cases::single(rr::CaseSpec("rs", "zwrite_disabled"), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 60, 255));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        c.rs(D3DRS_ZWRITEENABLE, FALSE);
        c.quad(10, 10, 90, 90, 0.3f, 0xFFFFA020);
        c.quad(40, 40, 120, 120, 0.6f, 0xFF40E060);
        c.rs(D3DRS_ZWRITEENABLE, TRUE);
        c.quad(0, 100, 128, 128, 0.2f, 0xFF8080FF);
        c.quad(0, 100, 128, 128, 0.4f, 0xFFFF2020);
    });
    Cases::single(rr::CaseSpec("rs", "depth_bias_coplanar"), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 60, 255));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF3060C0);
        c.rs(D3DRS_ZWRITEENABLE, FALSE);
        c.rsf(D3DRS_DEPTHBIAS, -0.002f);
        c.quad(10, 10, 118, 64, 0.5f, 0xFFFFA020);
        c.rsf(D3DRS_DEPTHBIAS, 0.002f);
        c.quad(10, 70, 118, 120, 0.5f, 0xFF40E060);
        c.rsf(D3DRS_DEPTHBIAS, 0.0f);
        c.quad(60, 30, 100, 100, 0.5f, 0xFFE04080);
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0; }, "depth bias"));
    Cases::single(rr::CaseSpec("rs", "depth_bias_slope_scale"), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 60, 255));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        struct V { float x, y, z, rhw; DWORD color; };
        const V base[4] = {{0, 0, 0.2f, 1, 0xFF3060C0}, {128, 0, 0.8f, 1, 0xFF3060C0}, {0, 128, 0.2f, 1, 0xFF3060C0}, {128, 128, 0.8f, 1, 0xFF3060C0}};
        c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        c.draw(D3DPT_TRIANGLESTRIP, 2, base, sizeof(V));
        c.rs(D3DRS_ZWRITEENABLE, FALSE);
        const V plain[4] = {{8, 8, 0.2f + 0.6f * 0.0625f, 1, 0xFFE04080}, {120, 8, 0.2f + 0.6f * 0.9375f, 1, 0xFFE04080}, {8, 40, 0.2f + 0.6f * 0.0625f, 1, 0xFFE04080}, {120, 40, 0.2f + 0.6f * 0.9375f, 1, 0xFFE04080}};
        c.draw(D3DPT_TRIANGLESTRIP, 2, plain, sizeof(V));
        c.rsf(D3DRS_SLOPESCALEDEPTHBIAS, -4.0f);
        const V top[4] = {{8, 56, 0.2f + 0.6f * 0.0625f, 1, 0xFFFFA020}, {120, 56, 0.2f + 0.6f * 0.9375f, 1, 0xFFFFA020}, {8, 112, 0.2f + 0.6f * 0.0625f, 1, 0xFFFFA020}, {120, 112, 0.2f + 0.6f * 0.9375f, 1, 0xFFFFA020}};
        c.draw(D3DPT_TRIANGLESTRIP, 2, top, sizeof(V));
        c.rsf(D3DRS_SLOPESCALEDEPTHBIAS, 0.0f);
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS) != 0; }, "slope scaled depth bias"));
}

struct StencilOp {
    const char* name;
    DWORD op;
};

void stencil_visualize(Ctx& c) {
    c.rs(D3DRS_STENCILENABLE, TRUE);
    c.rs(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    c.rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    c.rs(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    c.rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    c.rs(D3DRS_STENCILMASK, 0xFF);
    const DWORD values[8] = {0x00, 0x01, 0x03, 0x55, 0xFC, 0xFD, 0xFE, 0xFF};
    const DWORD colors[8] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFF00FFFF, 0xFFFFFFFF, 0xFFFF00FF, 0xFF808080};
    for (int i = 0; i < 8; ++i) {
        c.rs(D3DRS_STENCILREF, values[i]);
        c.quad(0, 0, 128, 128, 0.5f, colors[i]);
    }
    c.rs(D3DRS_STENCILENABLE, FALSE);
}

struct StencilStart {
    const char* name;
    DWORD op;
    DWORD initial;
    unsigned repeats;
};

const StencilStart kStencilOps[] = {
    {"keep", D3DSTENCILOP_KEEP, 0xFE, 2}, {"zero", D3DSTENCILOP_ZERO, 0xFE, 2}, {"replace", D3DSTENCILOP_REPLACE, 0xFE, 2}, {"incrsat", D3DSTENCILOP_INCRSAT, 0xFE, 2},
    {"decrsat", D3DSTENCILOP_DECRSAT, 0x01, 2}, {"invert", D3DSTENCILOP_INVERT, 0xFE, 1}, {"incr", D3DSTENCILOP_INCR, 0xFE, 5}, {"decr", D3DSTENCILOP_DECR, 0x01, 2},
};

void stencil_cases() {
    for (const StencilStart& o : kStencilOps) {
        rr::CaseSpec spec("rs", std::string("stencil_op_") + o.name);
        if (o.op == D3DSTENCILOP_KEEP) {
            spec.uniform_ok();
        }
        Cases::single(spec, [o](Ctx& c) {
            c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, o.initial);
            c.rs(D3DRS_STENCILENABLE, TRUE);
            c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
            c.rs(D3DRS_STENCILREF, 0x55);
            c.rs(D3DRS_STENCILPASS, o.op);
            c.rs(D3DRS_COLORWRITEENABLE, 0);
            for (unsigned i = 0; i < o.repeats; ++i) {
                c.quad(16, 16, 112, 112, 0.5f, 0xFFFFFFFF);
            }
            c.rs(D3DRS_COLORWRITEENABLE, 15);
            stencil_visualize(c);
        }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128});
    }
    Cases::single(rr::CaseSpec("rs", "stencil_zfail_and_fail"), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 0xFE);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.quad(0, 0, 64, 128, 0.3f, 0xFF404040);
        c.rs(D3DRS_STENCILENABLE, TRUE);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
        c.rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_DECR);
        c.rs(D3DRS_STENCILFAIL, D3DSTENCILOP_REPLACE);
        c.rs(D3DRS_COLORWRITEENABLE, 0);
        c.quad(0, 0, 128, 128, 0.6f, 0xFFFFFFFF);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_NEVER);
        c.rs(D3DRS_STENCILREF, 0x55);
        c.quad(0, 0, 128, 32, 0.2f, 0xFFFFFFFF);
        c.rs(D3DRS_COLORWRITEENABLE, 15);
        stencil_visualize(c);
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128});
    Cases::single(rr::CaseSpec("rs", "stencil_two_sided"), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 0xFE);
        c.rs(D3DRS_STENCILENABLE, TRUE);
        c.rs(D3DRS_TWOSIDEDSTENCILMODE, TRUE);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_DECR);
        c.rs(D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS);
        c.rs(D3DRS_CCW_STENCILPASS, D3DSTENCILOP_INCR);
        c.rs(D3DRS_COLORWRITEENABLE, 0);
        struct V { float x, y, z, rhw; };
        const V cw[3] = {{10, 10, 0.5f, 1}, {60, 10, 0.5f, 1}, {10, 120, 0.5f, 1}};
        const V ccw[3] = {{70, 10, 0.5f, 1}, {70, 120, 0.5f, 1}, {120, 10, 0.5f, 1}};
        c.fvf(D3DFVF_XYZRHW);
        c.draw(D3DPT_TRIANGLELIST, 1, cw, sizeof(V));
        c.draw(D3DPT_TRIANGLELIST, 1, ccw, sizeof(V));
        c.rs(D3DRS_COLORWRITEENABLE, 15);
        stencil_visualize(c);
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128}, Needs::caps([](const D3DCAPS9& caps) { return (caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) != 0; }, "two sided stencil"));
    Cases::single(rr::CaseSpec("rs", "stencil_mask_and_write_mask"), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 0xFE);
        c.rs(D3DRS_STENCILENABLE, TRUE);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
        c.rs(D3DRS_STENCILREF, 0x55);
        c.rs(D3DRS_STENCILWRITEMASK, 0xF0);
        c.rs(D3DRS_COLORWRITEENABLE, 0);
        c.quad(0, 0, 128, 64, 0.5f, 0xFFFFFFFF);
        c.rs(D3DRS_STENCILWRITEMASK, 0xFF);
        c.rs(D3DRS_COLORWRITEENABLE, 15);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
        c.rs(D3DRS_STENCILMASK, 0xFF);
        c.rs(D3DRS_STENCILREF, 0x5E);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF40FF40);
        c.rs(D3DRS_STENCILMASK, 0xF0);
        c.rs(D3DRS_STENCILREF, 0xF0);
        c.quad(0, 0, 128, 128, 0.5f, 0xFFFF4040);
        c.rs(D3DRS_STENCILMASK, 0xFF);
        c.rs(D3DRS_STENCILREF, 0x55);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF4040FF);
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128});
}

struct Tri {
    float x, y, z, rhw;
    DWORD color;
};


constexpr UINT kWrapTexels = 128;

std::uint32_t wrap_texel(UINT index) {
    const double angle = 6.283185307179586 * static_cast<double>(index % kWrapTexels) / kWrapTexels;
    const unsigned r = (index % kWrapTexels) * 255 / (kWrapTexels - 1);
    const unsigned g = static_cast<unsigned>(128.0 + 127.0 * std::sin(angle));
    const unsigned b = 255 - r;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

struct WrapVertex {
    float x, y, z, rhw;
    DWORD color;
    float u0, v0, u1, v1;
};

struct WrapScene {
    const char* name;
    bool along_u;
    bool second_set;
};

const WrapScene kWrapScenes[] = {{"wrap_coord_u", true, false}, {"wrap_coord_v", false, false}, {"wrap_coord_set1_u", true, true}};

std::uint32_t sample_wrapped(double coordinate) {
    const double t = coordinate * kWrapTexels - 0.5;
    const double base = std::floor(t);
    const double f = t - base;
    const long i0 = static_cast<long>(base);
    const std::uint32_t a = wrap_texel(static_cast<UINT>(((i0 % 128) + 128) % 128));
    const std::uint32_t b = wrap_texel(static_cast<UINT>(((i0 + 1) % 128 + 128) % 128));
    std::uint32_t out = 0xFF000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const double ca = static_cast<double>((a >> shift) & 255u);
        const double cb = static_cast<double>((b >> shift) & 255u);
        out |= static_cast<std::uint32_t>(std::lround(ca + (cb - ca) * f)) << shift;
    }
    return out;
}

void wrap_scene(Ctx& c, const WrapScene& scene) {
    c.clear(Dx::kColor(10, 10, 40, 255));
    Com<IDirect3DTexture9> t = c.texture(kWrapTexels, kWrapTexels, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [&](UINT x, UINT y, UINT, UINT) { return wrap_texel(scene.along_u ? x : y); });
    c.tex(0, t.get());
    c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    if (scene.second_set) {
        c.tss(0, D3DTSS_TEXCOORDINDEX, 1);
    }
    c.rs(c.broken("wrap") ? D3DRS_WRAP7 : (scene.second_set ? D3DRS_WRAP1 : D3DRS_WRAP0), scene.along_u ? D3DWRAPCOORD_0 : D3DWRAPCOORD_1);
    const float a = scene.along_u ? 0.9f : 0.5f;
    const float b = scene.along_u ? 0.1f : 0.5f;
    const float va = scene.along_u ? 0.5f : 0.9f;
    const float vb = scene.along_u ? 0.5f : 0.1f;
    const WrapVertex quad[4] = {{8, 8, 0.5f, 1, 0xFFFFFFFF, scene.second_set ? 0.0f : a, scene.second_set ? 0.0f : va, a, va},
                                {120, 8, 0.5f, 1, 0xFFFFFFFF, scene.second_set ? 0.0f : b, scene.second_set ? 0.0f : va, b, va},
                                {8, 120, 0.5f, 1, 0xFFFFFFFF, scene.second_set ? 0.0f : a, scene.second_set ? 0.0f : vb, a, vb},
                                {120, 120, 0.5f, 1, 0xFFFFFFFF, scene.second_set ? 0.0f : b, scene.second_set ? 0.0f : vb, b, vb}};
    c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);
    c.draw(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(WrapVertex));
}

bool wrap_reference(const WrapScene& scene, rr::CaptureSet& out) {
    rr::Image image(128, 128, rr::PixelKind::Bgra8Unorm);
    for (UINT y = 0; y < 128; ++y) {
        for (UINT x = 0; x < 128; ++x) {
            std::uint32_t argb = 0xFF0A0A28u;
            if (x >= 8 && x < 120 && y >= 8 && y < 120) {
                const double along_x = static_cast<double>(x - 8) / 112.0;
                const double along_y = static_cast<double>(y - 8) / 112.0;
                argb = sample_wrapped(scene.along_u ? 0.9 + 0.2 * along_x : 0.9 + 0.2 * along_y);
            }
            image.set_rgba8(x, y, ((argb >> 16) & 255u) | (((argb >> 8) & 255u) << 8) | ((argb & 255u) << 16) | (255u << 24));
        }
    }
    out.add("", std::move(image));
    return true;
}

struct LineSegment {
    int x0, y0, x1, y1;
};

const LineSegment kLastPixelLines[] = {{10, 20, 100, 20}, {20, 30, 20, 118}, {30, 40, 90, 100}, {110, 10, 40, 80}, {10, 50, 70, 50}, {30, 60, 120, 60}, {10, 70, 110, 70},
                                       {20, 80, 105, 80}, {10, 90, 80, 90}, {30, 100, 100, 100}, {40, 25, 40, 110}, {55, 25, 55, 105}, {70, 30, 70, 115}, {85, 20, 85, 100},
                                       {100, 30, 100, 110}, {15, 115, 60, 70}, {70, 120, 120, 70}, {25, 15, 75, 65}, {80, 15, 120, 55}};

void lastpixel_scene(Ctx& c, bool last_pixel) {
    c.clear(Dx::kColor(0, 0, 0, 255));
    c.rs(D3DRS_LASTPIXEL, (last_pixel != c.broken("lastpixel")) ? TRUE : FALSE);
    struct V {
        float x, y, z, rhw;
        DWORD color;
    };
    std::vector<V> vertices;
    for (const LineSegment& line : kLastPixelLines) {
        vertices.push_back({static_cast<float>(line.x0), static_cast<float>(line.y0), 0.5f, 1.0f, 0xFFFFFFFF});
        vertices.push_back({static_cast<float>(line.x1), static_cast<float>(line.y1), 0.5f, 1.0f, 0xFFFFFFFF});
    }
    c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    c.draw(D3DPT_LINELIST, static_cast<UINT>(vertices.size() / 2), vertices.data(), sizeof(V));
}

bool lastpixel_reference(bool last_pixel, rr::CaptureSet& out) {
    rr::Image image(128, 128, rr::PixelKind::Bgra8Unorm);
    for (UINT y = 0; y < 128; ++y) {
        for (UINT x = 0; x < 128; ++x) {
            image.set_rgba8(x, y, 0xFF000000u);
        }
    }
    for (const LineSegment& line : kLastPixelLines) {
        const int steps = std::max(std::abs(line.x1 - line.x0), std::abs(line.y1 - line.y0));
        for (int k = 0; k <= steps; ++k) {
            if (k == steps && !last_pixel) {
                break;
            }
            const double t = steps == 0 ? 0.0 : static_cast<double>(k) / steps;
            const int px = static_cast<int>(std::lround(line.x0 + (line.x1 - line.x0) * t));
            const int py = static_cast<int>(std::lround(line.y0 + (line.y1 - line.y0) * t));
            image.set_rgba8(static_cast<UINT>(px), static_cast<UINT>(py), 0xFFFFFFFFu);
        }
    }
    out.add("", std::move(image));
    return true;
}

void wrap_and_lastpixel_cases() {
    for (const WrapScene& scene : kWrapScenes) {
        rr::CaseSpec spec("rs", scene.name);
        spec.tolerance(rr::Tolerance::blend(3).with_outliers(0.01));
        if (scene.second_set) {
            spec.same_as({"rs.wrap_coord_u"}, "the same quad and texture through texture coordinate set 1 with WRAP1 and TEXCOORDINDEX 1, so the wrapped image equals the set 0 row by design; a driver that ignores WRAP1 or the coordinate index changes it");
        } else if (scene.along_u) {
            spec.same_as({"rs.wrap_coord_set1_u"}, "the same quad and texture through texture coordinate set 0 with WRAP0, so the wrapped image equals the set 1 row by design; a driver that ignores WRAP0 changes it");
        }
        Cases::single(spec, [&scene](Ctx& c) { wrap_scene(c, scene); }, {}, nullptr,
                      [&scene](Kit&, unsigned, rr::CaptureSet& out, std::string&) { return wrap_reference(scene, out); });
    }
    for (int on = 0; on < 2; ++on) {
        Cases::single(rr::CaseSpec("rs", on != 0 ? "lastpixel_on" : "lastpixel_off").tolerance(rr::Tolerance::exact()), [on](Ctx& c) { lastpixel_scene(c, on != 0); }, {}, nullptr,
                      [on](Kit&, unsigned, rr::CaptureSet& out, std::string&) { return lastpixel_reference(on != 0, out); });
    }
}

void raster_cases() {
    struct CullInfo {
        const char* name;
        DWORD mode;
    };
    const CullInfo culls[] = {{"none", D3DCULL_NONE}, {"cw", D3DCULL_CW}, {"ccw", D3DCULL_CCW}};
    for (const CullInfo& cull : culls) {
        Cases::single(rr::CaseSpec("rs", std::string("cull_") + cull.name), [cull](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_CULLMODE, cull.mode);
            const Tri cw[3] = {{10, 10, 0.5f, 1, 0xFFFF4040}, {60, 10, 0.5f, 1, 0xFFFF4040}, {10, 120, 0.5f, 1, 0xFFFF4040}};
            const Tri ccw[3] = {{70, 10, 0.5f, 1, 0xFF40FF40}, {70, 120, 0.5f, 1, 0xFF40FF40}, {120, 10, 0.5f, 1, 0xFF40FF40}};
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_TRIANGLELIST, 1, cw, sizeof(Tri));
            c.draw(D3DPT_TRIANGLELIST, 1, ccw, sizeof(Tri));
        });
    }
    struct FillInfo {
        const char* name;
        DWORD mode;
    };
    const FillInfo fills[] = {{"solid", D3DFILL_SOLID}, {"wireframe", D3DFILL_WIREFRAME}, {"point", D3DFILL_POINT}};
    for (const FillInfo& f : fills) {
        Cases::single(rr::CaseSpec("rs", std::string("fill_") + f.name).tolerance(rr::Tolerance::raster(4, 1)), [f](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_FILLMODE, f.mode);
            c.rsf(D3DRS_POINTSIZE, 5.0f);
            std::vector<Tri> tris;
            const DWORD palette[6] = {0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFFFFFF40, 0xFF40FFFF, 0xFFFF40FF};
            for (unsigned k = 0; k < 24; ++k) {
                const float a0 = 6.2831853f * static_cast<float>(k) / 24.0f;
                const float a1 = 6.2831853f * static_cast<float>(k + 1) / 24.0f;
                tris.push_back({64, 64, 0.5f, 1, 0xFFFFFFFF});
                tris.push_back({64 + 56 * std::cos(a0), 64 + 56 * std::sin(a0), 0.5f, 1, palette[k % 6]});
                tris.push_back({64 + 56 * std::cos(a1), 64 + 56 * std::sin(a1), 0.5f, 1, palette[(k + 1) % 6]});
            }
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_TRIANGLELIST, 24, tris.data(), sizeof(Tri));
        });
    }
    Cases::single(rr::CaseSpec("rs", "scissor_test"), [](Ctx& c) {
        background(c);
        const RECT rect{20, 30, 100, 90};
        c.chk(c.d->SetScissorRect(&rect), "SetScissorRect");
        c.rs(D3DRS_SCISSORTESTENABLE, TRUE);
        c.quad(0, 0, 128, 128, 0.5f, 0xFF40E040, 0xFF40E0E0, 0xFFE040E0, 0xFFE0E040);
        c.rs(D3DRS_SCISSORTESTENABLE, FALSE);
        c.quad(0, 0, 8, 8, 0.5f, 0xFFFFFFFF);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0; }, "scissor test"));
    Cases::single(rr::CaseSpec("rs", "scissor_clear_interaction"), [](Ctx& c) {
        background(c);
        const RECT rect{30, 30, 90, 110};
        c.chk(c.d->SetScissorRect(&rect), "SetScissorRect");
        c.rs(D3DRS_SCISSORTESTENABLE, TRUE);
        c.clear(Dx::kColor(255, 255, 0, 255), D3DCLEAR_TARGET);
        c.rs(D3DRS_SCISSORTESTENABLE, FALSE);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0; }, "scissor test"));
    {
        const int on = 1;
        Cases::single(rr::CaseSpec("rs", "antialiased_line_renders").tolerance(rr::Tolerance::raster(32, 1).with_outliers(0.002)), [](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_ANTIALIASEDLINEENABLE, TRUE);
            const Tri lines[8] = {{10, 12, 0.5f, 1, 0xFFFFFF40}, {118, 40, 0.5f, 1, 0xFFFFFF40}, {10, 30, 0.5f, 1, 0xFF40FFFF}, {118, 100, 0.5f, 1, 0xFF40FFFF},
                                  {20, 120, 0.5f, 1, 0xFFFF40FF}, {100, 10, 0.5f, 1, 0xFFFF40FF}, {64, 5, 0.5f, 1, 0xFFFFFFFF}, {64, 123, 0.5f, 1, 0xFFFFFFFF}};
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_LINELIST, 4, lines, sizeof(Tri));
        });
    }
}

const Registrar kBlend(blend_cases);
const Registrar kWrite(write_cases);
const Registrar kTest(test_cases);
const Registrar kStencil(stencil_cases);
const Registrar kRaster(raster_cases);
const Registrar kWrapAndLastPixel(wrap_and_lastpixel_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
