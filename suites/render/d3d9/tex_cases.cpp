#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

void tex_quad(Ctx& c, float x0, float y0, float x1, float y1, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, DWORD color = 0xFFFFFFFF) {
    const std::vector<TlVtx> q = Geo::tl_quad(x0, y0, x1, y1, color, u0, v0, u1, v1);
    c.fvf(kTlFvf);
    c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
}

void backdrop(Ctx& c) {
    c.clear(Dx::kColor(0, 0, 0, 255));
    c.quad(0, 0, 128, 128, 0.9f, 0xFF204060, 0xFF406020, 0xFF602040, 0xFFC0C040);
}

void sampler_linear(Ctx& c, DWORD stage = 0, bool linear = true) {
    c.ss(stage, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    c.ss(stage, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

Fill alpha_rainbow() {
    return [](UINT x, UINT y, UINT w, UINT h) {
        const std::uint32_t base = Texels::rainbow()(x, y, w, h);
        const unsigned a = 255 - x * 150 / std::max(1u, w - 1);
        return (base & 0x00FFFFFFu) | (a << 24);
    };
}

struct FormatInfo {
    const char* name;
    D3DFORMAT format;
    bool linear;
};

void format_cases() {
    const FormatInfo formats[] = {
        {"a8r8g8b8", D3DFMT_A8R8G8B8, true}, {"x8r8g8b8", D3DFMT_X8R8G8B8, true}, {"r5g6b5", D3DFMT_R5G6B5, true}, {"x1r5g5b5", D3DFMT_X1R5G5B5, true},
        {"a1r5g5b5", D3DFMT_A1R5G5B5, true}, {"a4r4g4b4", D3DFMT_A4R4G4B4, true}, {"a8", D3DFMT_A8, true}, {"l8", D3DFMT_L8, true},
        {"a8l8", D3DFMT_A8L8, true}, {"l16", D3DFMT_L16, true}, {"a2r10g10b10", D3DFMT_A2R10G10B10, true}, {"a2b10g10r10", D3DFMT_A2B10G10R10, true},
        {"a16b16g16r16", D3DFMT_A16B16G16R16, true}, {"g16r16", D3DFMT_G16R16, true}, {"r16f", D3DFMT_R16F, true}, {"g16r16f", D3DFMT_G16R16F, true},
        {"a16b16g16r16f", D3DFMT_A16B16G16R16F, true}, {"r32f", D3DFMT_R32F, false}, {"g32r32f", D3DFMT_G32R32F, false}, {"a32b32g32r32f", D3DFMT_A32B32G32R32F, false},
        {"v8u8", D3DFMT_V8U8, true}, {"q8w8v8u8", D3DFMT_Q8W8V8U8, true}, {"dxt1", D3DFMT_DXT1, true}, {"dxt3", D3DFMT_DXT3, true}, {"dxt5", D3DFMT_DXT5, true},
        {"a8b8g8r8", D3DFMT_A8B8G8R8, true}, {"x8b8g8r8", D3DFMT_X8B8G8R8, true}, {"dxt2", D3DFMT_DXT2, true}, {"dxt4", D3DFMT_DXT4, true},
        {"ati1", static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '1')), true}, {"ati2", static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '2')), true},
    };
    for (const FormatInfo& f : formats) {
        Cases::single(rr::CaseSpec("tex", std::string("format_") + f.name).tolerance(rr::Tolerance::blend(4)), [f](Ctx& c) {
            backdrop(c);
            const bool ati = std::strcmp(f.name, "ati1") == 0 || std::strcmp(f.name, "ati2") == 0;
            Com<IDirect3DTexture9> t;
            if (!ati) {
                t = c.texture(32, 32, 1, 0, f.format, D3DPOOL_MANAGED, alpha_rainbow());
            } else {
                c.chk(c.d->CreateTexture(32, 32, 1, 0, f.format, D3DPOOL_DEFAULT, t.put(), nullptr), "CreateTexture");
                Com<IDirect3DSurface9> sys;
                c.chk(c.d->CreateOffscreenPlainSurface(32, 32, f.format, D3DPOOL_SYSTEMMEM, sys.put(), nullptr), "CreateOffscreenPlainSurface");
                D3DLOCKED_RECT lr{};
                if (!t || !sys || FAILED(sys->LockRect(&lr, nullptr, 0))) {
                    c.fail("cannot stage the block compressed surface");
                    return;
                }
                const Fill fill = alpha_rainbow();
                for (UINT by = 0; by < 8; ++by) {
                    for (UINT bx = 0; bx < 8; ++bx) {
                        std::uint32_t texels[16];
                        for (UINT k = 0; k < 16; ++k) {
                            texels[k] = fill(bx * 4 + (k & 3), by * 4 + (k >> 2), 32, 32);
                        }
                        Texels::encode_block(f.format, texels, static_cast<std::uint8_t*>(lr.pBits) + static_cast<std::size_t>(by) * lr.Pitch + bx * Texels::block_bytes(f.format));
                    }
                }
                sys->UnlockRect();
                Com<IDirect3DSurface9> level;
                t->GetSurfaceLevel(0, level.put());
                c.chk(c.d->UpdateSurface(sys.get(), nullptr, level.get(), nullptr), "UpdateSurface");
            }
            c.tex(0, t.get());
            sampler_linear(c, 0, f.linear);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            tex_quad(c, 12, 12, 116, 116);
        }, {}, Needs::all({Needs::format(0, D3DRTYPE_TEXTURE, f.format, std::string("texture format ") + f.name), [f](IDirect3DDevice9* device) {
            if (std::strcmp(f.name, "ati1") != 0 && std::strcmp(f.name, "ati2") != 0) {
                return std::string();
            }
            Com<IDirect3DSurface9> probe;
            return SUCCEEDED(device->CreateOffscreenPlainSurface(32, 32, f.format, D3DPOOL_SYSTEMMEM, probe.put(), nullptr)) ? std::string() : std::string("system memory surfaces of the ") + f.name + " format";
        }}));
    }
}

