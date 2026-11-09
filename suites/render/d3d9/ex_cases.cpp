#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

struct Dev2 {
    TestWindow window;
    Com<IDirect3DDevice9> device;
    Com<IDirect3DDevice9Ex> extended;
    D3DPRESENT_PARAMETERS pp{};
    UINT ordinal = 0;

    bool open(IDirect3DDevice9* like, bool want_ex, UINT width, UINT height, D3DSWAPEFFECT effect, UINT buffers, std::string& error) {
        Com<IDirect3D9> factory;
        like->GetDirect3D(factory.put());
        D3DDEVICE_CREATION_PARAMETERS params{};
        like->GetCreationParameters(&params);
        ordinal = params.AdapterOrdinal;
        TestWindowConfig config;
        TestWindow::fit_config(config, width, height);
        if (!window.create(config, error)) {
            return false;
        }
        pp = D3DPRESENT_PARAMETERS{};
        pp.BackBufferWidth = width;
        pp.BackBufferHeight = height;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = buffers;
        pp.SwapEffect = effect;
        pp.hDeviceWindow = window.hwnd();
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
        HRESULT result = E_FAIL;
        if (want_ex) {
            Com<IDirect3D9Ex> factory_ex = factory.as<IDirect3D9Ex>();
            if (!factory_ex) {
                error = "no IDirect3D9Ex";
                return false;
            }
            result = factory_ex->CreateDeviceEx(ordinal, D3DDEVTYPE_HAL, window.hwnd(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, extended.put());
            if (SUCCEEDED(result)) {
                device = extended.as<IDirect3DDevice9>();
            }
        } else {
            result = factory->CreateDevice(ordinal, D3DDEVTYPE_HAL, window.hwnd(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, device.put());
        }
        if (FAILED(result)) {
            error = "second device: " + D3d9Tools::hr(result);
            return false;
        }
        return true;
    }

    [[nodiscard]] Surface back(UINT width, UINT height) {
        Surface s;
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, s.surface.put());
        s.format = D3DFMT_X8R8G8B8;
        s.width = width;
        s.height = height;
        return s;
    }
};

Com<IDirect3DDevice9Ex> query_ex(IDirect3DDevice9* d) {
    IDirect3DDevice9Ex* e = nullptr;
    d->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&e));
    return Com<IDirect3DDevice9Ex>(e);
}

void raw_quad(IDirect3DDevice9* d, float x0, float y0, float x1, float y1, DWORD tl, DWORD tr, DWORD bl, DWORD br) {
    struct V { float x, y, z, rhw; DWORD color; };
    const V v[4] = {{x0, y0, 0.5f, 1, tl}, {x1, y0, 0.5f, 1, tr}, {x0, y1, 0.5f, 1, bl}, {x1, y1, 0.5f, 1, br}};
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V));
}

void scene_for(IDirect3DDevice9* d, UINT w, UINT h, unsigned f) {
    d->Clear(0, nullptr, D3DCLEAR_TARGET, Dx::kColor(10 + 20 * f, 20, 60), 1.0f, 0);
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    raw_quad(d, 0.1f * fw, 0.1f * fh, 0.9f * fw, 0.9f * fh, 0xFFE05030, 0xFF30E050, 0xFF3050E0, 0xFFE0E030);
    const float t = static_cast<float>(f) / 5.0f;
    raw_quad(d, (0.15f + 0.5f * t) * fw, 0.3f * fh, (0.35f + 0.5f * t) * fw, 0.7f * fh, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFF000000);
}

rr::CaseSpec ex(const char* name, unsigned frames = 1, std::vector<unsigned> captured = {}) {
    rr::CaseSpec spec("ex", name);
    spec.variant(1);
    if (frames > 1) {
        spec.frames(frames, std::move(captured));
    }
    return spec;
}

Need ex_need() {
    return [](IDirect3DDevice9* device) {
        Com<IDirect3DDevice9Ex> e = query_ex(device);
        return e ? std::string() : std::string("IDirect3DDevice9Ex");
    };
}

