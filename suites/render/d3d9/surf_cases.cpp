#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

void gradient(Ctx& c) {
    c.quad(0, 0, 128, 128, 0.9f, 0xFF204060, 0xFF406020, 0xFF602040, 0xFFC0C040);
}

void clear_cases() {
    Cases::single(rr::CaseSpec("surf", "clear_rects_flags"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 40, 255));
        D3DRECT rects[3] = {{4, 4, 60, 50}, {40, 40, 120, 70}, {10, 90, 100, 124}};
        c.chk(c.d->Clear(3, rects, D3DCLEAR_TARGET, Dx::kColor(230, 120, 40, 255), 1.0f, 0), "Clear rects");
        D3DRECT inner[1] = {{50, 10, 100, 120}};
        c.chk(c.d->Clear(1, inner, D3DCLEAR_TARGET, Dx::kColor(40, 200, 120, 128), 1.0f, 0), "Clear rect");
    });
    Cases::single(rr::CaseSpec("surf", "clear_depth_value_then_test"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 40, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0.5f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        c.quad(0, 0, 64, 128, 0.4f, 0xFF40E080);
        c.quad(64, 0, 128, 128, 0.6f, 0xFFE04080);
        D3DRECT r[1] = {{20, 20, 100, 60}};
        c.chk(c.d->Clear(1, r, D3DCLEAR_ZBUFFER, 0, 0.0f, 0), "Clear z rect");
        c.quad(0, 0, 128, 128, 0.2f, 0xFF4080F0);
    });
    Cases::single(rr::CaseSpec("surf", "clear_stencil_value"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 40, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 0x7F);
        c.rs(D3DRS_STENCILENABLE, TRUE);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        c.rs(D3DRS_STENCILREF, 0x7F);
        c.quad(10, 10, 118, 118, 0.5f, 0xFF60E060);
        D3DRECT r[1] = {{30, 30, 90, 90}};
        c.chk(c.d->Clear(1, r, D3DCLEAR_STENCIL, 0, 1.0f, 0x10), "Clear stencil rect");
        c.quad(0, 0, 128, 128, 0.5f, 0xFFE06060);
    });
    Cases::single(rr::CaseSpec("surf", "depth_stencil_null_rendering"), [](Ctx& c) {
        c.chk(c.d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        c.clear(Dx::kColor(10, 10, 40, 255), D3DCLEAR_TARGET);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.quad(10, 10, 100, 100, 0.8f, 0xFF60E060);
        c.quad(30, 30, 118, 118, 0.2f, 0xFFE06060);
    });
    Cases::single(rr::CaseSpec("surf", "viewport_subrect"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 40, 255));
        D3DVIEWPORT9 vp{20, 30, 80, 70, 0.0f, 1.0f};
        c.chk(c.d->SetViewport(&vp), "SetViewport");
        c.camera(4.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.fvf(kVtxFvf);
        c.draw_mesh(Geo::cube(1.2f, 0xFF90C0F0));
        D3DVIEWPORT9 full{0, 0, 128, 128, 0.0f, 1.0f};
        c.chk(c.d->SetViewport(&full), "SetViewport");
    });
    Cases::single(rr::CaseSpec("surf", "viewport_depth_range"), [](Ctx& c) {
        c.clear(Dx::kColor(10, 10, 40, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        D3DVIEWPORT9 far_range{0, 0, 128, 128, 0.25f, 0.75f};
        c.chk(c.d->SetViewport(&far_range), "SetViewport");
        c.quad(10, 10, 90, 90, 0.6f, 0xFFE06040);
        D3DVIEWPORT9 near_range{0, 0, 128, 128, 0.0f, 0.5f};
        c.chk(c.d->SetViewport(&near_range), "SetViewport");
        c.quad(40, 40, 120, 120, 0.6f, 0xFF40E060);
        D3DVIEWPORT9 full{0, 0, 128, 128, 0.0f, 1.0f};
        c.chk(c.d->SetViewport(&full), "SetViewport");
    });
    Cases::single(rr::CaseSpec("surf", "backbuffer_render_and_read"), [](Ctx& c) {
        Com<IDirect3DSurface9> back;
        c.chk(c.d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put()), "GetBackBuffer");
        if (!back) {
            return;
        }
        D3DSURFACE_DESC desc{};
        back->GetDesc(&desc);
        c.end();
        c.chk(c.d->SetRenderTarget(0, back.get()), "SetRenderTarget");
        c.begin();
        c.clear(Dx::kColor(20, 20, 60, 255), D3DCLEAR_TARGET);
        gradient(c);
        c.quad(30, 30, 100, 100, 0.5f, 0xFFF0A040);
        Surface s;
        s.surface = std::move(back);
        s.format = desc.Format;
        s.width = desc.Width;
        s.height = desc.Height;
        c.snap_surface(s, "");
        c.end();
        c.chk(c.d->SetRenderTarget(0, c.target.surface.get()), "SetRenderTarget");
        c.begin();
    });
}

