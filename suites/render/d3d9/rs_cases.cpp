#include "render/d3d9/harness.hpp"

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
        Cases::single(rr::CaseSpec("rs", std::string("blend_src_") + s.name).tolerance(rr::Tolerance::blend(3)), [s](Ctx& c) {
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
        Cases::single(rr::CaseSpec("rs", std::string("depthfunc_") + kFuncNames[i]), [i](Ctx& c) {
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
    {"decrsat", D3DSTENCILOP_DECRSAT, 0x01, 2}, {"invert", D3DSTENCILOP_INVERT, 0xFE, 1}, {"incr", D3DSTENCILOP_INCR, 0xFE, 2}, {"decr", D3DSTENCILOP_DECR, 0x01, 2},
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
            c.rsf(D3DRS_POINTSIZE, 3.0f);
            const Tri tris[6] = {{10, 10, 0.5f, 1, 0xFFFF4040}, {70, 20, 0.5f, 1, 0xFF40FF40}, {20, 100, 0.5f, 1, 0xFF4040FF},
                                 {60, 70, 0.5f, 1, 0xFFFFFF40}, {120, 60, 0.5f, 1, 0xFF40FFFF}, {90, 120, 0.5f, 1, 0xFFFF40FF}};
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_TRIANGLELIST, 2, tris, sizeof(Tri));
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
    for (int on = 0; on < 2; ++on) {
        Cases::single(rr::CaseSpec("rs", on ? "antialiased_line_on" : "antialiased_line_off").tolerance(rr::Tolerance::raster(32, 1).with_outliers(0.002)), [on](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_ANTIALIASEDLINEENABLE, on ? TRUE : FALSE);
            const Tri lines[8] = {{10, 12, 0.5f, 1, 0xFFFFFF40}, {118, 40, 0.5f, 1, 0xFFFFFF40}, {10, 30, 0.5f, 1, 0xFF40FFFF}, {118, 100, 0.5f, 1, 0xFF40FFFF},
                                  {20, 120, 0.5f, 1, 0xFFFF40FF}, {100, 10, 0.5f, 1, 0xFFFF40FF}, {64, 5, 0.5f, 1, 0xFFFFFFFF}, {64, 123, 0.5f, 1, 0xFFFFFFFF}};
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_LINELIST, 4, lines, sizeof(Tri));
        });
    }
    for (int on = 0; on < 2; ++on) {
        Cases::single(rr::CaseSpec("rs", on ? "lastpixel_on" : "lastpixel_off").tolerance(rr::Tolerance::exact()), [on](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            c.rs(D3DRS_LASTPIXEL, on ? TRUE : FALSE);
            const Tri lines[8] = {{10.5f, 20.5f, 0.5f, 1, 0xFFFFFFFF}, {50.5f, 20.5f, 0.5f, 1, 0xFFFFFFFF}, {10.5f, 30.5f, 0.5f, 1, 0xFFFF8080}, {50.5f, 30.5f, 0.5f, 1, 0xFFFF8080},
                                  {70.5f, 10.5f, 0.5f, 1, 0xFF80FF80}, {70.5f, 60.5f, 0.5f, 1, 0xFF80FF80}, {90.5f, 60.5f, 0.5f, 1, 0xFF8080FF}, {90.5f, 10.5f, 0.5f, 1, 0xFF8080FF}};
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_LINELIST, 4, lines, sizeof(Tri));
        });
    }
    for (int axis = 0; axis < 2; ++axis) {
        Cases::single(rr::CaseSpec("rs", axis ? "wrap_coord_v" : "wrap_coord_u").tolerance(rr::Tolerance::blend(3)), [axis](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 60, 255));
            Com<IDirect3DTexture9> t = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            c.rs(D3DRS_WRAP0, axis ? D3DWRAP_V : D3DWRAP_U);
            const std::vector<TlVtx> top = Geo::tl_quad(8, 8, 120, 60, 0xFFFFFFFF, axis ? 0.0f : 0.9f, axis ? 0.9f : 0.0f, axis ? 1.0f : 0.1f, axis ? 0.1f : 1.0f);
            c.fvf(kTlFvf);
            c.draw(D3DPT_TRIANGLELIST, 2, top.data(), sizeof(TlVtx));
            c.rs(D3DRS_WRAP0, 0);
            const std::vector<TlVtx> bottom = Geo::tl_quad(8, 68, 120, 120, 0xFFFFFFFF, axis ? 0.0f : 0.9f, axis ? 0.9f : 0.0f, axis ? 1.0f : 0.1f, axis ? 0.1f : 1.0f);
            c.draw(D3DPT_TRIANGLELIST, 2, bottom.data(), sizeof(TlVtx));
        });
    }
    Cases::single(rr::CaseSpec("rs", "wrap_coord_set1_u").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 60, 255));
        Com<IDirect3DTexture9> t = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        c.tss(0, D3DTSS_TEXCOORDINDEX, 1);
        struct V { float x, y, z, rhw, u0, v0, u1, v1; };
        c.fvf(D3DFVF_XYZRHW | D3DFVF_TEX2);
        c.rs(D3DRS_WRAP1, D3DWRAP_U);
        const V a[4] = {{8, 8, 0.5f, 1, 0, 0, 0.9f, 0}, {120, 8, 0.5f, 1, 0, 0, 0.1f, 0}, {8, 60, 0.5f, 1, 0, 0, 0.9f, 1}, {120, 60, 0.5f, 1, 0, 0, 0.1f, 1}};
        c.draw(D3DPT_TRIANGLESTRIP, 2, a, sizeof(V));
        c.rs(D3DRS_WRAP1, 0);
        const V b[4] = {{8, 68, 0.5f, 1, 0, 0, 0.9f, 0}, {120, 68, 0.5f, 1, 0, 0, 0.1f, 0}, {8, 120, 0.5f, 1, 0, 0, 0.9f, 1}, {120, 120, 0.5f, 1, 0, 0, 0.1f, 1}};
        c.draw(D3DPT_TRIANGLESTRIP, 2, b, sizeof(V));
        c.tss(0, D3DTSS_TEXCOORDINDEX, 0);
    });
}

const Registrar kBlend(blend_cases);
const Registrar kWrite(write_cases);
const Registrar kTest(test_cases);
const Registrar kStencil(stencil_cases);
const Registrar kRaster(raster_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
