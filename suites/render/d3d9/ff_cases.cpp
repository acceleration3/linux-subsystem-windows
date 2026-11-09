#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

constexpr DWORD kWhite = 0xFFFFFFFFu;

const Mesh& sphere() {
    static const Mesh mesh = Geo::sphere(28, 18, 1.2f, kWhite);
    return mesh;
}

Com<IDirect3DTexture9> checker(Ctx& c) {
    Com<IDirect3DTexture9> t = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(8, Texels::argb(255, 240, 230, 60), Texels::argb(255, 40, 70, 200)));
    c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    return t;
}

Com<IDirect3DTexture9> rainbow(Ctx& c) {
    Com<IDirect3DTexture9> t = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
    c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    return t;
}

void lit(Ctx& c, const D3DMATERIAL9& material, DWORD ambient = 0) {
    c.camera(4.0f);
    c.rs(D3DRS_LIGHTING, TRUE);
    c.rs(D3DRS_AMBIENT, ambient);
    c.material(material);
    c.fvf(kVtxFvf);
}

D3DMATERIAL9 grey(float power = 0, float spec = 0) { return Dx::material(0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0, 0, 0, spec, spec, spec, power); }

void clear_blue(Ctx& c) { c.clear(Dx::kColor(20, 24, 48)); }

struct Pd {
    float x, y, z;
    DWORD color;
};