struct DepthFormat {
    const char* name;
    D3DFORMAT format;
};

bool has_stencil(D3DFORMAT format) { return format == D3DFMT_D24S8 || format == D3DFMT_D24FS8; }

void depth_separation(Ctx& c, D3DFORMAT format) {
    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
    c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
    const bool d16_only = c.broken("depth_16bit") && format != D3DFMT_D16 && format != D3DFMT_D16_LOCKABLE;
    for (unsigned i = 0; i < 6; ++i) {
        const float k = 1000.0f + 9000.0f * static_cast<float>(i);
        const float x0 = 4.0f + 20.0f * static_cast<float>(i);
        const float far_z = d16_only ? k / 65535.0f : (k + 0.4f) / 65535.0f;
        const float near_z = d16_only ? k / 65535.0f : (k + 0.1f) / 65535.0f;
        c.quad(x0, 114, x0 + 18, 126, far_z, 0xFFE04030);
        c.quad(x0, 114, x0 + 18, 126, near_z, 0xFF30E070);
    }
    if (has_stencil(format)) {
        c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
        c.rs(D3DRS_STENCILENABLE, TRUE);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        c.rs(D3DRS_STENCILREF, 0x5A);
        c.rs(D3DRS_STENCILPASS, c.broken("stencil_lost") ? D3DSTENCILOP_KEEP : D3DSTENCILOP_REPLACE);
        c.rs(D3DRS_STENCILWRITEMASK, 0xFF);
        c.rs(D3DRS_COLORWRITEENABLE, 0);
        c.quad(10, 100, 118, 106, 0.5f, 0xFFFFFFFF);
        c.rs(D3DRS_COLORWRITEENABLE, 15);
        c.rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        c.rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
        c.quad(0, 98, 128, 110, 0.5f, 0xFFF0C030);
        c.rs(D3DRS_STENCILENABLE, FALSE);
    }
}

bool snap_locked_depth(Ctx& c, const std::string& label) {
    const bool was = c.in_scene;
    c.end();
    D3DLOCKED_RECT locked{};
    if (!c.depth.surface || FAILED(c.depth.surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        c.fail("the depth surface cannot be locked");
        return false;
    }
    rr::Image image(c.size, c.size, rr::PixelKind::R32Float);
    for (UINT y = 0; y < c.size; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch);
        float* out = reinterpret_cast<float*>(image.row(y));
        for (UINT x = 0; x < c.size; ++x) {
            out[x] = static_cast<float>(c.broken("depth_lock") ? row[x] & ~0xFFu : row[x]) / 65535.0f;
        }
    }
    c.depth.surface->UnlockRect();
    c.pending.add(label, std::move(image));
    if (was) {
        c.begin();
    }
    return true;
}

