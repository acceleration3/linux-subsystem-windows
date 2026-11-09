#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

const Layout kScene{D3DFMT_A8R8G8B8, D3DFMT_D24S8, 256};

rr::CaseSpec scene(const char* name, unsigned frames, std::vector<unsigned> captured, rr::Tolerance tol = rr::Tolerance::blend(3)) {
    return rr::CaseSpec("scene", name).size(256, 256).frames(frames, std::move(captured)).tolerance(std::move(tol));
}

void look(Ctx& c, float ex, float ey, float ez, float ax, float ay, float az, float fov = 1.0f) {
    const float eye[3] = {ex, ey, ez};
    const float at[3] = {ax, ay, az};
    const float up[3] = {0, 1, 0};
    c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
    c.xf(D3DTS_PROJECTION, Xf::perspective(fov, 1.0f, 0.3f, 80.0f));
}

Com<IDirect3DTexture9> grass_texture(Ctx& c) {
    return c.texture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
        const unsigned n = (x * 7 + y * 13 + (x ^ y) * 3) & 31;
        return Texels::argb(255, 40 + n, 110 + n * 2, 40 + n / 2);
    });
}

Com<IDirect3DTexture9> leaf_texture(Ctx& c) {
    return c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
        const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 0.5f;
        const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) - 0.35f;
        const bool inside = dx * dx * 5.0f + dy * dy * 2.0f < 0.22f || (std::fabs(dx) < 0.04f && y > h / 2);
        return inside ? Texels::argb(255, 30, 140 + (y * 100 / h), 40) : Texels::argb(0, 0, 0, 0);
    });
}

void tint(Mesh& m, DWORD color) {
    for (Vtx& v : m.vertices) {
        v.diffuse = color;
    }
}

