#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

struct TV {
    float x, y, z;
    DWORD diffuse;
    DWORD specular;
    float u0, v0, u1, v1;
};
constexpr DWORD kTvFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2;

void draw_quad(Ctx& c, float s = 0.9f, DWORD tl = 0xFFE05030, DWORD tr = 0x80F0F040, DWORD bl = 0xC040A0F0, DWORD br = 0x30A0F0A0, float uv = 1.0f) {
    const TV v[4] = {{-s, s, 0.5f, tl, 0xFF804020, 0, 0, 0, 0}, {s, s, 0.5f, tr, 0xFF208040, uv, 0, uv, 0},
                     {-s, -s, 0.5f, bl, 0xFF402080, 0, uv, 0, uv}, {s, -s, 0.5f, br, 0xFFA0A0A0, uv, uv, uv, uv}};
    c.xf(D3DTS_WORLD, Xf());
    c.xf(D3DTS_VIEW, Xf());
    c.xf(D3DTS_PROJECTION, Xf());
    c.fvf(kTvFvf);
    c.draw(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TV));
}

void stage(Ctx& c, DWORD n, DWORD op, DWORD a1, DWORD a2, DWORD aop = D3DTOP_SELECTARG2, DWORD aa1 = D3DTA_TEXTURE, DWORD aa2 = D3DTA_CURRENT) {
    c.tss(n, D3DTSS_COLOROP, op);
    c.tss(n, D3DTSS_COLORARG1, a1);
    c.tss(n, D3DTSS_COLORARG2, a2);
    c.tss(n, D3DTSS_ALPHAOP, aop);
    c.tss(n, D3DTSS_ALPHAARG1, aa1);
    c.tss(n, D3DTSS_ALPHAARG2, aa2);
}

struct Pair {
    Com<IDirect3DTexture9> a;
    Com<IDirect3DTexture9> b;
};

Pair textures(Ctx& c) {
    Pair p;
    p.a = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
        const std::uint32_t g = Texels::rainbow()(x, y, w, h);
        const unsigned a = 255 - y * 200 / std::max(1u, h - 1);
        return ((x / 8 + y / 8) & 1) ? (g & 0x00FFFFFFu) | (a << 24) : (g & 0x00FFFFFFu) | 0xFF000000u;
    });
    p.b = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::alpha_rings());
    for (DWORD s = 0; s < 2; ++s) {
        c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    }
    c.tex(0, p.a.get());
    c.tex(1, p.b.get());
    return p;
}

struct OpInfo {
    const char* name;
    DWORD op;
    DWORD cap;
};

const OpInfo kOps[] = {
    {"selectarg1", D3DTOP_SELECTARG1, D3DTEXOPCAPS_SELECTARG1},
    {"selectarg2", D3DTOP_SELECTARG2, D3DTEXOPCAPS_SELECTARG2},
    {"modulate", D3DTOP_MODULATE, D3DTEXOPCAPS_MODULATE},
    {"modulate2x", D3DTOP_MODULATE2X, D3DTEXOPCAPS_MODULATE2X},
    {"modulate4x", D3DTOP_MODULATE4X, D3DTEXOPCAPS_MODULATE4X},
    {"add", D3DTOP_ADD, D3DTEXOPCAPS_ADD},
    {"addsigned", D3DTOP_ADDSIGNED, D3DTEXOPCAPS_ADDSIGNED},
    {"addsigned2x", D3DTOP_ADDSIGNED2X, D3DTEXOPCAPS_ADDSIGNED2X},
    {"subtract", D3DTOP_SUBTRACT, D3DTEXOPCAPS_SUBTRACT},
    {"addsmooth", D3DTOP_ADDSMOOTH, D3DTEXOPCAPS_ADDSMOOTH},
    {"blenddiffusealpha", D3DTOP_BLENDDIFFUSEALPHA, D3DTEXOPCAPS_BLENDDIFFUSEALPHA},
    {"blendtexturealpha", D3DTOP_BLENDTEXTUREALPHA, D3DTEXOPCAPS_BLENDTEXTUREALPHA},
    {"blendfactoralpha", D3DTOP_BLENDFACTORALPHA, D3DTEXOPCAPS_BLENDFACTORALPHA},
    {"blendtexturealphapm", D3DTOP_BLENDTEXTUREALPHAPM, D3DTEXOPCAPS_BLENDTEXTUREALPHAPM},
    {"blendcurrentalpha", D3DTOP_BLENDCURRENTALPHA, D3DTEXOPCAPS_BLENDCURRENTALPHA},
    {"premodulate", D3DTOP_PREMODULATE, D3DTEXOPCAPS_PREMODULATE},
    {"modulatealpha_addcolor", D3DTOP_MODULATEALPHA_ADDCOLOR, D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR},
    {"modulatecolor_addalpha", D3DTOP_MODULATECOLOR_ADDALPHA, D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA},
    {"modulateinvalpha_addcolor", D3DTOP_MODULATEINVALPHA_ADDCOLOR, D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR},
    {"modulateinvcolor_addalpha", D3DTOP_MODULATEINVCOLOR_ADDALPHA, D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA},
    {"multiplyadd", D3DTOP_MULTIPLYADD, D3DTEXOPCAPS_MULTIPLYADD},
    {"lerp", D3DTOP_LERP, D3DTEXOPCAPS_LERP},
};