void depth_cases() {
    const DepthFormat formats[] = {{"d16", D3DFMT_D16}, {"d24x8", D3DFMT_D24X8}, {"d24s8", D3DFMT_D24S8}, {"d32", D3DFMT_D32}, {"d24fs8", D3DFMT_D24FS8}, {"d16_lockable", D3DFMT_D16_LOCKABLE}};
    for (const DepthFormat& f : formats) {
        rr::CaseSpec spec("surf", std::string("depth_format_") + f.name);
        if (f.format == D3DFMT_D16) {
            spec.same_as({"surf.depth_format_d16_lockable"}, "the lockable D16 row draws the same spheres, depth strips and 16 bit depth as this row and only adds the locked depth capture, so the colour image is identical by design; a driver that drops the depth write fails in that capture");
        } else if (f.format == D3DFMT_D16_LOCKABLE) {
            spec.same_as({"surf.depth_format_d16"}, "the plain D16 row draws the same spheres, depth strips and 16 bit depth as this row, so the colour image is identical by design; this row adds the locked depth capture that proves the stored values");
        }
        if (f.format == D3DFMT_D16_LOCKABLE) {
            spec.tolerance_for("depth", rr::Tolerance::floats(2.0f / 65535.0f));
        }
        Cases::single(spec, [f](Ctx& c) {
            c.clear(Dx::kColor(10, 10, 40, 255));
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
            const float eye[3] = {0, 0.5f, -4.0f};
            const float at[3] = {0, 0, 0};
            const float up[3] = {0, 1, 0};
            c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
            c.xf(D3DTS_PROJECTION, Xf::perspective(1.0f, 1.0f, 0.5f, 20.0f));
            c.fvf(kVtxFvf);
            Mesh a = Geo::sphere(20, 14, 1.1f, 0xFFE06050);
            Mesh b = Geo::sphere(20, 14, 1.1f, 0xFF50A0E0);
            c.xf(D3DTS_WORLD, Xf::translate(-0.5f, 0, 0));
            c.draw_mesh(a);
            c.xf(D3DTS_WORLD, Xf::translate(0.5f, 0, -0.3f));
            c.draw_mesh(b);
            c.xf(D3DTS_WORLD, Xf());
            c.xf(D3DTS_VIEW, Xf());
            c.xf(D3DTS_PROJECTION, Xf());
            depth_separation(c, f.format);
            if (f.format == D3DFMT_D16_LOCKABLE) {
                c.snap("");
                (void)snap_locked_depth(c, "depth");
            }
        }, {D3DFMT_A8R8G8B8, f.format, 128}, Needs::format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, f.format, std::string("depth format ") + f.name));
    }
}

struct RtFormat {
    const char* name;
    D3DFORMAT format;
    rr::Tolerance tolerance;
};

