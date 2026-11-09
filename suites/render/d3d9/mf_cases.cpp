#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

rr::CaseSpec mf(const char* group, const std::string& name, std::vector<unsigned> captured = {1, 3}, unsigned frames = 4) {
    return rr::CaseSpec(group, name).frames(frames, std::move(captured));
}

void background(Ctx& c) {
    c.clear(Dx::kColor(10, 10, 10, 255));
    c.quad(0, 0, 128, 128, 0.9f, 0xE0FF4040, 0x60FFD040, 0xA040C0FF, 0xFF8040FF);
}

void overlay(Ctx& c, unsigned f) {
    const float o = 5.0f * static_cast<float>(f);
    c.quad(10 + o, 10 + o * 0.6f, 74 + o, 74 + o * 0.6f, 0.5f, 0xFF2090E0, 0x70E0E020, 0xB0E02080, 0x30FFFFFF);
}

struct Factor {
    const char* name;
    DWORD value;
};

const Factor kFactors[] = {
    {"zero", D3DBLEND_ZERO}, {"one", D3DBLEND_ONE}, {"srccolor", D3DBLEND_SRCCOLOR}, {"invsrccolor", D3DBLEND_INVSRCCOLOR}, {"srcalpha", D3DBLEND_SRCALPHA},
    {"invsrcalpha", D3DBLEND_INVSRCALPHA}, {"destalpha", D3DBLEND_DESTALPHA}, {"invdestalpha", D3DBLEND_INVDESTALPHA}, {"destcolor", D3DBLEND_DESTCOLOR},
    {"invdestcolor", D3DBLEND_INVDESTCOLOR}, {"srcalphasat", D3DBLEND_SRCALPHASAT}, {"bothsrcalpha", D3DBLEND_BOTHSRCALPHA},
    {"bothinvsrcalpha", D3DBLEND_BOTHINVSRCALPHA}, {"blendfactor", D3DBLEND_BLENDFACTOR}, {"invblendfactor", D3DBLEND_INVBLENDFACTOR},
};