const DWORD kLevelColors[8] = {0xFFFFFFFF, 0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFFFFFF40, 0xFFFF40FF, 0xFF40FFFF, 0xFF808080};

Com<IDirect3DTexture9> level_texture(Ctx& c, UINT size, D3DPOOL pool = D3DPOOL_MANAGED) {
    const Fill fill = [size](UINT x, UINT y, UINT w, UINT) {
        unsigned level = 0;
        while ((size >> level) > w) {
            ++level;
        }
        const bool dark = ((x * 4 / std::max(1u, w)) + (y * 4 / std::max(1u, w))) & 1u;
        const DWORD base = kLevelColors[level & 7];
        return dark ? (base & 0xFF000000u) | ((base & 0x00FEFEFEu) >> 1) : base;
    };
    return c.texture(size, size, 0, 0, D3DFMT_A8R8G8B8, pool, fill);
}

void ground(Ctx& c, float u_scale = 6.0f) {
    const float eye[3] = {0, 1.2f, -2.0f};
    const float at[3] = {0, 0, 6.0f};
    const float up[3] = {0, 1, 0};
    c.xf(D3DTS_WORLD, Xf());
    c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
    c.xf(D3DTS_PROJECTION, Xf::perspective(1.2f, 1.0f, 0.3f, 60.0f));
    const float x = 4.0f;
    const Vtx fixed[4] = {{-x, 0, 40, 0, 1, 0, 0xFFFFFFFF, 0, 0, 0}, {x, 0, 40, 0, 1, 0, 0xFFFFFFFF, 0, u_scale, 0},
                          {-x, 0, 0, 0, 1, 0, 0xFFFFFFFF, 0, 0, u_scale * 6.0f}, {x, 0, 0, 0, 1, 0, 0xFFFFFFFF, 0, u_scale, u_scale * 6.0f}};
    c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    c.fvf(kVtxFvf);
    c.draw(D3DPT_TRIANGLESTRIP, 2, fixed, sizeof(Vtx));
}