void game_scenes() {
    Cases::frames(scene("game_terrain_fog_foliage", 6, {0, 3, 5}, rr::Tolerance::blend(4)), [] {
        struct State {
            Com<IDirect3DTexture9> grass, leaf, light, brick;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->grass = grass_texture(c);
                st->leaf = leaf_texture(c);
                st->brick = c.texture(32, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
                    const bool mortar = (y % 8 == 0) || ((x + (y / 8 % 2) * 8) % 16 == 0);
                    return mortar ? Texels::argb(255, 200, 200, 190) : Texels::argb(255, 150 + (x * 3 % 20), 70, 55);
                });
                st->light = c.texture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float d = std::sqrt(std::pow(static_cast<float>(x) / static_cast<float>(w) - 0.5f, 2.0f) + std::pow(static_cast<float>(y) / static_cast<float>(h) - 0.5f, 2.0f));
                    const unsigned v = static_cast<unsigned>(std::clamp(1.1f - d * 1.5f, 0.35f, 1.0f) * 255);
                    return Texels::argb(255, v, v, v);
                });
            }
            const float a = 0.12f * static_cast<float>(f);
            c.clear(Dx::kColor(150, 175, 205));
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.xf(D3DTS_WORLD, Xf());
            look(c, -4.0f + 8.0f * a, 2.6f, -9.0f + 4.0f * a, 0, 0.5f, 4.0f);
            c.rs(D3DRS_FOGENABLE, TRUE);
            c.rs(D3DRS_FOGCOLOR, Dx::kColor(150, 175, 205));
            c.rs(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
            c.rsf(D3DRS_FOGSTART, 6.0f);
            c.rsf(D3DRS_FOGEND, 28.0f);
            c.fvf(kVtxFvf);
            for (DWORD s = 0; s < 2; ++s) {
                c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                c.ss(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                c.ss(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
                c.ss(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            }
            c.tex(0, st->grass.get());
            static const Mesh terrain = Geo::grid(24, 12.0f, 0.7f, 0xFFFFFFFF);
            Mesh t = terrain;
            for (Vtx& v : t.vertices) {
                v.u *= 2.0f;
                v.v *= 2.0f;
            }
            c.draw_mesh(t);
            c.tex(0, st->brick.get());
            c.tex(1, st->light.get());
            c.tss(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
            c.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            c.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            c.tss(1, D3DTSS_TEXCOORDINDEX, 0);
            Mesh house = Geo::cube(1.2f, 0xFFFFFFFF);
            c.xf(D3DTS_WORLD, Xf::translate(0, 1.5f, 5.0f));
            c.draw_mesh(house);
            c.tss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            c.tex(1, nullptr);
            c.tex(0, st->leaf.get());
            c.rs(D3DRS_ALPHATESTENABLE, TRUE);
            c.rs(D3DRS_ALPHAREF, 0x80);
            c.rs(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
            for (int i = 0; i < 10; ++i) {
                const float x = -6.0f + 1.4f * static_cast<float>(i);
                const float z = 2.0f + 3.0f * std::sin(static_cast<float>(i) * 1.7f) + 3.0f;
                const float sway = 0.12f * std::sin(a * 3.0f + static_cast<float>(i));
                for (int k = 0; k < 2; ++k) {
                    c.xf(D3DTS_WORLD, Xf::rot_y(1.5708f * static_cast<float>(k) + sway) * Xf::translate(x, 1.0f, z));
                    const std::vector<Vtx> q = Geo::quad(-0.8f, -0.9f, 0.8f, 1.1f, 0.0f, 0xFFFFFFFF);
                    c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(Vtx));
                }
            }
            c.rs(D3DRS_ALPHATESTENABLE, FALSE);
            c.rs(D3DRS_FOGENABLE, FALSE);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxSimultaneousTextures >= 2; }, "two textures"));

    Cases::frames(scene("skinned_character_indexed_blend", 6, {0, 3, 5}, rr::Tolerance::blend(4)), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            c.clear(Dx::kColor(30, 34, 52));
            c.camera(6.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_LIGHTING, FALSE);
            c.rs(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
            c.rs(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
            const float a = 0.5f * std::sin(0.9f * static_cast<float>(f));
            for (DWORD i = 0; i < 4; ++i) {
                const float y = -1.8f + 1.2f * static_cast<float>(i);
                c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(i)), Xf::translate(0, -y, 0) * Xf::rot_z(a * static_cast<float>(i)) * Xf::translate(0, y, 0));
            }
            struct V { float x, y, z, w; DWORD idx; DWORD color; };
            std::vector<V> verts;
            const unsigned rows = 30;
            const unsigned slices = 10;
            std::vector<std::uint16_t> idx;
            for (unsigned r = 0; r < rows; ++r) {
                const float t = static_cast<float>(r) / static_cast<float>(rows - 1);
                const float y = -1.8f + 3.6f * t;
                const float seg = t * 3.0f;
                const unsigned b0 = std::min(2u, static_cast<unsigned>(seg));
                const float w = seg - static_cast<float>(b0);
                for (unsigned s = 0; s <= slices; ++s) {
                    const float ang = 6.2831853f * static_cast<float>(s) / static_cast<float>(slices);
                    const float rad = 0.55f - 0.2f * std::fabs(t - 0.5f);
                    const DWORD packed = b0 | ((b0 + 1) << 8);
                    verts.push_back({rad * std::cos(ang), y, rad * std::sin(ang), w, packed, Dx::kColor(static_cast<unsigned>(120 + 120 * t), 160, static_cast<unsigned>(220 - 120 * t))});
                }
            }
            for (unsigned r = 0; r + 1 < rows; ++r) {
                for (unsigned s = 0; s < slices; ++s) {
                    const std::uint16_t a0 = static_cast<std::uint16_t>(r * (slices + 1) + s);
                    const std::uint16_t b = static_cast<std::uint16_t>(a0 + 1);
                    const std::uint16_t d = static_cast<std::uint16_t>(a0 + slices + 1);
                    const std::uint16_t e = static_cast<std::uint16_t>(d + 1);
                    idx.insert(idx.end(), {a0, b, e, a0, e, d});
                }
            }
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.fvf(D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE);
            c.draw_idx(D3DPT_TRIANGLELIST, static_cast<UINT>(verts.size()), static_cast<UINT>(idx.size() / 3), idx.data(), D3DFMT_INDEX16, verts.data(), sizeof(V));
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexBlendMatrixIndex >= 4 && (caps.DeclTypes & D3DDTCAPS_UBYTE4) != 0; }, "indexed vertex blending"));

    Cases::frames(scene("stencil_shadow_volume_two_sided", 4, {0, 1, 3}), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            c.clear(Dx::kColor(60, 70, 90), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 0);
            look(c, 0, 6.0f, -8.0f, 0, 0, 0);
            c.xf(D3DTS_WORLD, Xf());
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.rs(D3DRS_LIGHTING, FALSE);
            c.fvf(kVtxFvf);
            Mesh floor = Geo::grid(1, 5.0f, 0.0f, 0xFFB8C8B0);
            c.draw_mesh(floor);
            const float la = 0.8f * static_cast<float>(f);
            const float lx = 3.0f * std::cos(la);
            const float lz = 3.0f * std::sin(la);
            const float ly = 5.0f;
            Mesh cube = Geo::cube(0.8f, 0xFFD06040);
            c.xf(D3DTS_WORLD, Xf::translate(0, 1.0f, 0));
            c.draw_mesh(cube);
            std::vector<Vtx> vol;
            const float corners[4][2] = {{-0.8f, -0.8f}, {0.8f, -0.8f}, {0.8f, 0.8f}, {-0.8f, 0.8f}};
            const auto vtx = [](float x, float y, float z) { return Vtx{x, y, z, 0, 1, 0, 0xFFFFFFFF, 0, 0, 0}; };
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                const float ax = corners[i][0];
                const float az = corners[i][1];
                const float bx = corners[j][0];
                const float bz = corners[j][1];
                const auto far_point = [&](float x, float z) {
                    const float dx = x - lx;
                    const float dy = 1.8f - ly;
                    const float dz = z - lz;
                    return vtx(x + dx * 5.0f, 1.8f + dy * 5.0f, z + dz * 5.0f);
                };
                const Vtx a0 = vtx(ax, 1.8f, az);
                const Vtx b0 = vtx(bx, 1.8f, bz);
                const Vtx a1 = far_point(ax, az);
                const Vtx b1 = far_point(bx, bz);
                vol.insert(vol.end(), {a0, a1, b0, b0, a1, b1});
            }
            c.xf(D3DTS_WORLD, Xf());
            c.rs(D3DRS_COLORWRITEENABLE, 0);
            c.rs(D3DRS_ZWRITEENABLE, FALSE);
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.rs(D3DRS_STENCILENABLE, TRUE);
            c.rs(D3DRS_TWOSIDEDSTENCILMODE, TRUE);
            c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
            c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
            c.rs(D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS);
            c.rs(D3DRS_CCW_STENCILPASS, D3DSTENCILOP_DECR);
            c.draw(D3DPT_TRIANGLELIST, static_cast<UINT>(vol.size() / 3), vol.data(), sizeof(Vtx));
            c.rs(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
            c.rs(D3DRS_COLORWRITEENABLE, 15);
            c.rs(D3DRS_STENCILFUNC, D3DCMP_NOTEQUAL);
            c.rs(D3DRS_STENCILREF, 0);
            c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            c.quad(0, 0, 256, 256, 0.5f, 0x90000010);
            c.rs(D3DRS_STENCILENABLE, FALSE);
            c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            c.rs(D3DRS_ZWRITEENABLE, TRUE);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return (caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) != 0; }, "two sided stencil"));

    Cases::frames(scene("projected_texture_spotlight", 4, {0, 3}, rr::Tolerance::filter(6, 0.004)), [] {
        auto tex = std::make_shared<Com<IDirect3DTexture9>>();
        return FrameBody([tex](Ctx& c, unsigned f) {
            if (f == 0) {
                *tex = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float d = std::sqrt(std::pow(static_cast<float>(x) / static_cast<float>(w) - 0.5f, 2.0f) + std::pow(static_cast<float>(y) / static_cast<float>(h) - 0.5f, 2.0f));
                    const unsigned v = static_cast<unsigned>(std::clamp(1.0f - d * 2.0f, 0.0f, 1.0f) * 255);
                    return Texels::argb(255, v, v, v * 9 / 10);
                });
                c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
                c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
                c.ss(0, D3DSAMP_BORDERCOLOR, 0xFF000000);
                c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }
            c.clear(Dx::kColor(10, 12, 24));
            look(c, 0, 5.0f, -7.0f, 0, 0, 1.0f);
            c.xf(D3DTS_WORLD, Xf());
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.tex(0, tex->get());
            const float lx = 2.0f * std::sin(0.6f * static_cast<float>(f));
            const float eye[3] = {lx, 6.0f, -1.0f};
            const float at[3] = {lx * 0.5f, 0, 1.0f};
            const float up[3] = {0, 0, 1};
            Xf light_view = Xf::look_at(eye, at, up);
            Xf light_proj = Xf::perspective(0.9f, 1.0f, 0.5f, 30.0f);
            Xf bias;
            bias.m[0][0] = 0.5f;
            bias.m[1][1] = -0.5f;
            bias.m[3][0] = 0.5f;
            bias.m[3][1] = 0.5f;
            Xf view;
            const float ceye[3] = {0, 5.0f, -7.0f};
            const float cat[3] = {0, 0, 1.0f};
            const float cup[3] = {0, 1, 0};
            view = Xf::look_at(ceye, cat, cup);
            Xf rot_t;
            for (int r = 0; r < 3; ++r) {
                for (int k = 0; k < 3; ++k) {
                    rot_t.m[r][k] = view.m[k][r];
                }
            }
            const Xf cam_to_world = rot_t * Xf::translate(ceye[0], ceye[1], ceye[2]);
            c.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), cam_to_world * light_view * light_proj * bias);
            c.fvf(kVtxFvf);
            c.draw_mesh(Geo::grid(12, 6.0f, 0.4f, 0xFFB0B8C0));
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            c.tss(0, D3DTSS_TEXCOORDINDEX, 0);
        });
    }, kScene);
}