Need op_need(DWORD cap, const char* name) {
    return Needs::caps([cap](const D3DCAPS9& caps) { return (caps.TextureOpCaps & cap) != 0 && caps.MaxTextureBlendStages >= 2; }, std::string("texture op ") + name);
}

void op_cases() {
    for (const OpInfo& info : kOps) {
        Cases::single(rr::CaseSpec("tss", std::string("op_") + info.name).tolerance(rr::Tolerance::blend(3)), [info](Ctx& c) {
            c.clear(Dx::kColor(30, 30, 60, 255));
            Pair p = textures(c);
            c.rs(D3DRS_TEXTUREFACTOR, 0x8060B0E0);
            const bool current_alpha = info.op == D3DTOP_BLENDCURRENTALPHA;
            stage(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, current_alpha ? D3DTA_TEXTURE : D3DTA_DIFFUSE, D3DTA_CURRENT);
            if (info.op == D3DTOP_BLENDTEXTUREALPHAPM) {
                stage(c, 1, info.op, D3DTA_CURRENT, D3DTA_TEXTURE);
            } else {
                stage(c, 1, info.op, D3DTA_TEXTURE, D3DTA_CURRENT);
            }
            c.tss(1, D3DTSS_COLORARG0, D3DTA_TFACTOR);
            if (info.op == D3DTOP_PREMODULATE) {
                Com<IDirect3DTexture9> third = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(4, 0xFFFFFFFF, 0xFF4060A0));
                c.tex(2, third.get());
                stage(c, 2, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
                c.tss(2, D3DTSS_TEXCOORDINDEX, 0);
                c.tss(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
                draw_quad(c);
                return;
            }
            c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
            draw_quad(c);
        }, {}, op_need(info.cap, info.name));
    }

    Cases::single(rr::CaseSpec("tss", "op_bumpenvmap").tolerance(rr::Tolerance::filter(6, 0.004)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Com<IDirect3DTexture9> bump = c.texture(32, 32, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            const unsigned u = 128 + static_cast<unsigned>(90.0f * std::sin(fx * 25.0f));
            const unsigned v = 128 + static_cast<unsigned>(90.0f * std::cos(fy * 22.0f));
            return Texels::argb(255, u, v, 0);
        });
        Pair p = textures(c);
        c.tex(0, bump.get());
        c.tex(1, p.a.get());
        c.tss(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
        c.tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        c.tss(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
        c.tss(0, D3DTSS_BUMPENVMAT00, Dx::fbits(0.12f));
        c.tss(0, D3DTSS_BUMPENVMAT01, Dx::fbits(0.0f));
        c.tss(0, D3DTSS_BUMPENVMAT10, Dx::fbits(0.0f));
        c.tss(0, D3DTSS_BUMPENVMAT11, Dx::fbits(0.12f));
        c.tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        c.tss(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        stage(c, 1, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
        c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        c.tss(1, D3DTSS_TEXCOORDINDEX, 0);
        draw_quad(c, 0.9f, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 1.0f);
    }, {}, Needs::all({op_need(D3DTEXOPCAPS_BUMPENVMAP, "bumpenvmap"), Needs::format(D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DRTYPE_TEXTURE, D3DFMT_V8U8, "V8U8 bump textures")}));

    Cases::single(rr::CaseSpec("tss", "op_bumpenvmapluminance").tolerance(rr::Tolerance::filter(6, 0.004)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Com<IDirect3DTexture9> bump = c.texture(32, 32, 1, 0, D3DFMT_X8L8V8U8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            const unsigned u = 128 + static_cast<unsigned>(80.0f * std::sin(fx * 20.0f));
            const unsigned v = 128 + static_cast<unsigned>(80.0f * std::cos(fy * 18.0f));
            return Texels::argb(255, u, v, 128 + static_cast<unsigned>(100.0f * fx));
        });
        Pair p = textures(c);
        c.tex(0, bump.get());
        c.tex(1, p.a.get());
        c.tss(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAPLUMINANCE);
        c.tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        c.tss(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
        c.tss(0, D3DTSS_BUMPENVMAT00, Dx::fbits(0.1f));
        c.tss(0, D3DTSS_BUMPENVMAT01, Dx::fbits(0.0f));
        c.tss(0, D3DTSS_BUMPENVMAT10, Dx::fbits(0.0f));
        c.tss(0, D3DTSS_BUMPENVMAT11, Dx::fbits(0.1f));
        c.tss(0, D3DTSS_BUMPENVLSCALE, Dx::fbits(1.0f));
        c.tss(0, D3DTSS_BUMPENVLOFFSET, Dx::fbits(0.0f));
        c.tss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        c.tss(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        stage(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
        c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        draw_quad(c, 0.9f, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 1.0f);
    }, {}, Needs::all({op_need(D3DTEXOPCAPS_BUMPENVMAPLUMINANCE, "bumpenvmapluminance"), Needs::format(D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DRTYPE_TEXTURE, D3DFMT_X8L8V8U8, "X8L8V8U8 bump textures")}));

    Cases::single(rr::CaseSpec("tss", "op_dotproduct3").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Com<IDirect3DTexture9> normals = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            float nx = 0.6f * std::sin(fx * 18.0f);
            float ny = 0.6f * std::cos(fy * 18.0f);
            float nz = std::sqrt(std::max(0.05f, 1.0f - nx * nx - ny * ny));
            const unsigned r = static_cast<unsigned>((nx * 0.5f + 0.5f) * 255.0f);
            const unsigned g = static_cast<unsigned>((ny * 0.5f + 0.5f) * 255.0f);
            const unsigned b = static_cast<unsigned>((nz * 0.5f + 0.5f) * 255.0f);
            return Texels::argb(255, r, g, b);
        });
        Pair p = textures(c);
        c.tex(0, normals.get());
        c.tex(1, p.a.get());
        c.rs(D3DRS_TEXTUREFACTOR, Dx::kColor(200, 230, 170));
        stage(c, 0, D3DTOP_DOTPRODUCT3, D3DTA_TEXTURE, D3DTA_TFACTOR, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
        stage(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
        c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        draw_quad(c, 0.9f, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 1.0f);
    }, {}, op_need(D3DTEXOPCAPS_DOTPRODUCT3, "dotproduct3"));
}

void alpha_cases() {
    struct AlphaOp {
        const char* name;
        DWORD op;
    };
    const AlphaOp ops[] = {{"selectarg1", D3DTOP_SELECTARG1}, {"modulate", D3DTOP_MODULATE}, {"add", D3DTOP_ADD}, {"subtract", D3DTOP_SUBTRACT}, {"blendfactoralpha", D3DTOP_BLENDFACTORALPHA}, {"blendtexturealpha", D3DTOP_BLENDTEXTUREALPHA}};
    for (const AlphaOp& a : ops) {
        Cases::single(rr::CaseSpec("tss", std::string("alphaop_") + a.name).tolerance(rr::Tolerance::blend(3)), [a](Ctx& c) {
            c.clear(Dx::kColor(40, 90, 40, 255));
            Pair p = textures(c);
            c.rs(D3DRS_TEXTUREFACTOR, 0x60FFFFFF);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            stage(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
            stage(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, a.op, D3DTA_TEXTURE, D3DTA_CURRENT);
            c.tss(1, D3DTSS_ALPHAARG0, D3DTA_TFACTOR);
            if (a.op == D3DTOP_BLENDFACTORALPHA) {
                c.tss(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
            }
            c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
            draw_quad(c);
        }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxTextureBlendStages >= 2; }, "two blend stages"));
    }
}

void arg_cases() {
    struct Arg {
        const char* name;
        DWORD arg1;
        DWORD arg2;
    };
    const Arg args[] = {
        {"complement", D3DTA_TEXTURE | D3DTA_COMPLEMENT, D3DTA_DIFFUSE},
        {"alphareplicate", D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE, D3DTA_DIFFUSE},
        {"tfactor", D3DTA_TFACTOR, D3DTA_DIFFUSE | D3DTA_COMPLEMENT},
        {"specular", D3DTA_SPECULAR, D3DTA_TEXTURE},
        {"diffuse_alphareplicate", D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE, D3DTA_TEXTURE},
        {"tfactor_alphareplicate_complement", D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE | D3DTA_COMPLEMENT, D3DTA_TEXTURE},
    };
    for (const Arg& a : args) {
        Cases::single(rr::CaseSpec("tss", std::string("arg_") + a.name).tolerance(rr::Tolerance::blend(3)), [a](Ctx& c) {
            c.clear(Dx::kColor(30, 30, 60));
            Pair p = textures(c);
            c.rs(D3DRS_TEXTUREFACTOR, 0x80D0A050);
            c.rs(D3DRS_SPECULARENABLE, TRUE);
            stage(c, 0, D3DTOP_MODULATE, a.arg1, a.arg2, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
            c.tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            draw_quad(c);
        });
    }

    Cases::single(rr::CaseSpec("tss", "arg_temp_resultarg").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Pair p = textures(c);
        stage(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
        c.tss(0, D3DTSS_RESULTARG, D3DTA_TEMP);
        stage(c, 1, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
        c.tss(1, D3DTSS_RESULTARG, D3DTA_CURRENT);
        stage(c, 2, D3DTOP_MODULATE, D3DTA_TEMP, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEMP, D3DTA_CURRENT);
        c.tex(2, nullptr);
        c.tss(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
        draw_quad(c);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.PrimitiveMiscCaps & D3DPMISCCAPS_TSSARGTEMP) != 0 && caps.MaxTextureBlendStages >= 3; }, "TSS temp register"));

    Cases::single(rr::CaseSpec("tss", "arg_constant").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Pair p = textures(c);
        stage(c, 0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CONSTANT, D3DTOP_SELECTARG1, D3DTA_CONSTANT, D3DTA_CURRENT);
        c.tss(0, D3DTSS_CONSTANT, Dx::kColor(255, 180, 90, 200));
        c.tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        draw_quad(c);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.PrimitiveMiscCaps & D3DPMISCCAPS_PERSTAGECONSTANT) != 0; }, "per stage constant"));

    Cases::single(rr::CaseSpec("tss", "stages_chained_eight").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Com<IDirect3DTexture9> t[8];
        for (DWORD i = 0; i < 8; ++i) {
            const std::uint32_t lit = Texels::argb(255, 6 + 2 * ((i + 0) % 3) * (i + 1), 5 + 3 * ((i + 1) % 3) * (i + 1) / 2, 4 + 2 * ((i + 2) % 3) * (i + 1));
            t[i] = c.texture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(1 + (i * 3) % 7, lit, Texels::argb(255, 2, 2, 2)));
            c.tex(i, t[i].get());
            c.ss(i, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            c.ss(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            if (i == 0) {
                stage(c, i, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
            } else {
                stage(c, i, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
            }
            c.tss(i, D3DTSS_TEXCOORDINDEX, 0);
        }
        draw_quad(c, 0.9f, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 1.0f);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxTextureBlendStages >= 8 && caps.MaxSimultaneousTextures >= 8; }, "eight texture stages"));

    Cases::single(rr::CaseSpec("tss", "texcoordindex_swap_sets").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(30, 30, 60));
        Pair p = textures(c);
        c.tss(0, D3DTSS_TEXCOORDINDEX, 1);
        c.tss(1, D3DTSS_TEXCOORDINDEX, 0);
        stage(c, 0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_DIFFUSE, D3DTA_CURRENT);
        stage(c, 1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_CURRENT);
        c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        const TV v[4] = {{-0.9f, 0.9f, 0.5f, 0xFFFFFFFF, 0, 0, 0, 0, 0}, {0.9f, 0.9f, 0.5f, 0xFFFFFFFF, 0, 3, 0, 1, 0},
                         {-0.9f, -0.9f, 0.5f, 0xFFFFFFFF, 0, 0, 3, 0, 1}, {0.9f, -0.9f, 0.5f, 0xFFFFFFFF, 0, 3, 3, 1, 1}};
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(kTvFvf);
        c.draw(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TV));
    });
}

const Registrar kOpsReg(op_cases);
const Registrar kAlphaReg(alpha_cases);
const Registrar kArgReg(arg_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