void mip_cases() {
    struct Combo {
        const char* name;
        DWORD min, mag, mip;
    };
    const Combo combos[] = {{"point_none", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE}, {"linear_none", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE},
                            {"point_point", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_POINT}, {"linear_point", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT},
                            {"linear_linear", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR}, {"point_linear", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_LINEAR}};
    const rr::Tolerance mip_tol = rr::Tolerance::raster(10, 1).with_outliers(0.02);
    for (const Combo& co : combos) {
        Cases::single(rr::CaseSpec("tex", std::string("filter_") + co.name).tolerance(mip_tol), [co](Ctx& c) {
            c.clear(Dx::kColor(90, 120, 200, 255));
            Com<IDirect3DTexture9> t = level_texture(c, 64);
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_MINFILTER, co.min);
            c.ss(0, D3DSAMP_MAGFILTER, co.mag);
            c.ss(0, D3DSAMP_MIPFILTER, co.mip);
            ground(c);
        });
    }
    Cases::single(rr::CaseSpec("tex", "mip_lod_bias_positive").tolerance(mip_tol), [](Ctx& c) {
        c.clear(Dx::kColor(90, 120, 200, 255));
        Com<IDirect3DTexture9> t = level_texture(c, 64);
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MIPMAPLODBIAS, Dx::fbits(1.5f));
        ground(c);
    });
    Cases::single(rr::CaseSpec("tex", "mip_lod_bias_negative").tolerance(mip_tol), [](Ctx& c) {
        c.clear(Dx::kColor(90, 120, 200, 255));
        Com<IDirect3DTexture9> t = level_texture(c, 64);
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MIPMAPLODBIAS, Dx::fbits(-1.0f));
        ground(c);
    });
    Cases::single(rr::CaseSpec("tex", "mip_max_level").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(90, 120, 200, 255));
        Com<IDirect3DTexture9> t = level_texture(c, 64);
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MAXMIPLEVEL, 3);
        ground(c, 1.0f);
    });
    Cases::single(rr::CaseSpec("tex", "mip_autogen").tolerance(rr::Tolerance::raster(16, 1).with_outliers(0.03)), [](Ctx& c) {
        c.clear(Dx::kColor(90, 120, 200, 255));
        Com<IDirect3DTexture9> t = c.texture(64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(16, 0xFFF0F0F0, 0xFF2040C0));
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        ground(c);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0; }, "automatic mip generation"));
    const int aniso[3] = {1, 4, 16};
    for (int a : aniso) {
        Cases::single(rr::CaseSpec("tex", "aniso_" + std::to_string(a)).tolerance(rr::Tolerance::psnr(31.0)), [a](Ctx& c) {
            c.clear(Dx::kColor(90, 120, 200, 255));
            Com<IDirect3DTexture9> t = c.texture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(8, 0xFFF0F0F0, 0xFF2040C0));
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            c.ss(0, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(a));
            ground(c);
        }, {}, Needs::caps([a](const D3DCAPS9& caps) { return (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0 && caps.MaxAnisotropy >= static_cast<DWORD>(a); }, "anisotropic filtering"));
    }
}

void address_cases() {
    struct Mode {
        const char* name;
        DWORD mode;
        DWORD cap;
    };
    const Mode modes[] = {{"wrap", D3DTADDRESS_WRAP, D3DPTADDRESSCAPS_WRAP}, {"mirror", D3DTADDRESS_MIRROR, D3DPTADDRESSCAPS_MIRROR},
                          {"clamp", D3DTADDRESS_CLAMP, D3DPTADDRESSCAPS_CLAMP}, {"border", D3DTADDRESS_BORDER, D3DPTADDRESSCAPS_BORDER},
                          {"mirroronce", D3DTADDRESS_MIRRORONCE, D3DPTADDRESSCAPS_MIRRORONCE}};
    for (const Mode& m : modes) {
        Cases::single(rr::CaseSpec("tex", std::string("address_") + m.name).tolerance(rr::Tolerance::blend(3)), [m](Ctx& c) {
            c.clear(Dx::kColor(20, 20, 20, 255));
            Com<IDirect3DTexture9> t = c.texture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
            c.tex(0, t.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_ADDRESSU, m.mode);
            c.ss(0, D3DSAMP_ADDRESSV, m.mode);
            c.ss(0, D3DSAMP_BORDERCOLOR, 0xFFFF00FF);
            tex_quad(c, 4, 4, 124, 124, -1.0f, -1.0f, 2.0f, 2.0f);
        }, {}, Needs::caps([m](const D3DCAPS9& caps) { return (caps.TextureAddressCaps & m.cap) != 0; }, std::string("address mode ") + m.name));
    }
    Cases::single(rr::CaseSpec("tex", "address_mixed_uv_border_color").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 20, 255));
        Com<IDirect3DTexture9> t = c.texture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(4, 0xFFFFFFFF, 0xFF404040));
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR);
        c.ss(0, D3DSAMP_BORDERCOLOR, 0xFF30D060);
        tex_quad(c, 4, 4, 124, 124, -0.5f, -0.5f, 1.5f, 2.5f);
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER) != 0 && (caps.TextureAddressCaps & D3DPTADDRESSCAPS_INDEPENDENTUV) != 0; }, "independent uv address modes"));
    Cases::single(rr::CaseSpec("tex", "srgb_texture_read").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(0, 0, 0, 255));
        Com<IDirect3DTexture9> t = c.texture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::rainbow());
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_SRGBTEXTURE, TRUE);
        tex_quad(c, 4, 4, 124, 124);
    }, {}, Needs::format(D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, "sRGB texture reads"));
}