void effect_scenes() {
    Cases::frames(scene("environment_mapped_car_cube_and_sphere", 4, {0, 3}, rr::Tolerance::filter(8, 0.006)), [] {
        struct State {
            Com<IDirect3DCubeTexture9> cube;
            Com<IDirect3DTexture9> sphere_map;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                c.chk(c.d->CreateCubeTexture(32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, st->cube.put(), nullptr), "CreateCubeTexture");
                for (int face = 0; face < 6 && st->cube; ++face) {
                    D3DLOCKED_RECT lr{};
                    c.chk(st->cube->LockRect(static_cast<D3DCUBEMAP_FACES>(face), 0, &lr, nullptr, 0), "LockRect");
                    if (lr.pBits == nullptr) {
                        return;
                    }
                    for (UINT y = 0; y < 32; ++y) {
                        DWORD* row = reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(lr.pBits) + static_cast<std::size_t>(y) * lr.Pitch);
                        for (UINT x = 0; x < 32; ++x) {
                            const bool sky = face == 2;
                            const bool ground = face == 3;
                            row[x] = sky ? Dx::kColor(90, 140, 230 - y * 3) : ground ? Dx::kColor(70, 100 + x, 60) : (y < 16 ? Dx::kColor(150 + x * 2, 190, 230) : Dx::kColor(80, 90 + (face * 20), 70));
                        }
                    }
                    st->cube->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), 0);
                }
                st->sphere_map = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float dx = static_cast<float>(x) / static_cast<float>(w) - 0.5f;
                    const float dy = static_cast<float>(y) / static_cast<float>(h) - 0.5f;
                    const float d = std::sqrt(dx * dx + dy * dy) * 2.0f;
                    const unsigned v = static_cast<unsigned>(std::clamp(1.2f - d, 0.1f, 1.0f) * 255);
                    return Texels::argb(255, v, v * 9 / 10, v * 7 / 10);
                });
            }
            c.clear(Dx::kColor(30, 40, 60));
            c.camera(6.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.fvf(kVtxFvf);
            for (DWORD s = 0; s < 1; ++s) {
                c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }
            const float r = 0.5f * static_cast<float>(f);
            c.tex(0, st->cube.get());
            c.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
            c.xf(D3DTS_WORLD, Xf::scale(1.0f, 0.6f, 1.7f) * Xf::rot_y(r) * Xf::translate(0, -0.7f, 0));
            c.draw_mesh(Geo::sphere(28, 18, 1.2f, 0xFFFFFFFF));
            c.tex(0, st->sphere_map.get());
            c.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_SPHEREMAP);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
            c.xf(D3DTS_WORLD, Xf::scale(0.7f, 0.5f, 0.9f) * Xf::rot_y(r * 0.5f) * Xf::translate(0, 0.8f, 0));
            c.draw_mesh(Geo::sphere(24, 16, 1.0f, 0xFFFFFFFF));
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            c.tss(0, D3DTSS_TEXCOORDINDEX, 0);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0; }, "cube textures"));

    Cases::frames(scene("bumpenvmap_water", 4, {0, 3}, rr::Tolerance::filter(10, 0.01)), [] {
        struct State {
            Com<IDirect3DTexture9> bump, sky;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->bump = c.texture(64, 64, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float fx = static_cast<float>(x) / static_cast<float>(w);
                    const float fy = static_cast<float>(y) / static_cast<float>(h);
                    return Texels::argb(255, 128 + static_cast<unsigned>(100.0f * std::sin(fx * 18.8f + fy * 6.3f)), 128 + static_cast<unsigned>(100.0f * std::cos(fy * 18.8f - fx * 6.3f)), 0);
                });
                st->sky = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
                    return Texels::argb(255, 40 + y * 2, 90 + y, 170 + y / 2 + ((x / 8 + y / 8) & 1) * 20);
                });
                c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
                c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                c.ss(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                c.ss(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
                for (DWORD s = 0; s < 2; ++s) {
                    c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                    c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                }
            }
            c.clear(Dx::kColor(10, 10, 30));
            c.tex(0, st->bump.get());
            c.tex(1, st->sky.get());
            c.tss(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
            c.tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            c.tss(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
            c.tss(0, D3DTSS_BUMPENVMAT00, Dx::fbits(0.08f));
            c.tss(0, D3DTSS_BUMPENVMAT01, 0);
            c.tss(0, D3DTSS_BUMPENVMAT10, 0);
            c.tss(0, D3DTSS_BUMPENVMAT11, Dx::fbits(0.08f));
            c.tss(0, D3DTSS_TEXCOORDINDEX, 0);
            c.tss(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            c.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            c.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            c.tss(1, D3DTSS_TEXCOORDINDEX, 1);
            c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
            struct V { float x, y, z, u0, v0, u1, v1; };
            const float shift = 0.13f * static_cast<float>(f);
            const V q[4] = {{-0.95f, 0.95f, 0.5f, shift, 0, 0, 0}, {0.95f, 0.95f, 0.5f, 3 + shift, 0, 1, 0}, {-0.95f, -0.95f, 0.5f, shift, 3, 0, 1}, {0.95f, -0.95f, 0.5f, 3 + shift, 3, 1, 1}};
            c.xf(D3DTS_WORLD, Xf());
            c.xf(D3DTS_VIEW, Xf());
            c.xf(D3DTS_PROJECTION, Xf());
            c.fvf(D3DFVF_XYZ | D3DFVF_TEX2);
            c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        });
    }, kScene, Needs::all({Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP) != 0; }, "bumpenvmap"), Needs::format(D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DRTYPE_TEXTURE, D3DFMT_V8U8, "V8U8 bump")}));

    Cases::frames(scene("dot3_lit_sphere_light_orbit", 5, {0, 2, 4}, rr::Tolerance::blend(5)), [] {
        struct State {
            Com<IDirect3DTexture9> normals, color;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->normals = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float fx = static_cast<float>(x) / static_cast<float>(w);
                    const float fy = static_cast<float>(y) / static_cast<float>(h);
                    const float nx = 0.5f * std::sin(fx * 37.7f);
                    const float ny = 0.5f * std::sin(fy * 37.7f);
                    const float nz = std::sqrt(1.0f - nx * nx - ny * ny);
                    return Texels::argb(255, static_cast<unsigned>((nx * 0.5f + 0.5f) * 255), static_cast<unsigned>((ny * 0.5f + 0.5f) * 255), static_cast<unsigned>((nz * 0.5f + 0.5f) * 255));
                });
                st->color = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(4, 0xFFE0C090, 0xFFB09060));
                for (DWORD s = 0; s < 2; ++s) {
                    c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                    c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                    c.ss(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
                    c.ss(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                }
            }
            c.clear(Dx::kColor(10, 10, 20));
            const float a = 0.9f * static_cast<float>(f);
            const float lx = std::cos(a) * 0.7f;
            const float ly = std::sin(a) * 0.7f;
            const float lz = 0.5f;
            const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
            c.rs(D3DRS_TEXTUREFACTOR, Dx::kColor(static_cast<unsigned>((lx / len * 0.5f + 0.5f) * 255), static_cast<unsigned>((ly / len * 0.5f + 0.5f) * 255), static_cast<unsigned>((lz / len * 0.5f + 0.5f) * 255)));
            c.tex(0, st->normals.get());
            c.tex(1, st->color.get());
            c.tss(0, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
            c.tss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            c.tss(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
            c.tss(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
            c.tss(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            c.tss(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            c.tss(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
            c.camera(3.6f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.tss(1, D3DTSS_TEXCOORDINDEX, 0);
            Mesh m = Geo::sphere(28, 18, 1.2f, 0xFFFFFFFF);
            for (Vtx& v : m.vertices) {
                v.u *= 3.0f;
                v.v *= 3.0f;
            }
            c.fvf(kVtxFvf);
            c.draw_mesh(m);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureOpCaps & D3DTEXOPCAPS_DOTPRODUCT3) != 0; }, "dot product 3"));

    Cases::frames(scene("light_orbit_eight_lights", 5, {0, 2, 4}, rr::Tolerance::blend(5)), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            c.clear(Dx::kColor(8, 8, 16));
            c.camera(5.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.rs(D3DRS_LIGHTING, TRUE);
            c.rs(D3DRS_AMBIENT, 0xFF101018);
            c.rs(D3DRS_SPECULARENABLE, TRUE);
            c.material(Dx::material(0.7f, 0.7f, 0.75f, 0.7f, 0.7f, 0.75f, 0, 0, 0, 0.8f, 0.8f, 0.8f, 20.0f));
            const float colors[8][3] = {{1, 0.3f, 0.3f}, {0.3f, 1, 0.3f}, {0.3f, 0.3f, 1}, {1, 1, 0.3f}, {1, 0.3f, 1}, {0.3f, 1, 1}, {1, 0.7f, 0.3f}, {0.7f, 0.7f, 1}};
            for (int i = 0; i < 8; ++i) {
                const float a = 0.785398f * static_cast<float>(i) + 0.35f * static_cast<float>(f);
                c.light(static_cast<DWORD>(i), Dx::point(2.8f * std::cos(a), 1.2f * std::sin(a * 2.0f), -2.4f + 1.0f * std::sin(a), 30.0f, 0.4f, 0.0f, 0.12f, colors[i][0], colors[i][1], colors[i][2]));
            }
            c.fvf(kVtxFvf);
            c.draw_mesh(Geo::sphere(32, 20, 1.3f, 0xFFFFFFFF));
            c.xf(D3DTS_WORLD, Xf::translate(0, -1.6f, 0));
            c.draw_mesh(Geo::grid(8, 4.0f, 0.0f, 0xFFFFFFFF));
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxActiveLights >= 8; }, "eight lights"));

    Cases::frames(scene("fog_flythrough_exp2", 5, {0, 2, 4}, rr::Tolerance::blend(4)), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            c.clear(Dx::kColor(160, 170, 180));
            look(c, 0, 1.6f, -6.0f + 3.0f * static_cast<float>(f), 0, 1.0f, 8.0f + 3.0f * static_cast<float>(f));
            c.xf(D3DTS_WORLD, Xf());
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.rs(D3DRS_FOGENABLE, TRUE);
            c.rs(D3DRS_FOGCOLOR, Dx::kColor(160, 170, 180));
            c.rs(D3DRS_FOGVERTEXMODE, D3DFOG_EXP2);
            c.rsf(D3DRS_FOGDENSITY, 0.07f);
            c.fvf(kVtxFvf);
            c.draw_mesh(Geo::grid(16, 14.0f, 0.2f, 0xFF708860));
            for (int i = 0; i < 8; ++i) {
                c.xf(D3DTS_WORLD, Xf::scale(1.0f, 1.0f + 0.4f * static_cast<float>(i % 3), 1.0f) * Xf::translate((i % 2 ? 2.5f : -2.5f), 1.0f, 4.0f + 4.0f * static_cast<float>(i)));
                c.draw_mesh(Geo::cube(0.9f, 0xFFB08060));
            }
        });
    }, kScene);

    Cases::frames(scene("instanced_grass_wind", 4, {0, 3}, rr::Tolerance::blend(4)), [] {
        struct State {
            Com<IDirect3DVertexBuffer9> blade, inst;
            Com<IDirect3DIndexBuffer9> ib;
            Com<IDirect3DVertexDeclaration9> decl;
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            struct Blade { float x, y, u; };
            struct Inst { float x, z, h, phase; DWORD color; };
            if (f == 0) {
                const Blade blade[6] = {{-0.06f, 0, 0}, {0.06f, 0, 0}, {-0.04f, 0.5f, 0.5f}, {0.04f, 0.5f, 0.5f}, {0, 1.0f, 1.0f}, {0, 1.0f, 1.0f}};
                const std::uint16_t idx[9] = {0, 1, 2, 2, 1, 3, 2, 3, 4};
                std::vector<Inst> inst;
                std::uint32_t seed = 7;
                for (int i = 0; i < 400; ++i) {
                    const float x = (rr::Pattern::unit_random(seed) - 0.5f) * 8.0f;
                    const float z = rr::Pattern::unit_random(seed) * 8.0f;
                    const float h = 0.5f + rr::Pattern::unit_random(seed) * 0.8f;
                    inst.push_back({x, z, h, rr::Pattern::unit_random(seed) * 6.0f, Dx::kColor(60 + static_cast<unsigned>(rr::Pattern::unit_random(seed) * 60), 150 + static_cast<unsigned>(rr::Pattern::unit_random(seed) * 90), 50)});
                }
                st->blade = c.vbuffer(blade, sizeof blade);
                st->inst = c.vbuffer(inst.data(), static_cast<UINT>(inst.size() * sizeof(Inst)));
                st->ib = c.ibuffer(idx, sizeof idx, D3DFMT_INDEX16);
                const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_POSITION, 0}, {0, 8, D3DDECLTYPE_FLOAT1, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                                {1, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_TEXCOORD, 1}, {1, 16, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
                st->decl = c.decl(el);
                st->vs = c.vs(
                    "float4 cm[4] : register(c0);\nfloat4 wind : register(c4);\n"
                    "struct I { float2 p : POSITION; float t : TEXCOORD0; float4 inst : TEXCOORD1; float4 col : COLOR0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
                    "O main(I i) { O o; float sway = sin(wind.x + i.inst.w) * 0.25 * i.t * i.t; float4 w = float4(i.inst.x + i.p.x + sway, i.p.y * i.inst.z, i.inst.y, 1.0);\n"
                    "o.p = float4(dot(w, cm[0]), dot(w, cm[1]), dot(w, cm[2]), dot(w, cm[3])); o.c = i.col * (0.5 + 0.5 * i.t); return o; }\n", "vs_3_0");
                st->ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_3_0");
            }
            if (!st->vs || !st->ps || !st->decl) {
                return;
            }
            c.clear(Dx::kColor(130, 170, 220));
            const float eye[3] = {0, 2.2f, -3.0f};
            const float at[3] = {0, 0.3f, 4.0f};
            const float up[3] = {0, 1, 0};
            float cmw[16];
            const Xf m = Xf::look_at(eye, at, up) * Xf::perspective(1.0f, 1.0f, 0.3f, 50.0f);
            for (int r = 0; r < 4; ++r) {
                for (int k = 0; k < 4; ++k) {
                    cmw[k * 4 + r] = m.m[r][k];
                }
            }
            c.chk(c.d->SetVertexShaderConstantF(0, cmw, 4), "const");
            const float wind[4] = {0.7f * static_cast<float>(f), 0, 0, 0};
            c.chk(c.d->SetVertexShaderConstantF(4, wind, 1), "const");
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.chk(c.d->SetVertexDeclaration(st->decl.get()), "SetVertexDeclaration");
            c.chk(c.d->SetVertexShader(st->vs.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(st->ps.get()), "SetPixelShader");
            c.chk(c.d->SetStreamSource(0, st->blade.get(), 0, sizeof(Blade)), "SetStreamSource");
            c.chk(c.d->SetStreamSource(1, st->inst.get(), 0, sizeof(Inst)), "SetStreamSource");
            c.chk(c.d->SetIndices(st->ib.get()), "SetIndices");
            c.chk(c.d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 400u), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u), "SetStreamSourceFreq");
            c.chk(c.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 5, 0, 3), "DrawIndexedPrimitive");
            c.chk(c.d->SetStreamSourceFreq(0, 1), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSourceFreq(1, 1), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSource(1, nullptr, 0, 0), "SetStreamSource");
            c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
        });
    }, kScene, Needs::shader(3, 3));
}

const Registrar kGame(game_scenes);
const Registrar kEffects(effect_scenes);

}  // namespace

}  // namespace lsw::suites::render::d3d9