IDirect3DDevice9Ex* as_ex(Ctx& c, Com<IDirect3DDevice9Ex>& holder) {
    holder = query_ex(c.d);
    if (!holder) {
        c.fail("device is not IDirect3DDevice9Ex");
    }
    return holder.get();
}

void surface_cases() {
    Cases::single(ex("create_render_target_ex"), [](Ctx& c) {
        Com<IDirect3DDevice9Ex> holder;
        IDirect3DDevice9Ex* e = as_ex(c, holder);
        if (e == nullptr) {
            return;
        }
        Com<IDirect3DSurface9> rt;
        c.chk(e->CreateRenderTargetEx(64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, rt.put(), nullptr, 0), "CreateRenderTargetEx");
        if (!rt) {
            return;
        }
        c.clear(Dx::kColor(10, 10, 40));
        c.chk(c.d->SetRenderTarget(0, rt.get()), "SetRenderTarget");
        c.clear(Dx::kColor(240, 240, 200), D3DCLEAR_TARGET);
        c.quad(4, 4, 60, 60, 0.5f, 0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFFFFFF40);
        RttOps::pop(c);
        const RECT dst{16, 16, 112, 112};
        c.chk(c.d->StretchRect(rt.get(), nullptr, c.target.surface.get(), &dst, D3DTEXF_LINEAR), "StretchRect");
    }, {}, ex_need());
    Cases::single(ex("create_offscreen_plain_surface_ex"), [](Ctx& c) {
        Com<IDirect3DDevice9Ex> holder;
        IDirect3DDevice9Ex* e = as_ex(c, holder);
        if (e == nullptr) {
            return;
        }
        Com<IDirect3DSurface9> sys;
        Com<IDirect3DSurface9> dev;
        c.chk(e->CreateOffscreenPlainSurfaceEx(48, 48, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, sys.put(), nullptr, 0), "CreateOffscreenPlainSurfaceEx system");
        c.chk(e->CreateOffscreenPlainSurfaceEx(48, 48, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, dev.put(), nullptr, 0), "CreateOffscreenPlainSurfaceEx default");
        D3DLOCKED_RECT lr{};
        if (!sys || !dev || FAILED(sys->LockRect(&lr, nullptr, 0))) {
            return;
        }
        for (UINT y = 0; y < 48; ++y) {
            DWORD* row = reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(lr.pBits) + static_cast<std::size_t>(y) * lr.Pitch);
            for (UINT x = 0; x < 48; ++x) {
                row[x] = Texels::rainbow()(x, y, 48, 48);
            }
        }
        sys->UnlockRect();
        c.chk(c.d->UpdateSurface(sys.get(), nullptr, dev.get(), nullptr), "UpdateSurface");
        c.clear(Dx::kColor(10, 10, 40));
        c.chk(c.d->StretchRect(dev.get(), nullptr, c.target.surface.get(), nullptr, D3DTEXF_POINT), "StretchRect");
    }, {}, ex_need());
    Cases::single(ex("create_depth_stencil_surface_ex"), [](Ctx& c) {
        Com<IDirect3DDevice9Ex> holder;
        IDirect3DDevice9Ex* e = as_ex(c, holder);
        if (e == nullptr) {
            return;
        }
        Com<IDirect3DSurface9> ds;
        c.chk(e->CreateDepthStencilSurfaceEx(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, ds.put(), nullptr, 0), "CreateDepthStencilSurfaceEx");
        if (!ds) {
            return;
        }
        c.chk(c.d->SetDepthStencilSurface(ds.get()), "SetDepthStencilSurface");
        c.clear(Dx::kColor(10, 10, 40), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
        c.quad(10, 10, 90, 90, 0.4f, 0xFF40E060);
        c.quad(40, 40, 120, 120, 0.7f, 0xFFE04060);
        c.quad(40, 40, 90, 90, 0.2f, 0xFF4060E0);
    }, {}, ex_need());
    Cases::single(ex("gpu_thread_priority_and_frame_latency_do_not_alter_image"), [](Ctx& c) {
        Com<IDirect3DDevice9Ex> holder;
        IDirect3DDevice9Ex* e = as_ex(c, holder);
        if (e == nullptr) {
            return;
        }
        c.chk(e->SetGPUThreadPriority(3), "SetGPUThreadPriority");
        c.chk(e->SetMaximumFrameLatency(2), "SetMaximumFrameLatency");
        INT priority = 0;
        UINT latency = 0;
        c.chk(e->GetGPUThreadPriority(&priority), "GetGPUThreadPriority");
        c.chk(e->GetMaximumFrameLatency(&latency), "GetMaximumFrameLatency");
        if (priority != 3 || latency != 2) {
            c.fail("GPU thread priority or frame latency did not read back as set");
            return;
        }
        c.clear(Dx::kColor(10, 10, 40));
        c.camera(4.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        c.rs(D3DRS_LIGHTING, TRUE);
        c.rs(D3DRS_AMBIENT, 0xFF303030);
        c.material(Dx::material(0.9f, 0.6f, 0.3f, 0.9f, 0.6f, 0.3f));
        c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
        c.fvf(kVtxFvf);
        c.draw_mesh(Geo::sphere(24, 16, 1.3f, 0xFFFFFFFF));
    }, {}, ex_need());
}

void second_device_cases() {
    Cases::frames(ex("present_ex_flipex_loop", 6, {0, 2, 5}), [] {
        auto dev = std::make_shared<Dev2>();
        return FrameBody([dev](Ctx& c, unsigned f) {
            std::string error;
            if (f == 0 && !dev->open(c.d, true, 128, 128, D3DSWAPEFFECT_FLIPEX, 2, error)) {
                c.fail(error);
                return;
            }
            if (!dev->device) {
                return;
            }
            IDirect3DDevice9* d = dev->device.get();
            Surface back = dev->back(128, 128);
            d->SetRenderTarget(0, back.surface.get());
            d->BeginScene();
            scene_for(d, 128, 128, f);
            d->EndScene();
            c.snap_on(d, back, "");
            c.chk(dev->extended->PresentEx(nullptr, nullptr, nullptr, nullptr, 0), "PresentEx");
        });
    }, {}, ex_need());

    Cases::frames(ex("present_ex_discard_check_device_state", 6, {0, 3, 5}), [] {
        auto dev = std::make_shared<Dev2>();
        return FrameBody([dev](Ctx& c, unsigned f) {
            std::string error;
            if (f == 0 && !dev->open(c.d, true, 128, 128, D3DSWAPEFFECT_DISCARD, 1, error)) {
                c.fail(error);
                return;
            }
            if (!dev->device) {
                return;
            }
            IDirect3DDevice9* d = dev->device.get();
            Surface back = dev->back(128, 128);
            d->BeginScene();
            scene_for(d, 128, 128, f);
            d->EndScene();
            c.snap_on(d, back, "");
            const HRESULT state = dev->extended->CheckDeviceState(dev->window.hwnd());
            if (FAILED(state)) {
                c.fail("CheckDeviceState: " + D3d9Tools::hr(state));
            }
            c.chk(dev->extended->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT), "PresentEx");
        });
    }, {}, ex_need());

    Cases::frames(ex("reset_ex_changes_back_buffer_size", 4, {0, 1, 2, 3}), [] {
        auto dev = std::make_shared<Dev2>();
        return FrameBody([dev](Ctx& c, unsigned f) {
            std::string error;
            const UINT sizes[4] = {96, 160, 96, 128};
            if (f == 0 && !dev->open(c.d, true, 96, 96, D3DSWAPEFFECT_DISCARD, 1, error)) {
                c.fail(error);
                return;
            }
            if (!dev->device) {
                return;
            }
            if (f > 0) {
                dev->pp.BackBufferWidth = sizes[f];
                dev->pp.BackBufferHeight = sizes[f];
                c.chk(dev->extended->ResetEx(&dev->pp, nullptr), "ResetEx");
            }
            IDirect3DDevice9* d = dev->device.get();
            Surface back = dev->back(sizes[f], sizes[f]);
            d->BeginScene();
            scene_for(d, sizes[f], sizes[f], f);
            d->EndScene();
            c.snap_on(d, back, "");
            c.chk(dev->extended->PresentEx(nullptr, nullptr, nullptr, nullptr, 0), "PresentEx");
        });
    }, {}, ex_need());

    Cases::frames(rr::CaseSpec("ex", "reset_restores_rendering_plain_device").frames(5, {0, 2, 4}), [] {
        auto dev = std::make_shared<Dev2>();
        return FrameBody([dev](Ctx& c, unsigned f) {
            std::string error;
            if (f == 0 && !dev->open(c.d, false, 128, 128, D3DSWAPEFFECT_DISCARD, 1, error)) {
                c.fail(error);
                return;
            }
            if (!dev->device) {
                return;
            }
            IDirect3DDevice9* d = dev->device.get();
            if (f == 1 || f == 3) {
                c.chk(d->Reset(&dev->pp), "Reset");
            }
            Surface back = dev->back(128, 128);
            d->BeginScene();
            scene_for(d, 128, 128, f);
            d->EndScene();
            c.snap_on(d, back, "");
            c.chk(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        });
    });

    Cases::single(ex("shared_render_target_opened_by_second_device"), [](Ctx& c) {
        Com<IDirect3DTexture9> source;
        HANDLE shared = nullptr;
        c.chk(c.d->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, source.put(), &shared), "CreateTexture shared");
        if (!source || shared == nullptr) {
            c.fail("no shared handle was produced");
            return;
        }
        Com<IDirect3DSurface9> level;
        source->GetSurfaceLevel(0, level.put());
        c.chk(c.d->SetRenderTarget(0, level.get()), "SetRenderTarget");
        c.clear(Dx::kColor(240, 230, 120), D3DCLEAR_TARGET);
        c.quad(4, 4, 60, 60, 0.5f, 0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFF202020);
        c.quad(20, 20, 44, 44, 0.4f, 0xFFFFFFFF);
        RttOps::pop(c);
        Com<IDirect3DQuery9> done;
        if (SUCCEEDED(c.d->CreateQuery(D3DQUERYTYPE_EVENT, done.put())) && done) {
            done->Issue(D3DISSUE_END);
            c.end();
            int spins = 0;
            while (done->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE && ++spins < 20000) {
                Sleep(0);
            }
            c.begin();
        }
        Dev2 other;
        std::string error;
        if (!other.open(c.d, true, 128, 128, D3DSWAPEFFECT_DISCARD, 1, error)) {
            c.fail(error);
            return;
        }
        Com<IDirect3DTexture9> opened;
        HANDLE handle = shared;
        const HRESULT result = other.device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, opened.put(), &handle);
        if (FAILED(result)) {
            c.fail("opening the shared render target on a second device: " + D3d9Tools::hr(result));
            return;
        }
        IDirect3DDevice9* d = other.device.get();
        Surface back = other.back(128, 128);
        d->BeginScene();
        d->Clear(0, nullptr, D3DCLEAR_TARGET, Dx::kColor(10, 10, 40), 1.0f, 0);
        d->SetTexture(0, opened.get());
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        const std::vector<TlVtx> q = Geo::tl_quad(8, 8, 120, 120, 0xFFFFFFFF);
        d->SetFVF(kTlFvf);
        d->SetRenderState(D3DRS_LIGHTING, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
        d->EndScene();
        c.snap_on(d, back, "");
    }, {}, [](IDirect3DDevice9* device) {
        Com<IDirect3DTexture9> probe;
        HANDLE handle = nullptr;
        Com<IDirect3DDevice9Ex> e = query_ex(device);
        if (!e) {
            return std::string("IDirect3DDevice9Ex");
        }
        return SUCCEEDED(device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, probe.put(), &handle)) && handle != nullptr
                   ? std::string()
                   : std::string("shared resources");
    });
}

const Registrar kSurfaces(surface_cases);
const Registrar kDevices(second_device_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