Com<IDirect3DCubeTexture9> cube_texture(Ctx& c, UINT size) {
    Com<IDirect3DCubeTexture9> t;
    c.chk(c.d->CreateCubeTexture(size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, t.put(), nullptr), "CreateCubeTexture");
    if (!t) {
        return t;
    }
    const DWORD base[6] = {0xFFE04040, 0xFF40E0E0, 0xFF40E040, 0xFFE040E0, 0xFF4040E0, 0xFFE0E040};
    for (int face = 0; face < 6; ++face) {
        D3DLOCKED_RECT locked{};
        c.chk(t->LockRect(static_cast<D3DCUBEMAP_FACES>(face), 0, &locked, nullptr, 0), "LockRect cube");
        if (locked.pBits == nullptr) {
            return t;
        }
        for (UINT y = 0; y < size; ++y) {
            DWORD* row = reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch);
            for (UINT x = 0; x < size; ++x) {
                const bool cell = ((x / 8) + (y / 8)) & 1u;
                const unsigned shade = 140 + 115 * x / size;
                const DWORD b = base[face];
                const unsigned r = ((b >> 16) & 255) * shade / 255;
                const unsigned g = ((b >> 8) & 255) * shade / 255;
                const unsigned bl = (b & 255) * shade / 255;
                row[x] = cell ? Dx::kColor(r, g, bl) : Dx::kColor(r / 2, g / 2, bl / 2);
            }
        }
        t->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), 0);
    }
    return t;
}

void cube_volume_cases() {
    Cases::single(rr::CaseSpec("tex", "cube_reflection_texgen").tolerance(rr::Tolerance::filter(6, 0.004)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DCubeTexture9> t = cube_texture(c, 32);
        c.tex(0, t.get());
        sampler_linear(c);
        c.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
        c.camera(4.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.xf(D3DTS_WORLD, Xf::rot_y(0.4f));
        c.fvf(kVtxFvf);
        c.draw_mesh(Geo::sphere(28, 18, 1.3f, 0xFFFFFFFF));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0; }, "cube textures"));
    Cases::single(rr::CaseSpec("tex", "cube_sky_direct_coords").tolerance(rr::Tolerance::filter(6, 0.004)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DCubeTexture9> t = cube_texture(c, 32);
        c.tex(0, t.get());
        sampler_linear(c);
        struct V { float x, y, z, u, v, w; };
        std::vector<V> v;
        const float s = 0.9f;
        const float pos[4][2] = {{-s, s}, {s, s}, {-s, -s}, {s, -s}};
        const float dir[4][3] = {{-1, 1, 1}, {1, 1, 1}, {-1, -1, 1}, {1, -1, 1}};
        for (int i = 0; i < 4; ++i) {
            v.push_back({pos[i][0], pos[i][1], 0.5f, dir[i][0], dir[i][1], dir[i][2]});
        }
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEX1);
        c.draw(D3DPT_TRIANGLESTRIP, 2, v.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0; }, "cube textures"));
    Cases::single(rr::CaseSpec("tex", "volume_slice").tolerance(rr::Tolerance::filter(6, 0.004)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DVolumeTexture9> t;
        c.chk(c.d->CreateVolumeTexture(16, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, t.put(), nullptr), "CreateVolumeTexture");
        if (!t) {
            return;
        }
        D3DLOCKED_BOX box{};
        c.chk(t->LockBox(0, &box, nullptr, 0), "LockBox");
        if (box.pBits == nullptr) {
            return;
        }
        for (UINT z = 0; z < 16; ++z) {
            for (UINT y = 0; y < 16; ++y) {
                DWORD* row = reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(box.pBits) + static_cast<std::size_t>(z) * box.SlicePitch + static_cast<std::size_t>(y) * box.RowPitch);
                for (UINT x = 0; x < 16; ++x) {
                    row[x] = Dx::kColor(x * 17, y * 17, z * 17);
                }
            }
        }
        t->UnlockBox(0);
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
        struct V { float x, y, z, u, v, w; };
        const float s = 0.9f;
        const V q[4] = {{-s, s, 0.5f, 0, 0, 0.1f}, {s, s, 0.5f, 1, 0, 0.5f}, {-s, -s, 0.5f, 0, 1, 0.5f}, {s, -s, 0.5f, 1, 1, 0.9f}};
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEX1);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0; }, "volume textures"));
}