void blend_accumulation() {
    for (const Factor& s : kFactors) {
        Cases::frames(mf("rs", std::string("accumulate_blend_src_") + s.name).tolerance(rr::Tolerance::blend(4)), [s] {
            return FrameBody([s](Ctx& c, unsigned f) {
                if (f == 0) {
                    background(c);
                }
                c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
                c.rs(D3DRS_BLENDFACTOR, 0xFF4080C0);
                c.rs(D3DRS_SRCBLEND, s.value);
                c.rs(D3DRS_DESTBLEND, s.value == D3DBLEND_BOTHSRCALPHA || s.value == D3DBLEND_BOTHINVSRCALPHA ? D3DBLEND_ZERO : D3DBLEND_INVSRCALPHA);
                overlay(c, f);
            });
        }, {}, s.value == D3DBLEND_BLENDFACTOR || s.value == D3DBLEND_INVBLENDFACTOR
                   ? Needs::caps([](const D3DCAPS9& caps) { return (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0; }, "blend factor")
                   : Need());
    }
}

const D3DCMPFUNC kFuncs[8] = {D3DCMP_NEVER, D3DCMP_LESS, D3DCMP_EQUAL, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_NOTEQUAL, D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS};
const char* const kNames[8] = {"never", "less", "equal", "lessequal", "greater", "notequal", "greaterequal", "always"};

void test_accumulation() {
    for (int i = 1; i < 7; ++i) {
        Cases::frames(mf("rs", std::string("progressive_alphatest_") + kNames[i]), [i] {
            return FrameBody([i](Ctx& c, unsigned f) {
                if (f == 0) {
                    c.clear(Dx::kColor(20, 20, 60, 255));
                }
                const unsigned refs[4] = {0x40, 0x7F, 0x80, 0xC0};
                c.rs(D3DRS_ALPHATESTENABLE, TRUE);
                c.rs(D3DRS_ALPHAREF, refs[f]);
                c.rs(D3DRS_ALPHAFUNC, kFuncs[i]);
                const unsigned alphas[7] = {0x00, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF};
                for (int k = 0; k < 7; ++k) {
                    c.quad(4.0f + 31.0f * static_cast<float>(f), 4.0f + 17.0f * static_cast<float>(k), 31.0f + 31.0f * static_cast<float>(f), 17.0f + 17.0f * static_cast<float>(k), 0.5f,
                           (alphas[k] << 24) | 0x00E0A040u | (static_cast<unsigned>(k) * 30u << 8));
                }
            });
        }, {}, Needs::caps([i](const D3DCAPS9& caps) { return (caps.AlphaCmpCaps & (1u << (static_cast<unsigned>(kFuncs[i]) - 1))) != 0; }, "alpha compare function"));
    }
    for (int i = 1; i < 7; ++i) {
        Cases::frames(mf("rs", std::string("persistent_depth_") + kNames[i]), [i] {
            return FrameBody([i](Ctx& c, unsigned f) {
                if (f == 0) {
                    c.clear(Dx::kColor(20, 20, 60, 255));
                    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
                    c.rs(D3DRS_ZFUNC, D3DCMP_ALWAYS);
                    c.quad(0, 0, 128, 128, 0.5f, 0xFF3060C0, 0xFF60C030, 0xFFC03060, 0xFF30C0C0);
                }
                c.rs(D3DRS_ZFUNC, kFuncs[i]);
                const float zs[4] = {0.3f, 0.5f, 0.5f, 0.7f};
                c.quad(4 + 30.0f * static_cast<float>(f), 4, 36 + 30.0f * static_cast<float>(f), 124, zs[f], f % 2 ? 0xFFFFA020 : 0xFF40E060);
            });
        }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128}, Needs::caps([i](const D3DCAPS9& caps) { return (caps.ZCmpCaps & (1u << (static_cast<unsigned>(kFuncs[i]) - 1))) != 0; }, "depth compare function"));
    }
    struct Op {
        const char* name;
        DWORD op;
        DWORD initial;
    };
    const Op ops[] = {{"keep", D3DSTENCILOP_KEEP, 0xFE}, {"zero", D3DSTENCILOP_ZERO, 0xFE}, {"replace", D3DSTENCILOP_REPLACE, 0xFE}, {"incrsat", D3DSTENCILOP_INCRSAT, 0xFD},
                      {"decrsat", D3DSTENCILOP_DECRSAT, 0x02}, {"invert", D3DSTENCILOP_INVERT, 0xFE}, {"incr", D3DSTENCILOP_INCR, 0xFD}, {"decr", D3DSTENCILOP_DECR, 0x02}};
    for (const Op& o : ops) {
        rr::CaseSpec spec = mf("rs", std::string("persistent_stencil_") + o.name);
        if (o.op == D3DSTENCILOP_KEEP) {
            spec.uniform_ok();
        }
        Cases::frames(spec, [o] {
            return FrameBody([o](Ctx& c, unsigned f) {
                if (f == 0) {
                    c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, o.initial);
                } else {
                    c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET);
                }
                c.rs(D3DRS_STENCILENABLE, TRUE);
                c.rs(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
                c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
                c.rs(D3DRS_STENCILREF, 0x55);
                c.rs(D3DRS_STENCILPASS, o.op);
                c.rs(D3DRS_STENCILWRITEMASK, 0xFF);
                c.rs(D3DRS_COLORWRITEENABLE, 0);
                c.quad(8.0f + 12.0f * static_cast<float>(f), 16, 72.0f + 12.0f * static_cast<float>(f), 112, 0.5f, 0xFFFFFFFF);
                c.rs(D3DRS_COLORWRITEENABLE, 15);
                c.rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
                c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
                const DWORD values[10] = {0x00, 0x01, 0x02, 0x03, 0x55, 0xFC, 0xFD, 0xFE, 0xFF, 0xFB};
                const DWORD colors[10] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFF00FFFF, 0xFFFFFFFF, 0xFFFF00FF, 0xFF808080, 0xFFFF8000, 0xFF0080FF};
                for (int k = 0; k < 10; ++k) {
                    c.rs(D3DRS_STENCILREF, values[k]);
                    c.quad(0, 0, 128, 128, 0.5f, colors[k]);
                }
                c.rs(D3DRS_STENCILENABLE, FALSE);
            });
        }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128});
    }
}