void format_cases() {
    const RtFormat formats[] = {{"x8r8g8b8", D3DFMT_X8R8G8B8, rr::Tolerance::blend(2)}, {"a16b16g16r16f", D3DFMT_A16B16G16R16F, rr::Tolerance::floats(5e-3f, 5e-3f)},
                                {"a32b32g32r32f", D3DFMT_A32B32G32R32F, rr::Tolerance::floats(4e-3f, 4e-3f)}, {"r32f", D3DFMT_R32F, rr::Tolerance::floats(4e-3f, 4e-3f)}};
    for (const RtFormat& f : formats) {
        rr::CaseSpec rt_spec = rr::CaseSpec("surf", std::string("rendertarget_format_") + f.name).tolerance(f.tolerance);
        if (f.format == D3DFMT_X8R8G8B8) {
            rt_spec.same_as({"ex.gpu_thread_priority_and_frame_latency_do_not_alter_image"}, "an X8R8G8B8 target whose destination alpha reads one leaves the INVDESTALPHA and DESTALPHA quad invisible, so the picture is the plain lit sphere of the default target row by design; a destination alpha stored in the X channel makes the quad visible");
        }
        Cases::single(rt_spec, [f](Ctx& c) {
            const bool x8 = f.format == D3DFMT_X8R8G8B8;
            c.clear(Dx::kColor(10, 10, 40, x8 ? 0 : 255));
            if (x8) {
                c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
                c.rs(D3DRS_SRCBLEND, c.broken("x8_alpha") ? D3DBLEND_ONE : D3DBLEND_INVDESTALPHA);
                c.rs(D3DRS_DESTBLEND, c.broken("x8_alpha") ? D3DBLEND_ZERO : D3DBLEND_DESTALPHA);
                c.quad(0, 0, 128, 128, 0.5f, 0xFF30E0F0);
                c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            }
            c.camera(4.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.rs(D3DRS_LIGHTING, TRUE);
            c.rs(D3DRS_AMBIENT, 0xFF303030);
            c.material(Dx::material(0.9f, 0.6f, 0.3f, 0.9f, 0.6f, 0.3f));
            c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
            c.fvf(kVtxFvf);
            c.draw_mesh(Geo::sphere(24, 16, 1.3f, 0xFFFFFFFF));
        }, {f.format, D3DFMT_D24S8, 128}, Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, f.format, std::string("render target format ") + f.name));
    }
    Cases::single(rr::CaseSpec("surf", "mrt_per_target_write_mask"), [](Ctx& c) {
        Surface t1;
        std::string error;
        if (!c.kit.make_target(t1, 128, 128, D3DFMT_A8R8G8B8, error)) {
            c.fail(error);
            return;
        }
        c.chk(c.d->SetRenderTarget(1, t1.surface.get()), "SetRenderTarget1");
        c.clear(Dx::kColor(10, 10, 40, 255));
        c.rs(D3DRS_COLORWRITEENABLE, 0x7);
        c.rs(D3DRS_COLORWRITEENABLE1, 0x3);
        Com<IDirect3DPixelShader9> ps = c.ps("struct O { float4 a : COLOR0; float4 b : COLOR1; };\nO main(float4 col : COLOR0) { O o; o.a = col; o.b = float4(1 - col.rgb, 1); return o; }\n", "ps_3_0");
        Com<IDirect3DVertexShader9> vs = c.vs("struct I { float4 p : POSITION; float4 c : COLOR0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\nO main(I i) { O o; o.p = i.p; o.c = i.c; return o; }\n", "vs_3_0");
        if (!ps || !vs) {
            return;
        }
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        struct V { float x, y, z; DWORD color; };
        const V q[4] = {{-0.8f, 0.8f, 0.5f, 0xFFE06040}, {0.8f, 0.8f, 0.5f, 0xFF40E060}, {-0.8f, -0.8f, 0.5f, 0xFF4060E0}, {0.8f, -0.8f, 0.5f, 0xFFE0E040}};
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        c.snap("rt0");
        c.snap_surface(t1, "rt1");
        c.chk(c.d->SetRenderTarget(1, nullptr), "SetRenderTarget1");
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.NumSimultaneousRTs >= 2 && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS) != 0 && caps.PixelShaderVersion >= D3DPS_VERSION(3, 0); }, "independent write masks"));
}