Com<IDirect3DSurface9> sysmem_surface(Ctx& c, UINT w, UINT h, D3DFORMAT format, const Fill& fill) {
    Com<IDirect3DSurface9> s;
    c.chk(c.d->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_SYSTEMMEM, s.put(), nullptr), "CreateOffscreenPlainSurface");
    if (!s) {
        return s;
    }
    D3DLOCKED_RECT locked{};
    c.chk(s->LockRect(&locked, nullptr, 0), "LockRect");
    if (locked.pBits == nullptr) {
        return {};
    }
    const UINT bpt = Texels::bytes_per_texel(format);
    for (UINT y = 0; y < h; ++y) {
        for (UINT x = 0; x < w; ++x) {
            Texels::pack(format, fill(x, y, w, h), static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch + x * bpt);
        }
    }
    s->UnlockRect();
    return s;
}

Com<IDirect3DSurface9> device_surface(Ctx& c, UINT w, UINT h, D3DFORMAT format, const Fill& fill) {
    Com<IDirect3DSurface9> sys = sysmem_surface(c, w, h, format, fill);
    Com<IDirect3DSurface9> dev;
    c.chk(c.d->CreateOffscreenPlainSurface(w, h, format, D3DPOOL_DEFAULT, dev.put(), nullptr), "CreateOffscreenPlainSurface default");
    if (sys && dev) {
        c.chk(c.d->UpdateSurface(sys.get(), nullptr, dev.get(), nullptr), "UpdateSurface");
    }
    return dev;
}