Fill alpha_rainbow(unsigned shift) {
    return [shift](UINT x, UINT y, UINT w, UINT h) {
        const std::uint32_t base = Texels::rainbow()((x + shift) % w, y, w, h);
        const unsigned a = 255 - ((x + shift) % w) * 150 / std::max(1u, w - 1);
        return (base & 0x00FFFFFFu) | (a << 24);
    };
}

void format_streaming() {
    struct F {
        const char* name;
        D3DFORMAT format;
        bool linear;
    };
    const F formats[] = {
        {"a8r8g8b8", D3DFMT_A8R8G8B8, true}, {"x8r8g8b8", D3DFMT_X8R8G8B8, true}, {"r5g6b5", D3DFMT_R5G6B5, true}, {"x1r5g5b5", D3DFMT_X1R5G5B5, true},
        {"a1r5g5b5", D3DFMT_A1R5G5B5, true}, {"a4r4g4b4", D3DFMT_A4R4G4B4, true}, {"a8", D3DFMT_A8, true}, {"l8", D3DFMT_L8, true},
        {"a8l8", D3DFMT_A8L8, true}, {"l16", D3DFMT_L16, true}, {"a2r10g10b10", D3DFMT_A2R10G10B10, true}, {"a2b10g10r10", D3DFMT_A2B10G10R10, true},
        {"a16b16g16r16", D3DFMT_A16B16G16R16, true}, {"g16r16", D3DFMT_G16R16, true}, {"r16f", D3DFMT_R16F, true}, {"g16r16f", D3DFMT_G16R16F, true},
        {"a16b16g16r16f", D3DFMT_A16B16G16R16F, true}, {"r32f", D3DFMT_R32F, false}, {"g32r32f", D3DFMT_G32R32F, false}, {"a32b32g32r32f", D3DFMT_A32B32G32R32F, false},
        {"v8u8", D3DFMT_V8U8, true}, {"q8w8v8u8", D3DFMT_Q8W8V8U8, true}, {"a8b8g8r8", D3DFMT_A8B8G8R8, true}, {"x8b8g8r8", D3DFMT_X8B8G8R8, true},
    };
    for (const F& f : formats) {
        Cases::frames(mf("tex", std::string("restream_format_") + f.name, {0, 2}, 3).tolerance(rr::Tolerance::blend(4)), [f] {
            auto texture = std::make_shared<Com<IDirect3DTexture9>>();
            return FrameBody([f, texture](Ctx& c, unsigned k) {
                if (k == 0) {
                    *texture = c.texture(32, 32, 1, 0, f.format, D3DPOOL_MANAGED, alpha_rainbow(0));
                } else if (*texture) {
                    (void)c.fill_level(texture->get(), 0, f.format, alpha_rainbow(8 * k));
                }
                if (!*texture) {
                    return;
                }
                c.clear(Dx::kColor(0, 0, 0, 255));
                c.quad(0, 0, 128, 128, 0.9f, 0xFF204060, 0xFF406020, 0xFF602040, 0xFFC0C040);
                c.tex(0, texture->get());
                c.ss(0, D3DSAMP_MINFILTER, f.linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
                c.ss(0, D3DSAMP_MAGFILTER, f.linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
                c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
                c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                const std::vector<TlVtx> q = Geo::tl_quad(12, 12, 116, 116, 0xFFFFFFFF);
                c.fvf(kTlFvf);
                c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
                c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            });
        }, {}, Needs::format(0, D3DRTYPE_TEXTURE, f.format, std::string("texture format ") + f.name));
    }
}

const Registrar kBlend(blend_accumulation);
const Registrar kTest(test_accumulation);
const Registrar kFormats(format_streaming);

}  // namespace

}  // namespace lsw::suites::render::d3d9