void msaa_cases() {
    const D3DMULTISAMPLE_TYPE kinds[2] = {D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_4_SAMPLES};
    struct Variant {
        const char* name;
        int kind;
        int mode;
    };
    const Variant variants[] = {{"msaa_resolve_stretchrect_2x", 0, 0}, {"msaa_resolve_stretchrect_4x", 1, 0}, {"msaa_sample_mask_4x", 1, 1}, {"msaa_centroid_interpolation_4x", 1, 2}};
    for (const Variant& v : variants) {
        Cases::single(rr::CaseSpec("surf", v.name).tolerance(rr::Tolerance::blend(3).with_outliers(0.004)), [v, kinds](Ctx& c) {
            Com<IDirect3DSurface9> ms;
            Com<IDirect3DSurface9> msd;
            c.chk(c.d->CreateRenderTarget(128, 128, D3DFMT_A8R8G8B8, kinds[v.kind], 0, FALSE, ms.put(), nullptr), "CreateRenderTarget multisampled");
            c.chk(c.d->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, kinds[v.kind], 0, FALSE, msd.put(), nullptr), "CreateDepthStencilSurface multisampled");
            if (!ms || !msd) {
                return;
            }
            c.chk(c.d->SetRenderTarget(0, ms.get()), "SetRenderTarget");
            c.chk(c.d->SetDepthStencilSurface(msd.get()), "SetDepthStencilSurface");
            c.clear(Dx::kColor(10, 10, 40, 255));
            c.rs(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            if (v.mode == 2) {
                Com<IDirect3DVertexShader9> vs = c.vs("struct I { float4 p : POSITION; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float2 t : TEXCOORD0; };\nO main(I i) { O o; o.p = i.p; o.t = i.t; return o; }\n", "vs_3_0");
                Com<IDirect3DPixelShader9> ps = c.ps("float4 main(centroid float2 uv : TEXCOORD0) : COLOR { return float4(uv.x, uv.y, 1.0 - uv.x * 0.5, 1.0); }\n", "ps_3_0");
                if (!vs || !ps) {
                    return;
                }
                c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
                c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
                struct V { float x, y, z, u, v; };
                const V tri[6] = {{-0.9f, -0.9f, 0.5f, 0, 0}, {0.0f, 0.95f, 0.5f, 1.0f, 0}, {0.9f, -0.5f, 0.5f, 0, 1.0f}, {-0.6f, 0.9f, 0.4f, 0.1f, 0.9f}, {0.8f, 0.8f, 0.4f, 0.9f, 0.1f}, {0.85f, -0.85f, 0.4f, 0.8f, 0.8f}};
                c.fvf(D3DFVF_XYZ | D3DFVF_TEX1);
                c.draw(D3DPT_TRIANGLELIST, 2, tri, sizeof(V));
                c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
                c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
            } else {
                if (v.mode == 1) {
                    c.rs(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFEu);
                }
                c.camera(4.0f);
                c.fvf(kVtxFvf);
                c.xf(D3DTS_WORLD, Xf::rot_y(0.5f) * Xf::rot_x(0.4f));
                Mesh cube = Geo::cube(1.0f, 0xFFFFFFFF);
                const DWORD cols[6] = {0xFFE04040, 0xFF40E040, 0xFF4040E0, 0xFFE0E040, 0xFFE040E0, 0xFF40E0E0};
                for (std::size_t i = 0; i < cube.vertices.size(); ++i) {
                    cube.vertices[i].diffuse = cols[i / 4];
                }
                c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
                c.draw_mesh(cube);
                c.rs(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFFu);
            }
            c.end();
            c.chk(c.d->StretchRect(ms.get(), nullptr, c.target.surface.get(), nullptr, D3DTEXF_NONE), "StretchRect resolve");
            c.chk(c.d->SetRenderTarget(0, c.target.surface.get()), "SetRenderTarget");
            c.chk(c.d->SetDepthStencilSurface(c.depth.surface.get()), "SetDepthStencilSurface");
            c.begin();
        }, {}, [v, kinds](IDirect3DDevice9* device) {
            Com<IDirect3D9> factory;
            device->GetDirect3D(factory.put());
            D3DDEVICE_CREATION_PARAMETERS p{};
            device->GetCreationParameters(&p);
            DWORD quality = 0;
            const bool ok = factory && SUCCEEDED(factory->CheckDeviceMultiSampleType(p.AdapterOrdinal, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, kinds[v.kind], &quality)) &&
                            SUCCEEDED(factory->CheckDeviceMultiSampleType(p.AdapterOrdinal, D3DDEVTYPE_HAL, D3DFMT_D24S8, TRUE, kinds[v.kind], &quality));
            if (ok && v.mode == 2) {
                D3DCAPS9 caps{};
                device->GetDeviceCaps(&caps);
                return caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) ? std::string() : std::string("pixel shader 3.0");
            }
            return ok ? std::string() : std::string("multisampling");
        });
    }
}

const Registrar kClear(clear_cases);
const Registrar kDepth(depth_cases);
const Registrar kFormats(format_cases);
const Registrar kMsaa(msaa_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