void transfer_cases() {
    Cases::single(rr::CaseSpec("tex", "rendertarget_texture_two_pass"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 30, 255));
        Com<IDirect3DTexture9> rt;
        c.chk(c.d->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, rt.put(), nullptr), "CreateTexture rt");
        if (!rt) {
            return;
        }
        Com<IDirect3DSurface9> surface;
        rt->GetSurfaceLevel(0, surface.put());
        c.end();
        c.chk(c.d->SetRenderTarget(0, surface.get()), "SetRenderTarget");
        c.chk(c.d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        c.begin();
        c.clear(Dx::kColor(240, 240, 200, 255), D3DCLEAR_TARGET);
        c.quad(4, 4, 60, 60, 0.5f, 0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFFFFFF40);
        c.quad(20, 20, 44, 44, 0.4f, 0xFF000000);
        c.end();
        c.chk(c.d->SetRenderTarget(0, c.target.surface.get()), "SetRenderTarget");
        c.chk(c.d->SetDepthStencilSurface(c.depth.surface.get()), "SetDepthStencilSurface");
        c.begin();
        c.tex(0, rt.get());
        sampler_linear(c);
        tex_quad(c, 10, 10, 118, 118);
        c.tex(0, nullptr);
    });
    Cases::single(rr::CaseSpec("tex", "pool_default_updatetexture").tolerance(rr::Tolerance::raster(10, 1).with_outliers(0.02)), [](Ctx& c) {
        c.clear(Dx::kColor(90, 120, 200, 255));
        Com<IDirect3DTexture9> t = level_texture(c, 64, D3DPOOL_DEFAULT);
        c.tex(0, t.get());
        sampler_linear(c);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        ground(c);
    });
    Cases::single(rr::CaseSpec("tex", "update_surface_rect_to_level").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DTexture9> t;
        c.chk(c.d->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, t.put(), nullptr), "CreateTexture");
        Com<IDirect3DSurface9> source = sysmem_surface(c, 64, 64, D3DFMT_A8R8G8B8, Texels::rainbow());
        Com<IDirect3DSurface9> fillsrc = sysmem_surface(c, 64, 64, D3DFMT_A8R8G8B8, Texels::solid(0xFF303030));
        if (!t || !source || !fillsrc) {
            return;
        }
        Com<IDirect3DSurface9> level;
        t->GetSurfaceLevel(0, level.put());
        c.chk(c.d->UpdateSurface(fillsrc.get(), nullptr, level.get(), nullptr), "UpdateSurface base");
        const RECT rect{8, 4, 40, 36};
        const POINT dest{20, 24};
        c.chk(c.d->UpdateSurface(source.get(), &rect, level.get(), &dest), "UpdateSurface rect");
        c.tex(0, t.get());
        sampler_linear(c, 0, false);
        tex_quad(c, 8, 8, 120, 120);
    });
    Cases::single(rr::CaseSpec("tex", "stretchrect_point_magnify"), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DSurface9> src = device_surface(c, 16, 16, D3DFMT_A8R8G8B8, Texels::checker(4, 0xFFF0D030, 0xFF3070D0));
        if (!src) {
            return;
        }
        const RECT s{2, 2, 14, 14};
        const RECT d{10, 20, 118, 100};
        c.chk(c.d->StretchRect(src.get(), &s, c.target.surface.get(), &d, D3DTEXF_POINT), "StretchRect");
    });
    Cases::single(rr::CaseSpec("tex", "stretchrect_linear_minify").tolerance(rr::Tolerance::blend(4).with_outliers(0.005)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DSurface9> src = device_surface(c, 96, 96, D3DFMT_A8R8G8B8, Texels::checker(24, 0xFFF0F0F0, 0xFF203060));
        if (!src) {
            return;
        }
        const RECT d{16, 16, 80, 100};
        c.chk(c.d->StretchRect(src.get(), nullptr, c.target.surface.get(), &d, D3DTEXF_LINEAR), "StretchRect");
    });
    Cases::single(rr::CaseSpec("tex", "stretchrect_format_x8r8g8b8_to_target").tolerance(rr::Tolerance::blend(2)), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        Com<IDirect3DSurface9> src = device_surface(c, 32, 32, D3DFMT_X8R8G8B8, Texels::rainbow());
        if (!src) {
            return;
        }
        c.chk(c.d->StretchRect(src.get(), nullptr, c.target.surface.get(), nullptr, D3DTEXF_LINEAR), "StretchRect");
    });
    Cases::single(rr::CaseSpec("tex", "color_fill_rects"), [](Ctx& c) {
        c.clear(Dx::kColor(20, 20, 40, 255));
        const RECT a{4, 4, 60, 40};
        const RECT b{30, 30, 100, 110};
        c.chk(c.d->ColorFill(c.target.surface.get(), &a, Dx::kColor(240, 80, 40)), "ColorFill");
        c.chk(c.d->ColorFill(c.target.surface.get(), &b, Dx::kColor(40, 200, 120, 128)), "ColorFill");
    });
}

void dynamic_cases() {
    Cases::frames(rr::CaseSpec("tex", "dynamic_texture_discard").frames(6, {5}).tolerance(rr::Tolerance::blend(2)), [] {
        auto texture = std::make_shared<Com<IDirect3DTexture9>>();
        return FrameBody([texture](Ctx& c, unsigned f) {
            if (f == 0) {
                c.chk(c.d->CreateTexture(32, 32, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture->put(), nullptr), "CreateTexture dynamic");
            }
            if (!*texture) {
                return;
            }
            c.fill_level(texture->get(), 0, D3DFMT_A8R8G8B8, [f](UINT x, UINT y, UINT w, UINT h) {
                const bool stripe = ((x + f * 5) / 4 + y / 8) & 1u;
                return stripe ? Texels::argb(255, 240, 60 + f * 30, 40) : Texels::argb(255, 20 + f * 10, 40, 160 + (y * 80 / h));
            });
            c.clear(Dx::kColor(10, 10, 10, 255));
            c.tex(0, texture->get());
            sampler_linear(c, 0, false);
            tex_quad(c, 8, 8, 120, 120);
        });
    });
}

const Registrar kFormats(format_cases);
const Registrar kMips(mip_cases);
const Registrar kAddress(address_cases);
const Registrar kCube(cube_volume_cases);
const Registrar kTransfer(transfer_cases);
const Registrar kDynamic(dynamic_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
