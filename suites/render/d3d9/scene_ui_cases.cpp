#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

const Layout kScene{D3DFMT_A8R8G8B8, D3DFMT_D24S8, 256};

rr::CaseSpec scene(const char* name, unsigned frames, std::vector<unsigned> captured, rr::Tolerance tol = rr::Tolerance::blend(3)) {
    return rr::CaseSpec("scene", name).size(256, 256).frames(frames, std::move(captured)).tolerance(std::move(tol));
}

void sprite_quad(Ctx& c, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, DWORD color = 0xFFFFFFFF) {
    const std::vector<TlVtx> q = Geo::tl_quad(x0, y0, x1, y1, color, u0, v0, u1, v1);
    c.fvf(kTlFvf);
    c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
}

void blend_over(Ctx& c) {
    c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
    c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

void linear_tex(Ctx& c, DWORD s, DWORD address = D3DTADDRESS_CLAMP, bool smooth = true) {
    c.ss(s, D3DSAMP_MINFILTER, smooth ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    c.ss(s, D3DSAMP_MAGFILTER, smooth ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    c.ss(s, D3DSAMP_ADDRESSU, address);
    c.ss(s, D3DSAMP_ADDRESSV, address);
}

const std::uint16_t kGlyphs[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};

Fill glyph_atlas() {
    return [](UINT x, UINT y, UINT, UINT) {
        const UINT cell = x / 16;
        const UINT lx = x % 16;
        const UINT ly = y % 16;
        if (y >= 16 || cell >= 10 || lx < 2 || lx >= 14 || ly < 1 || ly >= 16) {
            return Texels::argb(0, 255, 255, 255);
        }
        const UINT gx = (lx - 2) / 4;
        const UINT gy = (ly - 1) / 3;
        if (gx >= 3 || gy >= 5) {
            return Texels::argb(0, 255, 255, 255);
        }
        const bool on = (kGlyphs[cell] >> (14 - (gy * 3 + gx))) & 1u;
        return on ? Texels::argb(255, 255, 255, 255) : Texels::argb(0, 255, 255, 255);
    };
}

void text(Ctx& c, float x, float y, float scale, const std::string& digits, DWORD color) {
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const unsigned d = static_cast<unsigned>(digits[i] - '0');
        const float u0 = static_cast<float>(d) / 16.0f;
        sprite_quad(c, x + static_cast<float>(i) * 12.0f * scale, y, x + static_cast<float>(i) * 12.0f * scale + 14.0f * scale, y + 14.0f * scale, u0, 0, u0 + 1.0f / 16.0f, 1.0f, color);
    }
}

void ui_scenes() {
    Cases::frames(scene("particles_point_sprites_ring_vb", 8, {0, 3, 7}, rr::Tolerance::raster(6, 1).with_outliers(0.01)), [] {
        struct State {
            Com<IDirect3DVertexBuffer9> ring;
            UINT cursor = 0;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            struct P { float x, y, z; float size; DWORD color; };
            constexpr UINT kPer = 64;
            constexpr UINT kSlots = 4;
            if (f == 0) {
                c.chk(c.d->CreateVertexBuffer(kPer * kSlots * sizeof(P), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY | D3DUSAGE_POINTS, D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, st->ring.put(), nullptr), "CreateVertexBuffer");
            }
            if (!st->ring) {
                return;
            }
            std::vector<P> v;
            std::uint32_t seed = 99;
            for (UINT i = 0; i < kPer; ++i) {
                const float t = rr::Pattern::unit_random(seed) * 2.0f + 0.15f * static_cast<float>(f);
                const float vx = (rr::Pattern::unit_random(seed) - 0.5f) * 3.0f;
                const float life = std::fmod(t, 2.0f);
                const float x = vx * life;
                const float y = 3.0f * life - 2.2f * life * life - 0.8f;
                const unsigned a = static_cast<unsigned>(255.0f * (1.0f - life / 2.0f));
                v.push_back({x, y, 0.0f, 5.0f + 20.0f * (1.0f - life / 2.0f), Dx::kColor(255, 160 + static_cast<unsigned>(rr::Pattern::unit_random(seed) * 90), 60, a)});
            }
            const UINT slot = f % kSlots;
            void* data = nullptr;
            c.chk(st->ring->Lock(slot * kPer * sizeof(P), kPer * sizeof(P), &data, slot == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE), "Lock");
            if (data == nullptr) {
                return;
            }
            std::memcpy(data, v.data(), v.size() * sizeof(P));
            st->ring->Unlock();
            c.clear(Dx::kColor(10, 8, 20));
            c.camera(4.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_POINTSPRITEENABLE, TRUE);
            c.rs(D3DRS_POINTSCALEENABLE, FALSE);
            c.rsf(D3DRS_POINTSIZE_MAX, 64.0f);
            c.tex(0, nullptr);
            c.fvf(D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE);
            c.chk(c.d->SetStreamSource(0, st->ring.get(), 0, sizeof(P)), "SetStreamSource");
            c.chk(c.d->DrawPrimitive(D3DPT_POINTLIST, slot * kPer, kPer), "DrawPrimitive");
            c.tex(0, nullptr);

        });
    }, kScene);

    Cases::frames(scene("hud_text_pretransformed_sprites", 4, {0, 3}), [] {
        struct State {
            Com<IDirect3DTexture9> atlas;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->atlas = c.texture(256, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, glyph_atlas());
            }
            if (!st->atlas) {
                return;
            }
            c.clear(Dx::kColor(40, 70, 40));
            c.quad(0, 0, 256, 128, 0.9f, 0xFF6090D0, 0xFF6090D0, 0xFF30406A, 0xFF30406A);
            blend_over(c);
            c.quad(8, 8, 150, 40, 0.5f, 0x90000000);
            c.tex(0, st->atlas.get());
            linear_tex(c, 0, D3DTADDRESS_CLAMP, false);
            char buf[16];
            std::snprintf(buf, sizeof buf, "%05u", 1234u * (f + 1));
            text(c, 17, 17, 1.0f, buf, 0xFF000000);
            text(c, 16, 16, 1.0f, buf, 0xFFFFE060);
            text(c, 16, 60, 2.0f, "0123456789", 0xFFFFFFFF);
            text(c, 16, 120 + static_cast<float>(f) * 4.0f, 3.0f, "42", 0xFF60FF60);
            c.tex(0, nullptr);
        });
    }, kScene);

    Cases::frames(scene("wpf_like_ui_composition", 6, {0, 2, 5}, rr::Tolerance::blend(3)), [] {
        struct State {
            Com<IDirect3DTexture9> card, shadow, glyphs;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->card = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                    const float cx = std::fabs(static_cast<float>(x) - 31.5f) - 24.0f;
                    const float cy = std::fabs(static_cast<float>(y) - 31.5f) - 24.0f;
                    const float d = std::sqrt(std::max(cx, 0.0f) * std::max(cx, 0.0f) + std::max(cy, 0.0f) * std::max(cy, 0.0f)) - 7.0f;
                    const float a = std::clamp(0.5f - d, 0.0f, 1.0f);
                    const unsigned top = 240 - y * 60 / h;
                    const unsigned alpha = static_cast<unsigned>(a * 255.0f * 0.92f);
                    (void)w;
                    return Texels::argb(alpha, top * alpha / 255, top * alpha / 255 * 98 / 100, (top - 20) * alpha / 255);
                });
                st->shadow = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
                    const float dx = (static_cast<float>(x) - 31.5f) / 32.0f;
                    const float dy = (static_cast<float>(y) - 31.5f) / 32.0f;
                    const float a = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                    return Texels::argb(static_cast<unsigned>(a * a * 140.0f), 0, 0, 0);
                });
                st->glyphs = c.texture(256, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, glyph_atlas());
            }
            if (!st->card || !st->shadow || !st->glyphs) {
                return;
            }
            c.clear(Dx::kColor(32, 32, 36));
            c.quad(0, 0, 256, 256, 0.9f, 0xFF3A5A9A, 0xFF9A3A6A, 0xFF2A2A3A, 0xFF3A9A6A);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            linear_tex(c, 0);
            for (int i = 0; i < 12; ++i) {
                const float x = 14.0f + 58.0f * static_cast<float>(i % 4);
                const float y = 14.0f + 58.0f * static_cast<float>(i / 4) + 6.0f * std::sin(0.9f * static_cast<float>(f) + static_cast<float>(i));
                c.tex(0, st->shadow.get());
                sprite_quad(c, x - 6, y - 2, x + 62, y + 70, 0, 0, 1, 1, 0xFFFFFFFF);
                c.tex(0, st->card.get());
                const float s = 1.0f + 0.04f * std::sin(static_cast<float>(f) + static_cast<float>(i));
                sprite_quad(c, x, y, x + 56 * s, y + 56 * s, 0, 0, 1, 1, i % 3 == 0 ? 0xFFFFFFFF : i % 3 == 1 ? 0xFFFFE0E0 : 0xFFE0FFE0);
            }
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.tex(0, st->glyphs.get());
            linear_tex(c, 0, D3DTADDRESS_CLAMP, false);
            text(c, 20, 224, 1.0f, "0123", 0xFFFFFFFF);
            c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            c.tex(0, nullptr);
        });
    }, kScene);

    Cases::frames(scene("sprite_sheet_alpha_test_animation", 8, {0, 4, 7}), [] {
        struct State {
            Com<IDirect3DTexture9> sheet;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->sheet = c.texture(128, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
                    const UINT frame = x / 32;
                    const float dx = static_cast<float>(x % 32) - 15.5f;
                    const float dy = static_cast<float>(y) - 15.5f;
                    const float r = 12.0f - 2.0f * static_cast<float>(frame % 2);
                    const bool body = dx * dx + dy * dy < r * r;
                    const bool eye = (dx - 4.0f) * (dx - 4.0f) + (dy + 4.0f) * (dy + 4.0f) < 6.0f;
                    if (!body) {
                        return Texels::argb(0, 0, 0, 0);
                    }
                    return eye ? Texels::argb(255, 20, 20, 20) : Texels::argb(255, 240 - frame * 30, 200, 40 + frame * 50);
                });
            }
            if (!st->sheet) {
                return;
            }
            c.clear(Dx::kColor(60, 100, 160));
            c.rs(D3DRS_ALPHATESTENABLE, TRUE);
            c.rs(D3DRS_ALPHAREF, 0x40);
            c.rs(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            c.tex(0, st->sheet.get());
            linear_tex(c, 0, D3DTADDRESS_CLAMP, false);
            for (int i = 0; i < 6; ++i) {
                const unsigned frame = (f + static_cast<unsigned>(i)) % 4;
                const float u0 = static_cast<float>(frame) * 0.25f;
                const float x = 20.0f + 36.0f * static_cast<float>(i) + 10.0f * static_cast<float>(f);
                const float y = 40.0f + 30.0f * static_cast<float>(i % 3) + 14.0f * std::sin(0.8f * static_cast<float>(f + static_cast<unsigned>(i)));
                sprite_quad(c, x, y, x + 64, y + 64, u0, 0, u0 + 0.25f, 1);
            }
            c.rs(D3DRS_ALPHATESTENABLE, FALSE);
            c.tex(0, nullptr);
        });
    }, kScene);

    Cases::frames(scene("tile_map_scroll_wrap", 6, {0, 3, 5}), [] {
        struct State {
            Com<IDirect3DTexture9> tiles;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->tiles = c.texture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT, UINT) {
                    const UINT tile = (x / 16) + 4 * (y / 16);
                    const bool edge = (x % 16 == 0) || (y % 16 == 0);
                    const unsigned r = 60 + (tile * 37) % 160;
                    const unsigned g = 70 + (tile * 59) % 150;
                    const unsigned b = 80 + (tile * 83) % 140;
                    return edge ? Texels::argb(255, r / 2, g / 2, b / 2) : Texels::argb(255, r, g, b);
                });
            }
            if (!st->tiles) {
                return;
            }
            c.clear(Dx::kColor(0, 0, 0));
            c.tex(0, st->tiles.get());
            linear_tex(c, 0, D3DTADDRESS_WRAP, false);
            const float scroll = 0.17f * static_cast<float>(f);
            std::vector<TlVtx> v;
            for (int ty = 0; ty < 8; ++ty) {
                for (int tx = 0; tx < 8; ++tx) {
                    const unsigned tile = (static_cast<unsigned>(tx) * 5 + static_cast<unsigned>(ty) * 3 + (static_cast<unsigned>(tx * ty) % 4)) % 16;
                    const float u0 = static_cast<float>(tile % 4) * 0.25f;
                    const float v0 = static_cast<float>(tile / 4) * 0.25f;
                    const float x = 32.0f * static_cast<float>(tx) - 32.0f * std::fmod(scroll, 1.0f);
                    const std::vector<TlVtx> q = Geo::tl_quad(x, 32.0f * static_cast<float>(ty), x + 32.0f, 32.0f * static_cast<float>(ty) + 32.0f, 0xFFFFFFFF, u0, v0, u0 + 0.25f, v0 + 0.25f);
                    v.insert(v.end(), q.begin(), q.end());
                }
            }
            c.fvf(kTlFvf);
            c.draw(D3DPT_TRIANGLELIST, static_cast<UINT>(v.size() / 3), v.data(), sizeof(TlVtx));
            c.tex(0, nullptr);
        });
    }, kScene);

    Cases::frames(scene("scrolling_list_scissor_clip", 5, {0, 2, 4}), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            c.clear(Dx::kColor(30, 30, 40));
            c.quad(0, 0, 256, 256, 0.9f, 0xFF404060, 0xFF404060, 0xFF202030, 0xFF202030);
            const RECT view{32, 40, 224, 216};
            c.chk(c.d->SetScissorRect(&view), "SetScissorRect");
            c.rs(D3DRS_SCISSORTESTENABLE, TRUE);
            c.quad(32, 40, 224, 216, 0.5f, 0xFF101018);
            for (int i = 0; i < 12; ++i) {
                const float y = 36.0f + 30.0f * static_cast<float>(i) - 17.0f * static_cast<float>(f);
                const DWORD base = i % 2 ? 0xFF5078B0 : 0xFF6A5AA0;
                c.quad(36, y, 220, y + 26, 0.5f, base, base + 0x00202020, base - 0x00101010, base);
                c.quad(42, y + 8, 42 + 12.0f * static_cast<float>(1 + i % 5), y + 18, 0.4f, 0xFFF0F0F0);
            }
            c.rs(D3DRS_SCISSORTESTENABLE, FALSE);
            c.quad(30, 38, 226, 40, 0.3f, 0xFFFFFFFF);
            c.quad(30, 216, 226, 218, 0.3f, 0xFFFFFFFF);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return (caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0; }, "scissor test"));

    Cases::frames(scene("radar_sweep_lines_antialiased", 4, {0, 3}, rr::Tolerance::raster(40, 2).with_outliers(0.02)), [] {
        return FrameBody([](Ctx& c, unsigned f) {
            struct P { float x, y, z, rhw; DWORD color; };
            c.clear(Dx::kColor(0, 16, 8));
            c.rs(D3DRS_ANTIALIASEDLINEENABLE, TRUE);
            std::vector<P> lines;
            for (int ring = 1; ring <= 3; ++ring) {
                const float r = 36.0f * static_cast<float>(ring);
                for (int k = 0; k < 48; ++k) {
                    const float a0 = 6.2831853f * static_cast<float>(k) / 48.0f;
                    const float a1 = 6.2831853f * static_cast<float>(k + 1) / 48.0f;
                    lines.push_back({128 + r * std::cos(a0), 128 + r * std::sin(a0), 0.5f, 1, 0xFF208040});
                    lines.push_back({128 + r * std::cos(a1), 128 + r * std::sin(a1), 0.5f, 1, 0xFF208040});
                }
            }
            const float sweep = 0.6f * static_cast<float>(f);
            for (int k = 0; k < 8; ++k) {
                const float a = sweep - 0.06f * static_cast<float>(k);
                lines.push_back({128, 128, 0.5f, 1, Dx::kColor(60, 255 - 28 * static_cast<unsigned>(k), 90)});
                lines.push_back({128 + 108 * std::cos(a), 128 + 108 * std::sin(a), 0.5f, 1, Dx::kColor(60, 255 - 28 * static_cast<unsigned>(k), 90)});
            }
            c.fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.draw(D3DPT_LINELIST, static_cast<UINT>(lines.size() / 2), lines.data(), sizeof(P));
        });
    }, kScene);

    Cases::frames(scene("video_yuv_to_rgb_shader_blit", 6, {0, 2, 5}, rr::Tolerance::blend(4)), [] {
        struct State {
            Com<IDirect3DTexture9> y, u, v;
            Com<IDirect3DPixelShader9> ps;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                st->y = c.texture(64, 64, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, nullptr);
                st->u = c.texture(32, 32, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, nullptr);
                st->v = c.texture(32, 32, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, nullptr);
                st->ps = c.ps(
                    "sampler2D sy : register(s0);\nsampler2D su : register(s1);\nsampler2D sv : register(s2);\n"
                    "float4 main(float2 uv : TEXCOORD0) : COLOR { float y = (tex2D(sy, uv).r - 0.0625) * 1.164; float u = tex2D(su, uv).r - 0.5; float v = tex2D(sv, uv).r - 0.5;\n"
                    "return float4(saturate(y + 1.596 * v), saturate(y - 0.392 * u - 0.813 * v), saturate(y + 2.017 * u), 1.0); }\n", "ps_2_0");
            }
            if (!st->y || !st->u || !st->v || !st->ps) {
                return;
            }
            const float t = static_cast<float>(f);
            (void)c.fill_level(st->y.get(), 0, D3DFMT_L8, [t](UINT x, UINT y, UINT, UINT) {
                const float dx = static_cast<float>(x) - 20.0f - 5.0f * t;
                const float dy = static_cast<float>(y) - 32.0f;
                const bool ball = dx * dx + dy * dy < 150.0f;
                return Texels::argb(255, ball ? 200 : 40 + (x + y) % 64, 0, 0);
            });
            (void)c.fill_level(st->u.get(), 0, D3DFMT_L8, [t](UINT x, UINT y, UINT, UINT) { return Texels::argb(255, 64 + (x * 4 + static_cast<unsigned>(t) * 10) % 128, 0, 0); });
            (void)c.fill_level(st->v.get(), 0, D3DFMT_L8, [t](UINT x, UINT y, UINT, UINT) { return Texels::argb(255, 200 - (y * 4 + static_cast<unsigned>(t) * 6) % 128, 0, 0); });
            c.clear(Dx::kColor(0, 0, 0));
            c.tex(0, st->y.get());
            c.tex(1, st->u.get());
            c.tex(2, st->v.get());
            for (DWORD s = 0; s < 3; ++s) {
                linear_tex(c, s);
            }
            c.chk(c.d->SetPixelShader(st->ps.get()), "SetPixelShader");
            sprite_quad(c, 16, 16, 240, 240, 0, 0, 1, 1);
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
            c.tex(0, nullptr);
            c.tex(1, nullptr);
            c.tex(2, nullptr);
        });
    }, kScene, Needs::all({Needs::shader(2, 2), Needs::format(0, D3DRTYPE_TEXTURE, D3DFMT_L8, "L8 textures")}));

    Cases::frames(scene("texture_streaming_update_texture", 6, {0, 3, 5}), [] {
        struct State {
            Com<IDirect3DTexture9> sys, dev;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                c.chk(c.d->CreateTexture(128, 128, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, st->sys.put(), nullptr), "CreateTexture system");
                c.chk(c.d->CreateTexture(128, 128, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, st->dev.put(), nullptr), "CreateTexture default");
            }
            if (!st->sys || !st->dev) {
                return;
            }
            for (UINT level = 0; level < st->sys->GetLevelCount(); ++level) {
                (void)c.fill_level(st->sys.get(), level, D3DFMT_A8R8G8B8, [f, level](UINT x, UINT y, UINT w, UINT h) {
                    const bool band = ((x + y + f * 9 * w / 128) / std::max(1u, 8u >> std::min(level, 3u))) & 1u;
                    return band ? Texels::argb(255, 250 - level * 20, 200, 60 + f * 30) : Texels::argb(255, 30 + level * 20, 60, 180 - f * 15 + (y * 40 / h));
                });
            }
            c.chk(c.d->UpdateTexture(st->sys.get(), st->dev.get()), "UpdateTexture");
            c.clear(Dx::kColor(10, 10, 10));
            c.tex(0, st->dev.get());
            linear_tex(c, 0, D3DTADDRESS_WRAP);
            c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            sprite_quad(c, 16, 16, 240, 240, 0, 0, 3, 3);
            sprite_quad(c, 160, 160, 200, 200, 0, 0, 1, 1);
            c.tex(0, nullptr);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return (caps.Caps2 & D3DCAPS2_DYNAMICTEXTURES) != 0 || true; }, "textures"));
}

const Registrar kUi(ui_scenes);

}  // namespace

}  // namespace lsw::suites::render::d3d9