void fvf_cases() {
    Cases::single(rr::CaseSpec("ff", "fvf_xyz_diffuse"), [](Ctx& c) {
        clear_blue(c);
        c.camera(5.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        Mesh cube = Geo::cube(1.0f, kWhite);
        std::vector<Pd> verts;
        const DWORD palette[8] = {0xFFFF2020, 0xFF20FF20, 0xFF2020FF, 0xFFFFFF20, 0xFFFF20FF, 0xFF20FFFF, 0xFFFFFFFF, 0xFF808080};
        for (std::size_t i = 0; i < cube.vertices.size(); ++i) {
            verts.push_back({cube.vertices[i].x, cube.vertices[i].y, cube.vertices[i].z, palette[i % 8]});
        }
        c.xf(D3DTS_WORLD, Xf::rot_y(0.7f) * Xf::rot_x(0.5f));
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        c.draw_idx(D3DPT_TRIANGLELIST, static_cast<UINT>(verts.size()), cube.triangles(), cube.indices.data(), D3DFMT_INDEX16, verts.data(), sizeof(Pd));
    }, {D3DFMT_A8R8G8B8, D3DFMT_D24S8, 128});

    Cases::single(rr::CaseSpec("ff", "fvf_xyzrhw_diffuse"), [](Ctx& c) {
        clear_blue(c);
        c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        struct V { float x, y, z, rhw; DWORD color; };
        const V tri[3] = {{20, 100, 0.5f, 1, 0xFFFF0000}, {64, 12, 0.5f, 1, 0xFF00FF00}, {110, 100, 0.5f, 1, 0xFF0000FF}};
        c.draw(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
        const V strip[4] = {{10, 110, 0.5f, 1, 0x80FFFFFF}, {10, 124, 0.5f, 1, 0x80FF8000}, {118, 110, 0.5f, 1, 0x8000FF80}, {118, 124, 0.5f, 1, 0x800080FF}};
        c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
        c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        c.draw(D3DPT_TRIANGLESTRIP, 2, strip, sizeof(V));
    });

    Cases::single(rr::CaseSpec("ff", "fvf_xyzrhw_tex"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t = checker(c);
        c.tex(0, t.get());
        c.fvf(kTlFvf);
        const std::vector<TlVtx> q = Geo::tl_quad(16, 16, 112, 112, 0xFFFFFFFF, 0, 0, 2, 2);
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
    }, {}, nullptr);

    Cases::single(rr::CaseSpec("ff", "fvf_xyzw"), [](Ctx& c) {
        clear_blue(c);
        struct V { float x, y, z, w; DWORD color; };
        const V tri[3] = {{-1.0f, -1.0f, 1.0f, 2.0f, 0xFFFF4040}, {0.0f, 1.2f, 1.0f, 2.0f, 0xFF40FF40}, {1.0f, -1.0f, 1.0f, 2.0f, 0xFF4040FF}};
        Com<IDirect3DVertexShader9> vs = c.vs("struct I { float4 p : POSITION; float4 c : COLOR0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\nO main(I i) { O o; o.p = i.p; o.c = i.c; return o; }\n", "vs_2_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.fvf(D3DFVF_XYZW | D3DFVF_DIFFUSE);
        c.draw(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
        c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
        c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
    }, {}, Needs::shader(2, 2));

    for (int weights = 1; weights <= 3; ++weights) {
        Cases::single(rr::CaseSpec("ff", "fvf_xyzb" + std::to_string(weights)), [weights](Ctx& c) {
            clear_blue(c);
            c.camera(4.5f);
            c.rs(D3DRS_VERTEXBLEND, weights == 1 ? D3DVBF_1WEIGHTS : weights == 2 ? D3DVBF_2WEIGHTS : D3DVBF_3WEIGHTS);
            c.xf(D3DTS_WORLD, Xf::rot_z(0.5f));
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(1)), Xf::rot_z(-0.5f));
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(2)), Xf::translate(0.6f, 0.0f, 0.0f));
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(3)), Xf::translate(-0.6f, 0.0f, 0.0f) * Xf::scale(0.8f, 1.0f, 1.0f));
            std::vector<float> data;
            const unsigned rows = 14;
            const DWORD fvf = (weights == 1 ? D3DFVF_XYZB1 : weights == 2 ? D3DFVF_XYZB2 : D3DFVF_XYZB3) | D3DFVF_DIFFUSE;
            for (unsigned k = 0; k < rows; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(rows - 1);
                for (int side = -1; side <= 1; side += 2) {
                    data.push_back(0.4f * static_cast<float>(side));
                    data.push_back(-1.3f + 2.6f * t);
                    data.push_back(0.0f);
                    if (weights == 1) {
                        data.push_back(t);
                    } else if (weights == 2) {
                        data.push_back(0.5f * (1 - t));
                        data.push_back(0.5f * t);
                    } else {
                        data.push_back(0.5f * (1 - t));
                        data.push_back(0.5f * t);
                        data.push_back(0.25f);
                    }
                    const DWORD color = Dx::kColor(static_cast<unsigned>(255 * t), 200, static_cast<unsigned>(255 * (1 - t)));
                    float f = 0;
                    std::memcpy(&f, &color, 4);
                    data.push_back(f);
                }
            }
            c.fvf(fvf);
            c.draw(D3DPT_TRIANGLESTRIP, rows * 2 - 2, data.data(), static_cast<UINT>((3 + weights + 1) * 4));
        }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexBlendMatrices >= 4; }, "four blend matrices"));
    }

    Cases::single(rr::CaseSpec("ff", "fvf_blend_indexed"), [](Ctx& c) {
        clear_blue(c);
        c.camera(4.5f);
        c.rs(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
        c.rs(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
        c.xf(D3DTS_WORLD, Xf::translate(2.5f, 0.0f, 0.0f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(1)), Xf::translate(-2.5f, 0.0f, 0.0f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(2)), Xf::rot_z(0.7f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(3)), Xf::rot_z(-0.7f) * Xf::translate(0.4f, 0.0f, 0.0f));
        struct V { float x, y, z, w; DWORD idx; DWORD color; };
        std::vector<V> v;
        for (unsigned k = 0; k < 12; ++k) {
            const float t = static_cast<float>(k) / 11.0f;
            for (int side = -1; side <= 1; side += 2) {
                v.push_back({0.4f * static_cast<float>(side), -1.3f + 2.6f * t, 0.0f, t, 0x00000302u, Dx::kColor(255, static_cast<unsigned>(255 * t), 60)});
            }
        }
        c.fvf(D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE);
        c.draw(D3DPT_TRIANGLESTRIP, static_cast<UINT>(v.size()) - 2, v.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexBlendMatrixIndex >= 3 && (caps.DeclTypes & D3DDTCAPS_UBYTE4) != 0; }, "indexed vertex blending"));

    Cases::single(rr::CaseSpec("ff", "fvf_xyzb4_three_weights_indexed"), [](Ctx& c) {
        clear_blue(c);
        c.camera(4.5f);
        c.rs(D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS);
        c.rs(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
        c.xf(D3DTS_WORLD, Xf::translate(0.0f, 3.0f, 0.0f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(1)), Xf::translate(0.0f, -3.0f, 0.0f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(2)), Xf::rot_z(0.6f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(3)), Xf::rot_z(-0.6f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(4)), Xf::translate(0.5f, 0.0f, 0.0f));
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(5)), Xf::translate(-0.5f, 0.0f, 0.0f) * Xf::scale(0.8f, 1.0f, 1.0f));
        struct V { float x, y, z, w0, w1, w2; DWORD idx; DWORD color; };
        std::vector<V> v;
        for (unsigned k = 0; k < 14; ++k) {
            const float t = static_cast<float>(k) / 13.0f;
            for (int side = -1; side <= 1; side += 2) {
                v.push_back({0.4f * static_cast<float>(side), -1.3f + 2.6f * t, 0.0f, 0.5f * (1 - t), 0.5f * t, 0.25f, 0x05040302u, Dx::kColor(static_cast<unsigned>(255 * t), 200, static_cast<unsigned>(255 * (1 - t)))});
            }
        }
        c.fvf(D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE);
        c.draw(D3DPT_TRIANGLESTRIP, static_cast<UINT>(v.size()) - 2, v.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexBlendMatrixIndex >= 5 && (caps.DeclTypes & D3DDTCAPS_UBYTE4) != 0; }, "indexed vertex blending with four weights"));

    Cases::single(rr::CaseSpec("ff", "fvf_normal_lit"), [](Ctx& c) {
        clear_blue(c);
        lit(c, grey(), Dx::kColor(30, 30, 30));
        c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 0.9f));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.draw_mesh(sphere());
    });

    Cases::single(rr::CaseSpec("ff", "fvf_psize"), [](Ctx& c) {
        clear_blue(c);
        c.camera(5.0f);
        struct V { float x, y, z, size; DWORD color; };
        std::vector<V> v;
        for (int i = 0; i < 6; ++i) {
            v.push_back({-1.2f + 0.48f * static_cast<float>(i), 0.9f - 0.35f * static_cast<float>(i), 0.0f, 4.0f + 6.0f * static_cast<float>(i), Dx::kColor(255, 40 * i, 255 - 40 * i)});
        }
        c.rs(D3DRS_POINTSPRITEENABLE, TRUE);
        c.rs(D3DRS_POINTSCALEENABLE, FALSE);
        c.rsf(D3DRS_POINTSIZE_MAX, 64.0f);
        c.fvf(D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE);
        c.draw(D3DPT_POINTLIST, 6, v.data(), sizeof(V));
    });

    Cases::single(rr::CaseSpec("ff", "fvf_specular").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t = checker(c);
        c.tex(0, t.get());
        c.camera(4.0f);
        Mesh m = Geo::sphere(20, 14, 1.2f, kWhite);
        for (Vtx& v : m.vertices) {
            const unsigned s = static_cast<unsigned>(std::max(0.0f, v.nx * 0.6f + v.ny * 0.5f - v.nz * 0.4f) * 255.0f);
            v.specular = Dx::kColor(s, s, s);
            v.diffuse = 0xFFB0B0B0;
        }
        c.rs(D3DRS_SPECULARENABLE, TRUE);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.fvf(kVtxFvf);
        c.draw_mesh(m);
    });

    struct SizeCase { const char* name; DWORD fvf; int floats; };
    const SizeCase sizes[] = {{"fvf_tex1_size1", D3DFVF_TEXCOORDSIZE1(0), 1}, {"fvf_tex1_size3", D3DFVF_TEXCOORDSIZE3(0), 3}, {"fvf_tex1_size4", D3DFVF_TEXCOORDSIZE4(0), 4}};
    for (const SizeCase& sc : sizes) {
        Cases::single(rr::CaseSpec("ff", sc.name), [sc](Ctx& c) {
            clear_blue(c);
            Com<IDirect3DTexture9> t = rainbow(c);
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            const float pos[4][2] = {{-0.8f, 0.8f}, {0.8f, 0.8f}, {-0.8f, -0.8f}, {0.8f, -0.8f}};
            const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
            std::vector<float> data;
            for (int i = 0; i < 4; ++i) {
                data.push_back(pos[i][0]);
                data.push_back(pos[i][1]);
                data.push_back(0.5f);
                data.push_back(uv[i][0]);
                if (sc.floats >= 2) {
                    data.push_back(uv[i][1]);
                }
                for (int k = 2; k < sc.floats; ++k) {
                    data.push_back(k == 3 ? 1.0f : 0.25f);
                }
            }
            c.xf(D3DTS_WORLD, Xf());
            c.xf(D3DTS_VIEW, Xf());
            c.xf(D3DTS_PROJECTION, Xf());
            if (sc.floats >= 3) {
                c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, (sc.floats == 3 ? D3DTTFF_COUNT3 : D3DTTFF_COUNT4) | D3DTTFF_PROJECTED);
                for (int i = 0; i < 4; ++i) {
                    float* q = &data[static_cast<std::size_t>(i * (3 + sc.floats))];
                    q[3] *= 2.0f;
                    q[4] *= 2.0f;
                    q[sc.floats == 3 ? 5 : 6] = 2.0f;
                }
            }
            c.fvf(D3DFVF_XYZ | D3DFVF_TEX1 | sc.fvf);
            c.draw(D3DPT_TRIANGLESTRIP, 2, data.data(), static_cast<UINT>((3 + sc.floats) * 4));
        });
    }

    Cases::single(rr::CaseSpec("ff", "fvf_tex2_lightmap"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> base = checker(c);
        Com<IDirect3DTexture9> light = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
            const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 0.5f;
            const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) - 0.5f;
            const unsigned v = static_cast<unsigned>(std::clamp(1.0f - 2.0f * std::sqrt(dx * dx + dy * dy), 0.1f, 1.0f) * 255.0f);
            return Texels::argb(255, v, v, v);
        });
        c.tex(0, base.get());
        c.tex(1, light.get());
        c.ss(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        c.ss(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        c.tss(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
        c.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        c.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
        struct V { float x, y, z, u0, v0, u1, v1; };
        const float s = 0.85f;
        const V q[4] = {{-s, s, 0.5f, 0, 0, 0, 0}, {s, s, 0.5f, 3, 0, 1, 0}, {-s, -s, 0.5f, 0, 3, 0, 1}, {s, -s, 0.5f, 3, 3, 1, 1}};
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEX2);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxTextureBlendStages >= 2 && caps.MaxSimultaneousTextures >= 2; }, "two texture stages"));

    Cases::single(rr::CaseSpec("ff", "fvf_tex4_add"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t[4];
        const std::uint32_t col[4] = {Texels::argb(255, 90, 0, 0), Texels::argb(255, 0, 90, 0), Texels::argb(255, 0, 0, 90), Texels::argb(255, 60, 60, 0)};
        for (int i = 0; i < 4; ++i) {
            t[i] = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(2 + 2 * static_cast<unsigned>(i), col[i], Texels::argb(255, 0, 0, 0)));
            c.tex(static_cast<DWORD>(i), t[i].get());
            if (i > 0) {
                c.tss(static_cast<DWORD>(i), D3DTSS_COLOROP, D3DTOP_ADD);
                c.tss(static_cast<DWORD>(i), D3DTSS_COLORARG1, D3DTA_TEXTURE);
                c.tss(static_cast<DWORD>(i), D3DTSS_COLORARG2, D3DTA_CURRENT);
            }
        }
        struct V { float x, y, z, uv[8]; };
        std::vector<V> q;
        const float pos[4][2] = {{-0.85f, 0.85f}, {0.85f, 0.85f}, {-0.85f, -0.85f}, {0.85f, -0.85f}};
        const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        for (int i = 0; i < 4; ++i) {
            V v{pos[i][0], pos[i][1], 0.5f, {}};
            for (int s = 0; s < 4; ++s) {
                v.uv[s * 2] = uv[i][0];
                v.uv[s * 2 + 1] = uv[i][1];
            }
            q.push_back(v);
        }
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEX4);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxSimultaneousTextures >= 4; }, "four simultaneous textures"));

    Cases::single(rr::CaseSpec("ff", "fvf_tex8_index"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t = rainbow(c);
        c.tex(0, t.get());
        c.tss(0, D3DTSS_TEXCOORDINDEX, 7);
        struct V { float x, y, z, uv[16]; };
        std::vector<V> q;
        const float pos[4][2] = {{-0.85f, 0.85f}, {0.85f, 0.85f}, {-0.85f, -0.85f}, {0.85f, -0.85f}};
        const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        for (int i = 0; i < 4; ++i) {
            V v{pos[i][0], pos[i][1], 0.5f, {}};
            for (int s = 0; s < 8; ++s) {
                v.uv[s * 2] = s == 7 ? uv[i][0] : 0.0f;
                v.uv[s * 2 + 1] = s == 7 ? uv[i][1] : 0.0f;
            }
            q.push_back(v);
        }
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEX8);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxSimultaneousTextures >= 1; }, "textures"));
}

void sphere_scene(Ctx& c, const D3DMATERIAL9& material, DWORD ambient, const std::function<void(Ctx&)>& lights) {
    clear_blue(c);
    lit(c, material, ambient);
    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
    c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
    lights(c);
    c.draw_mesh(sphere());
}

void light_cases() {
    const rr::Tolerance lit_tol = rr::Tolerance::blend(3);
    Cases::single(rr::CaseSpec("ff", "light_directional_two").tolerance(lit_tol), [](Ctx& c) {
        sphere_scene(c, grey(), Dx::kColor(10, 10, 14), [](Ctx& k) {
            k.light(0, Dx::directional(0.4f, -0.3f, 1.0f, 1.0f, 0.5f, 0.4f));
            k.light(1, Dx::directional(-0.8f, 0.5f, 0.6f, 0.2f, 0.3f, 0.9f));
        });
    });
    for (int att = 0; att < 3; ++att) {
        Cases::single(rr::CaseSpec("ff", "light_point_att" + std::to_string(att)).tolerance(lit_tol), [att](Ctx& c) {
            sphere_scene(c, grey(), 0, [att](Ctx& k) {
                const float a0 = att == 0 ? 1.0f : 0.0f;
                const float a1 = att == 1 ? 0.5f : 0.0f;
                const float a2 = att == 2 ? 0.12f : 0.0f;
                k.light(0, Dx::point(1.5f, 1.2f, -2.5f, 50.0f, a0 + (att > 0 ? 0.2f : 0.0f), a1, a2, 1.0f, 0.95f, 0.8f));
            });
        });
    }
    Cases::single(rr::CaseSpec("ff", "light_point_range_cutoff").tolerance(lit_tol), [](Ctx& c) {
        sphere_scene(c, grey(), Dx::kColor(12, 12, 12), [](Ctx& k) { k.light(0, Dx::point(1.0f, 0.5f, -2.2f, 2.6f, 1.0f, 0.0f, 0.0f, 1, 1, 1)); });
    });
    const float falloffs[3] = {0.0f, 1.0f, 4.0f};
    for (int i = 0; i < 3; ++i) {
        Cases::single(rr::CaseSpec("ff", "light_spot_falloff" + std::to_string(i)).tolerance(rr::Tolerance::blend(4)), [i, falloffs](Ctx& c) {
            sphere_scene(c, grey(), Dx::kColor(8, 8, 8), [i, falloffs](Ctx& k) {
                k.light(0, Dx::spot(0.8f, 0.8f, -3.0f, -0.2f, -0.2f, 1.0f, 30.0f, falloffs[i], 0.3f, 0.9f, 1.0f, 1.0f, 0.8f));
            });
        });
    }
    Cases::single(rr::CaseSpec("ff", "light_spot_narrow").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        sphere_scene(c, grey(), Dx::kColor(8, 8, 8), [](Ctx& k) {
            k.light(0, Dx::spot(0.0f, 0.0f, -3.0f, 0.0f, 0.0f, 1.0f, 30.0f, 1.0f, 0.15f, 0.35f, 1.0f, 0.7f, 0.7f));
        });
    });
    Cases::single(rr::CaseSpec("ff", "light_eight_lights").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        sphere_scene(c, grey(), 0, [](Ctx& k) {
            const float colors[8][3] = {{1, 0.2f, 0.2f}, {0.2f, 1, 0.2f}, {0.2f, 0.2f, 1}, {1, 1, 0.2f}, {1, 0.2f, 1}, {0.2f, 1, 1}, {0.6f, 0.6f, 0.6f}, {1, 0.6f, 0.2f}};
            for (int i = 0; i < 8; ++i) {
                const float a = 0.785398f * static_cast<float>(i);
                if (i % 2 == 0) {
                    k.light(static_cast<DWORD>(i), Dx::directional(-std::cos(a), -std::sin(a), 1.0f, colors[i][0] * 0.35f, colors[i][1] * 0.35f, colors[i][2] * 0.35f));
                } else {
                    k.light(static_cast<DWORD>(i), Dx::point(2.5f * std::cos(a), 2.5f * std::sin(a), -2.5f, 40.0f, 1.0f, 0.0f, 0.05f, colors[i][0] * 0.5f, colors[i][1] * 0.5f, colors[i][2] * 0.5f));
                }
            }
        }
        );
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxActiveLights >= 8 || caps.MaxActiveLights == 0xFFFFFFFFu; }, "eight active lights"));
    Cases::single(rr::CaseSpec("ff", "light_ambient_only"), [](Ctx& c) {
        sphere_scene(c, Dx::material(0, 0, 0, 0.9f, 0.5f, 0.3f), Dx::kColor(200, 200, 200), [](Ctx&) {});
    });
    Cases::single(rr::CaseSpec("ff", "light_emissive"), [](Ctx& c) {
        sphere_scene(c, Dx::material(0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.1f, 0.5f, 0.2f), Dx::kColor(30, 30, 30), [](Ctx& k) {
            k.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 0.5f, 0.5f, 0.5f));
        });
    });

    struct Source { const char* name; D3DRENDERSTATETYPE state; };
    const Source sources[] = {{"diffuse", D3DRS_DIFFUSEMATERIALSOURCE}, {"ambient", D3DRS_AMBIENTMATERIALSOURCE}, {"emissive", D3DRS_EMISSIVEMATERIALSOURCE}, {"specular", D3DRS_SPECULARMATERIALSOURCE}};
    for (const Source& src : sources) {
        Cases::single(rr::CaseSpec("ff", std::string("light_source_") + src.name + (src.state == D3DRS_SPECULARMATERIALSOURCE ? "_color2" : "_color1")).tolerance(lit_tol), [src](Ctx& c) {
            clear_blue(c);
            lit(c, Dx::material(0.3f, 0.3f, 0.3f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0f, 0.0f, 0.6f, 0.6f, 0.6f, 12.0f), Dx::kColor(90, 90, 90));
            Mesh m = Geo::sphere(24, 16, 1.2f, kWhite);
            for (Vtx& v : m.vertices) {
                v.diffuse = Dx::kColor(static_cast<unsigned>((v.nx * 0.5f + 0.5f) * 255), static_cast<unsigned>((v.ny * 0.5f + 0.5f) * 255), 160);
                v.specular = Dx::kColor(static_cast<unsigned>((v.ny * 0.5f + 0.5f) * 255), 100, static_cast<unsigned>((v.nx * 0.5f + 0.5f) * 255));
            }
            c.rs(D3DRS_COLORVERTEX, TRUE);
            c.rs(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
            c.rs(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
            c.rs(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
            c.rs(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
            c.rs(src.state, D3DMCS_COLOR1);
            if (src.state == D3DRS_SPECULARMATERIALSOURCE) {
                c.rs(src.state, D3DMCS_COLOR2);
                c.rs(D3DRS_SPECULARENABLE, TRUE);
            }
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
            c.draw_mesh(m);
        });
    }
    Cases::single(rr::CaseSpec("ff", "light_colorvertex_off").tolerance(lit_tol), [](Ctx& c) {
        clear_blue(c);
        lit(c, grey(), Dx::kColor(20, 20, 20));
        Mesh m = Geo::sphere(24, 16, 1.2f, kWhite);
        for (Vtx& v : m.vertices) {
            v.diffuse = Dx::kColor(static_cast<unsigned>((v.nx * 0.5f + 0.5f) * 255), 40, static_cast<unsigned>((v.ny * 0.5f + 0.5f) * 255));
        }
        c.rs(D3DRS_COLORVERTEX, FALSE);
        c.rs(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
        c.draw_mesh(m);
    });
    for (int local = 0; local < 2; ++local) {
        Cases::single(rr::CaseSpec("ff", local ? "light_specular_localviewer" : "light_specular_infinite").tolerance(rr::Tolerance::blend(6)), [local](Ctx& c) {
            sphere_scene(c, Dx::material(0.25f, 0.25f, 0.3f, 0.25f, 0.25f, 0.3f, 0, 0, 0, 1, 1, 1, 24.0f), Dx::kColor(15, 15, 15), [local](Ctx& k) {
                k.camera(2.4f);
                k.rs(D3DRS_SPECULARENABLE, TRUE);
                k.rs(D3DRS_LOCALVIEWER, local ? TRUE : FALSE);
                k.light(0, Dx::point(1.6f, 1.6f, -1.8f, 40.0f, 1.0f, 0.0f, 0.0f, 1, 1, 1));
            });
        });
    }
    Cases::single(rr::CaseSpec("ff", "light_specular_power_sharp").tolerance(rr::Tolerance::blend(8)), [](Ctx& c) {
        sphere_scene(c, Dx::material(0.2f, 0.2f, 0.25f, 0.2f, 0.2f, 0.25f, 0, 0, 0, 1, 1, 1, 90.0f), Dx::kColor(10, 10, 10), [](Ctx& k) {
            k.rs(D3DRS_SPECULARENABLE, TRUE);
            k.light(0, Dx::directional(-0.3f, -0.5f, 1.0f, 1, 1, 1));
        });
    });
    Cases::single(rr::CaseSpec("ff", "light_specular_disabled_no_highlight").tolerance(rr::Tolerance::blend(8)), [](Ctx& c) {
        sphere_scene(c, Dx::material(0.2f, 0.2f, 0.25f, 0.2f, 0.2f, 0.25f, 0, 0, 0, 1, 1, 1, 90.0f), Dx::kColor(10, 10, 10), [](Ctx& k) {
            k.rs(D3DRS_SPECULARENABLE, FALSE);
            k.light(0, Dx::directional(-0.3f, -0.5f, 1.0f, 1, 1, 1));
        });
    });
    for (int on = 0; on < 2; ++on) {
        Cases::single(rr::CaseSpec("ff", on ? "light_normalize_on_scaled" : "light_normalize_off_scaled").tolerance(lit_tol), [on](Ctx& c) {
            sphere_scene(c, grey(), Dx::kColor(10, 10, 10), [on](Ctx& k) {
                k.xf(D3DTS_WORLD, Xf::scale(0.6f, 0.6f, 0.6f));
                k.rs(D3DRS_NORMALIZENORMALS, on ? TRUE : FALSE);
                k.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
            });
        });
    }
    for (int cull = 0; cull < 2; ++cull) {
        Cases::single(rr::CaseSpec("ff", cull ? "light_two_sided_cull_cw" : "light_two_sided_cull_ccw").tolerance(lit_tol), [cull](Ctx& c) {
            clear_blue(c);
            lit(c, grey(), Dx::kColor(20, 20, 20));
            c.rs(D3DRS_CULLMODE, cull ? D3DCULL_CW : D3DCULL_CCW);
            c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.draw_mesh(sphere());
        });
    }
}

void fog_scene(Ctx& c, const std::function<void(Ctx&)>& fog, bool textured = true) {
    c.clear(Dx::kColor(150, 160, 170));
    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
    c.rs(D3DRS_FOGENABLE, TRUE);
    c.rs(D3DRS_FOGCOLOR, Dx::kColor(150, 160, 170));
    c.xf(D3DTS_WORLD, Xf());
    const float eye[3] = {0, 2.4f, -6.0f};
    const float at[3] = {0, 0, 1.0f};
    const float up[3] = {0, 1, 0};
    c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
    c.xf(D3DTS_PROJECTION, Xf::perspective(1.0f, 1.0f, 0.5f, 30.0f));
    Com<IDirect3DTexture9> t = textured ? checker(c) : Com<IDirect3DTexture9>();
    if (textured) {
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    fog(c);
    static const Mesh terrain = Geo::grid(4, 8.0f, 0.5f, kWhite);
    c.fvf(kVtxFvf);
    c.draw_mesh(terrain);
}

void fog_cases() {
    struct Mode { const char* name; DWORD mode; float density; };
    const Mode modes[] = {{"linear", D3DFOG_LINEAR, 0}, {"exp", D3DFOG_EXP, 0.08f}, {"exp2", D3DFOG_EXP2, 0.1f}};
    for (const Mode& m : modes) {
        Cases::single(rr::CaseSpec("ff", std::string("fog_vertex_") + m.name).tolerance(rr::Tolerance::blend(4)), [m](Ctx& c) {
            fog_scene(c, [m](Ctx& k) {
                k.rs(D3DRS_FOGVERTEXMODE, m.mode);
                k.rs(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
                k.rsf(D3DRS_FOGSTART, 4.0f);
                k.rsf(D3DRS_FOGEND, 16.0f);
                k.rsf(D3DRS_FOGDENSITY, m.density);
            });
        });
        Cases::single(rr::CaseSpec("ff", std::string("fog_table_") + m.name).tolerance(rr::Tolerance::blend(4)), [m](Ctx& c) {
            fog_scene(c, [m](Ctx& k) {
                k.rs(D3DRS_FOGTABLEMODE, m.mode);
                k.rs(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
                k.rsf(D3DRS_FOGSTART, 4.0f);
                k.rsf(D3DRS_FOGEND, 16.0f);
                k.rsf(D3DRS_FOGDENSITY, m.density);
            });
        }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE) != 0; }, "table fog"));
    }
    Cases::single(rr::CaseSpec("ff", "fog_vertex_range").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        fog_scene(c, [](Ctx& k) {
            k.rs(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
            k.rs(D3DRS_RANGEFOGENABLE, TRUE);
            k.rsf(D3DRS_FOGSTART, 4.0f);
            k.rsf(D3DRS_FOGEND, 14.0f);
        });
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_FOGRANGE) != 0; }, "range fog"));
    Cases::single(rr::CaseSpec("ff", "fog_table_linear_w_buffer").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        fog_scene(c, [](Ctx& k) {
            k.rs(D3DRS_ZENABLE, D3DZB_USEW);
            k.rs(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
            k.rs(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
            k.rsf(D3DRS_FOGSTART, 4.0f);
            k.rsf(D3DRS_FOGEND, 16.0f);
        });
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_WBUFFER) != 0 && (caps.RasterCaps & D3DPRASTERCAPS_WFOG) != 0; }, "W buffer and W fog"));
    Cases::single(rr::CaseSpec("ff", "fog_color_untextured").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        fog_scene(c, [](Ctx& k) {
            k.rs(D3DRS_FOGCOLOR, Dx::kColor(230, 90, 40));
            k.rs(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
            k.rsf(D3DRS_FOGSTART, 2.0f);
            k.rsf(D3DRS_FOGEND, 12.0f);
        }, false);
    });
    Cases::single(rr::CaseSpec("ff", "fog_specular_alpha_fog").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(100, 120, 140));
        c.rs(D3DRS_FOGENABLE, TRUE);
        c.rs(D3DRS_FOGCOLOR, Dx::kColor(255, 255, 255));
        c.rs(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        c.rs(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
        c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR);
        struct V { float x, y, z, rhw; DWORD color; DWORD spec; };
        std::vector<V> v;
        for (int i = 0; i < 8; ++i) {
            const float x0 = 8.0f + 15.0f * static_cast<float>(i);
            const DWORD f = static_cast<DWORD>(i * 255 / 7) << 24;
            v.push_back({x0, 10, 0.5f, 1, 0xFF30A030, f});
            v.push_back({x0 + 14, 10, 0.5f, 1, 0xFF30A030, f});
            v.push_back({x0, 118, 0.5f, 1, 0xFF30A030, f});
            v.push_back({x0 + 14, 10, 0.5f, 1, 0xFF30A030, f});
            v.push_back({x0 + 14, 118, 0.5f, 1, 0xFF30A030, f});
            v.push_back({x0, 118, 0.5f, 1, 0xFF30A030, f});
        }
        c.draw(D3DPT_TRIANGLELIST, 16, v.data(), sizeof(V));
    });
}

void transform_cases() {
    Cases::single(rr::CaseSpec("ff", "xform_four_worlds"), [](Ctx& c) {
        clear_blue(c);
        c.camera(7.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        static const Mesh cube = Geo::cube(0.8f, kWhite);
        Mesh m = cube;
        const DWORD cols[6] = {0xFFE04040, 0xFF40E040, 0xFF4040E0, 0xFFE0E040, 0xFFE040E0, 0xFF40E0E0};
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            m.vertices[i].diffuse = cols[i / 4];
        }
        c.fvf(kVtxFvf);
        const Xf worlds[4] = {Xf::rot_y(0.6f) * Xf::translate(-1.5f, 1.2f, 0), Xf::rot_x(0.6f) * Xf::translate(1.5f, 1.2f, 0.5f),
                              Xf::rot_z(0.6f) * Xf::translate(-1.5f, -1.2f, 0.5f), Xf::scale(0.7f, 1.2f, 0.7f) * Xf::rot_y(1.0f) * Xf::translate(1.5f, -1.2f, 0)};
        for (const Xf& w : worlds) {
            c.xf(D3DTS_WORLD, w);
            c.draw_mesh(m);
        }
    });

    struct Gen { const char* name; DWORD index; };
    const Gen gens[] = {{"camera_normal", D3DTSS_TCI_CAMERASPACENORMAL}, {"camera_position", D3DTSS_TCI_CAMERASPACEPOSITION},
                        {"reflection", D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR}, {"spheremap", D3DTSS_TCI_SPHEREMAP}};
    for (const Gen& g : gens) {
        Cases::single(rr::CaseSpec("ff", std::string("xform_texgen_") + g.name).tolerance(rr::Tolerance::filter(6, 0.003)), [g](Ctx& c) {
            clear_blue(c);
            lit(c, grey(), Dx::kColor(255, 255, 255));
            c.rs(D3DRS_LIGHTING, FALSE);
            Com<IDirect3DTexture9> t = rainbow(c);
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            c.tss(0, D3DTSS_TEXCOORDINDEX, g.index);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            Xf tm = Xf();
            if (g.index != D3DTSS_TCI_SPHEREMAP) {
                const float k = g.index == D3DTSS_TCI_CAMERASPACEPOSITION ? 0.3f : 0.5f;
                tm.m[0][0] = k;
                tm.m[1][1] = -k;
                tm.m[3][0] = 0.5f;
                tm.m[3][1] = 0.5f;
            }
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), tm);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.draw_mesh(sphere());
        });
    }

    Cases::single(rr::CaseSpec("ff", "xform_tex_count1_scale"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t = c.texture(32, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT1);
        Xf tm;
        tm.m[0][0] = 3.0f;
        tm.m[3][0] = 0.25f;
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), tm);
        struct V { float x, y, z, u; };
        const float s = 0.85f;
        const V q[4] = {{-s, s, 0.5f, 0}, {s, s, 0.5f, 1}, {-s, -s, 0.5f, 0}, {s, -s, 0.5f, 1}};
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE1(0));
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    });
    Cases::single(rr::CaseSpec("ff", "xform_tex_count2_rotate"), [](Ctx& c) {
        clear_blue(c);
        Com<IDirect3DTexture9> t = checker(c);
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        Xf tm = Xf::translate(-0.5f, -0.5f, 0) * Xf::rot_z(0.5f) * Xf::scale(2.0f, 2.0f, 1.0f);
        Xf shift;
        shift.m[2][0] = 0.5f;
        shift.m[2][1] = 0.5f;
        tm = tm * Xf::translate(0, 0, 0);
        Xf use;
        use.m[0][0] = tm.m[0][0];
        use.m[0][1] = tm.m[0][1];
        use.m[1][0] = tm.m[1][0];
        use.m[1][1] = tm.m[1][1];
        use.m[2][0] = 0.5f + tm.m[3][0];
        use.m[2][1] = 0.5f + tm.m[3][1];
        c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), use);
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(kVtxFvf);
        const std::vector<Vtx> q = Geo::quad(-0.85f, -0.85f, 0.85f, 0.85f, 0.5f, kWhite);
        c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(Vtx));
    });

    for (int n = 3; n <= 4; ++n) {
        Cases::single(rr::CaseSpec("ff", "xform_tex_projected_count" + std::to_string(n)), [n](Ctx& c) {
            clear_blue(c);
            Com<IDirect3DTexture9> t = rainbow(c);
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, (n == 3 ? D3DTTFF_COUNT3 : D3DTTFF_COUNT4) | D3DTTFF_PROJECTED);
            Xf tm;
            tm.m[2][3] = 0.0f;
            tm.m[1][3] = 0.0f;
            tm.m[0][2] = 0.0f;
            if (n == 3) {
                tm.m[0][2] = 0.0f;
                tm.m[1][2] = 0.5f;
                tm.m[0][0] = 1.0f;
                tm.m[1][1] = 1.0f;
                tm.m[2][2] = 1.0f;
                tm.m[1][2] = 0.0f;
                tm.m[1][1] = 0.0f;
                tm.m[1][2] = 0.0f;
                tm.m[2][2] = 0.0f;
                tm.m[1][1] = 1.0f;
                tm.m[2][2] = 1.0f;
            } else {
                tm.m[0][3] = 0.35f;
                tm.m[1][3] = 0.0f;
            }
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), tm);
            c.xf(D3DTS_WORLD, Xf());
            c.xf(D3DTS_VIEW, Xf());
            c.xf(D3DTS_PROJECTION, Xf());
            struct V { float x, y, z, u, v, w, q; };
            const float s = 0.85f;
            const float w0 = n == 3 ? 1.0f : 1.0f;
            const V quad[4] = {{-s, s, 0.5f, 0, 0, 1.0f, w0}, {s, s, 0.5f, 1, 0, 1.0f, w0}, {-s, -s, 0.5f, 0, 1, 2.0f, w0}, {s, -s, 0.5f, 1, 1, 2.0f, w0}};
            c.fvf(D3DFVF_XYZ | (n == 3 ? D3DFVF_TEXCOORDSIZE3(0) : D3DFVF_TEXCOORDSIZE4(0)) | D3DFVF_TEX1);
            c.draw(D3DPT_TRIANGLESTRIP, 2, quad, static_cast<UINT>((3 + n) * 4));
        });
    }

    Cases::single(rr::CaseSpec("ff", "xform_user_clip_planes"), [](Ctx& c) {
        clear_blue(c);
        c.camera(4.5f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        const float p0[4] = {1.0f, 0.0f, 0.0f, 0.4f};
        const float p1[4] = {0.0f, -1.0f, 0.0f, 0.7f};
        c.chk(c.d->SetClipPlane(0, p0), "SetClipPlane");
        c.chk(c.d->SetClipPlane(1, p1), "SetClipPlane");
        c.rs(D3DRS_CLIPPLANEENABLE, 3);
        Mesh m = Geo::cube(1.2f, kWhite);
        const DWORD cols[6] = {0xFFE04040, 0xFF40E040, 0xFF4040E0, 0xFFE0E040, 0xFFE040E0, 0xFF40E0E0};
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            m.vertices[i].diffuse = cols[i / 4];
        }
        c.xf(D3DTS_WORLD, Xf::rot_y(0.6f) * Xf::rot_x(0.4f));
        c.fvf(kVtxFvf);
        c.draw_mesh(m);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxUserClipPlanes >= 2; }, "two user clip planes"));

    Cases::single(rr::CaseSpec("ff", "xform_point_scale").tolerance(rr::Tolerance::raster(4, 1)), [](Ctx& c) {
        clear_blue(c);
        c.camera(6.0f);
        c.rs(D3DRS_POINTSPRITEENABLE, TRUE);
        c.rs(D3DRS_POINTSCALEENABLE, TRUE);
        c.rsf(D3DRS_POINTSIZE, 0.2f);
        c.rsf(D3DRS_POINTSIZE_MIN, 1.0f);
        c.rsf(D3DRS_POINTSIZE_MAX, 80.0f);
        c.rsf(D3DRS_POINTSCALE_A, 0.0f);
        c.rsf(D3DRS_POINTSCALE_B, 0.0f);
        c.rsf(D3DRS_POINTSCALE_C, 0.01f);
        struct V { float x, y, z; DWORD color; };
        std::vector<V> v;
        for (int i = 0; i < 8; ++i) {
            v.push_back({-1.6f + 0.45f * static_cast<float>(i), 0.8f * std::sin(static_cast<float>(i)), -2.5f + 1.3f * static_cast<float>(i), Dx::kColor(255, 40 + 30 * i, 80)});
        }
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        c.draw(D3DPT_POINTLIST, 8, v.data(), sizeof(V));
    });

    Cases::single(rr::CaseSpec("ff", "xform_point_size_minmax").tolerance(rr::Tolerance::raster(4, 1)), [](Ctx& c) {
        clear_blue(c);
        c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        c.rs(D3DRS_POINTSPRITEENABLE, FALSE);
        struct V { float x, y, z, rhw; DWORD color; };
        const float sizes[4] = {2.0f, 6.0f, 14.0f, 30.0f};
        for (int i = 0; i < 4; ++i) {
            c.rsf(D3DRS_POINTSIZE_MIN, 4.0f);
            c.rsf(D3DRS_POINTSIZE_MAX, 20.0f);
            c.rsf(D3DRS_POINTSIZE, sizes[i]);
            const V p{20.0f + 30.0f * static_cast<float>(i), 64.0f, 0.5f, 1.0f, 0xFFFFD040};
            c.draw(D3DPT_POINTLIST, 1, &p, sizeof(V));
        }
    });

    Cases::single(rr::CaseSpec("ff", "xform_wireframe").tolerance(rr::Tolerance::raster(4, 1)), [](Ctx& c) {
        clear_blue(c);
        c.camera(4.0f);
        c.rs(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        c.xf(D3DTS_WORLD, Xf::rot_x(0.4f));
        c.fvf(kVtxFvf);
        Mesh m = Geo::sphere(10, 8, 1.3f, 0xFFA0F0A0);
        c.draw_mesh(m);
    });

    Cases::single(rr::CaseSpec("ff", "xform_flat_shading").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        clear_blue(c);
        lit(c, grey(), Dx::kColor(25, 25, 25));
        c.rs(D3DRS_SHADEMODE, D3DSHADE_FLAT);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
        c.draw_mesh(Geo::sphere(10, 8, 1.3f, kWhite));
    });

    Cases::single(rr::CaseSpec("ff", "xform_gouraud_vs_flat_diffuse"), [](Ctx& c) {
        clear_blue(c);
        c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        struct V { float x, y, z, rhw; DWORD color; };
        const V tri[3] = {{10, 60, 0.5f, 1, 0xFFFF0000}, {54, 8, 0.5f, 1, 0xFF00FF00}, {54, 60, 0.5f, 1, 0xFF0000FF}};
        c.rs(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        c.draw(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
        const V tri2[3] = {{70, 60, 0.5f, 1, 0xFFFF0000}, {114, 8, 0.5f, 1, 0xFF00FF00}, {114, 60, 0.5f, 1, 0xFF0000FF}};
        c.rs(D3DRS_SHADEMODE, D3DSHADE_FLAT);
        c.draw(D3DPT_TRIANGLELIST, 1, tri2, sizeof(V));
    });

    Cases::single(rr::CaseSpec("ff", "xform_view_look_at_orbit"), [](Ctx& c) {
        clear_blue(c);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        const float eye[3] = {3.5f, 3.0f, -4.5f};
        const float at[3] = {0, 0, 0};
        const float up[3] = {0, 1, 0};
        c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
        c.xf(D3DTS_PROJECTION, Xf::perspective(0.9f, 1.0f, 0.5f, 30.0f));
        Com<IDirect3DTexture9> t = checker(c);
        c.tex(0, t.get());
        c.fvf(kVtxFvf);
        c.xf(D3DTS_WORLD, Xf::translate(0, -1.2f, 0));
        c.draw_mesh(Geo::grid(8, 3.0f, 0.2f, kWhite));
        c.xf(D3DTS_WORLD, Xf::translate(0, 0.0f, 0));
        c.draw_mesh(Geo::cube(0.9f, 0xFFFFC0C0));
    });

    Cases::single(rr::CaseSpec("ff", "xform_ortho_projection"), [](Ctx& c) {
        clear_blue(c);
        c.xf(D3DTS_WORLD, Xf::rot_y(0.5f) * Xf::rot_x(0.5f));
        c.xf(D3DTS_VIEW, Xf::translate(0, 0, 3.0f));
        c.xf(D3DTS_PROJECTION, Xf::ortho(4.0f, 4.0f, 0.1f, 10.0f));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        Mesh m = Geo::cube(1.0f, kWhite);
        const DWORD cols[6] = {0xFFE04040, 0xFF40E040, 0xFF4040E0, 0xFFE0E040, 0xFFE040E0, 0xFF40E0E0};
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            m.vertices[i].diffuse = cols[i / 4];
        }
        c.fvf(kVtxFvf);
        c.draw_mesh(m);
    });
}

const Registrar kFvf(fvf_cases);
const Registrar kLight(light_cases);
const Registrar kFog(fog_cases);
const Registrar kTransform(transform_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
