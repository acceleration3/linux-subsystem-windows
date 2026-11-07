#include "sanity/d3d9/fail_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d9/kit.hpp"
#include "support/child_runs.hpp"
#include "support/dxgi_budget.hpp"
#include "support/leak_loop.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace lsw::suites::sanity::d3d9 {

namespace {

void render_check(Problems& problems, const std::string& label, IDirect3DDevice9* device) {
    std::string error;
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 32, 32);
    if (!target) {
        problems.add(label + ": a render target cannot be created afterwards");
        return;
    }
    Com<IDirect3DSurface9> original;
    device->GetRenderTarget(0, original.put());
    device->SetRenderTarget(0, target.get());
    D3d9Tools::prepare(device, false);
    D3d9Tools::clear(device, 0xFF000000u);
    const bool drawn = D3d9Tools::draw_quad(device, 4, 4, 28, 28, 0xFF44AA88u);
    SceneImage image;
    std::string where;
    if (!drawn || !D3d9Tools::read_surface(device, target.get(), image, error)) {
        problems.add(label + ": a draw afterwards failed " + error);
    } else if (!D3d9Tools::region(image, 4, 4, 28, 28, 0xFF44AA88u, 0, where) || !D3d9Tools::region(image, 0, 0, 32, 4, 0xFF000000u, 0, where)) {
        problems.add(label + ": a draw afterwards renders wrongly: " + where);
    }
    device->SetRenderTarget(0, original.get());
}

bool is_depth_format(D3DFORMAT format) {
    switch (format) {
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D32:
    case D3DFMT_D15S1:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case D3DFMT_D16:
    case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D24FS8:
    case D3DFMT_D32_LOCKABLE:
    case D3DFMT_S8_LOCKABLE: return true;
    default: return false;
    }
}

}  // namespace

Outcome FailChecks::unsupported_features(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    IDirect3D9* d3d = rig.d3d.get();
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    Problems problems;
    const D3DFORMAT formats[] = {D3DFMT_MULTI2_ARGB8, D3DFMT_UYVY, D3DFMT_YUY2, D3DFMT_DXT1, D3DFMT_DXT5, D3DFMT_A32B32G32R32F, D3DFMT_A16B16G16R16F, D3DFMT_D16_LOCKABLE,
                                 D3DFMT_D32, D3DFMT_D15S1, D3DFMT_A8, D3DFMT_L16, D3DFMT_Q8W8V8U8, D3DFMT_CxV8U8, D3DFMT_A2W10V10U10};
    int absent = 0;
    for (const D3DFORMAT format : formats) {
        const std::string name = "format " + std::to_string(static_cast<unsigned>(format));
        struct Probe {
            const char* what;
            DWORD usage;
            D3DRESOURCETYPE type;
        };
        for (const Probe& probe : {Probe{"render target texture", D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE}, Probe{"depth-stencil texture", D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE},
                                   Probe{"plain texture", 0, D3DRTYPE_TEXTURE}, Probe{"render target surface", D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE},
                                   Probe{"depth-stencil surface", D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE}}) {
            if (probe.usage == D3DUSAGE_DEPTHSTENCIL && !is_depth_format(format)) {
                continue;
            }
            const HRESULT query = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, probe.usage, probe.type, format);
            if (query == D3D_OK) {
                continue;
            }
            if (query != D3DERR_NOTAVAILABLE && query != D3DERR_INVALIDCALL) {
                problems.add(name + " " + probe.what + ": CheckDeviceFormat returned " + Kit::hr(query));
                continue;
            }
            ++absent;
            HRESULT created = S_OK;
            if (probe.type == D3DRTYPE_TEXTURE) {
                Com<IDirect3DTexture9> t = D3d9Tools::texture(device, 64, 64, 1, probe.usage, format, D3DPOOL_DEFAULT, &created);
            } else if (probe.usage == D3DUSAGE_RENDERTARGET) {
                Com<IDirect3DSurface9> s = D3d9Tools::target(device, 64, 64, format, D3DMULTISAMPLE_NONE, false, &created);
            } else {
                Com<IDirect3DSurface9> s;
                created = device->CreateDepthStencilSurface(64, 64, format, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr);
            }
            Kit::expect_failure(problems, name + " " + probe.what + " that CheckDeviceFormat reports unavailable", created);
            render_check(problems, name + " " + probe.what, device);
        }
    }
    context.measure("unavailable_combinations", absent);
    if (absent == 0) {
        context.note("every probed format is available on this device");
    }
    {
        HRESULT created = S_OK;
        Com<IDirect3DTexture9> t = D3d9Tools::texture(device, caps.MaxTextureWidth + 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &created);
        Kit::expect_refused(problems, "a texture wider than MaxTextureWidth", created);
        t = D3d9Tools::texture(device, 1, caps.MaxTextureHeight + 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &created);
        Kit::expect_refused(problems, "a texture taller than MaxTextureHeight", created);
        if ((caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0) {
            Com<IDirect3DVolumeTexture9> volume;
            created = device->CreateVolumeTexture(caps.MaxVolumeExtent + 1, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, volume.put(), nullptr);
            Kit::expect_refused(problems, "a volume texture beyond MaxVolumeExtent", created);
        }
        if ((caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) == 0) {
            Com<IDirect3DCubeTexture9> cube;
            created = device->CreateCubeTexture(64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, cube.put(), nullptr);
            Kit::expect_failure(problems, "a cube texture without the cube map cap", created);
        }
        render_check(problems, "after the oversized resources", device);
    }
    {
        const float data[4] = {0, 0, 0, 0};
        if (caps.MaxVertexShaderConst > 0) {
            Kit::expect_failure(problems, "SetVertexShaderConstantF at register MaxVertexShaderConst", device->SetVertexShaderConstantF(caps.MaxVertexShaderConst, data, 1));
        }
        if (caps.PixelShaderVersion >= D3DPS_VERSION(3, 0)) {
            Kit::expect_failure(problems, "SetPixelShaderConstantF at register 224", device->SetPixelShaderConstantF(224, data, 1));
        } else if (caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
            Kit::expect_failure(problems, "SetPixelShaderConstantF at register 32 of a ps_2_0 device", device->SetPixelShaderConstantF(224 + 32, data, 1));
        }
        const struct {
            const char* profile;
            DWORD version;
            bool vertex;
        } models[] = {{"vs_3_0", D3DVS_VERSION(3, 0), true}, {"ps_3_0", D3DPS_VERSION(3, 0), false}, {"vs_2_0", D3DVS_VERSION(2, 0), true}, {"ps_2_0", D3DPS_VERSION(2, 0), false}};
        for (const auto& m : models) {
            const DWORD have = m.vertex ? caps.VertexShaderVersion : caps.PixelShaderVersion;
            if (have >= m.version) {
                continue;
            }
            std::vector<std::uint8_t> code;
            const std::string source = m.vertex ? "float4 main(float4 p : POSITION) : POSITION { return p; }\n" : "float4 main() : COLOR { return 1; }\n";
            if (!D3d9Tools::compile(source, m.profile, code, error)) {
                continue;
            }
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps;
            const HRESULT created = m.vertex ? device->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), vs.put()) : device->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), ps.put());
            Kit::expect_failure(problems, std::string("a ") + m.profile + " shader above the device's shader model", created);
        }
        render_check(problems, "after the shader probes", device);
    }
    {
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        Com<IDirect3DSurface9> small = D3d9Tools::target(device, 32, 32);
        const UINT beyond = std::min<UINT>(std::max<UINT>(caps.NumSimultaneousRTs, 1), 4);
        if (beyond < 4 && small) {
            Kit::expect_failure(problems, "SetRenderTarget beyond NumSimultaneousRTs", device->SetRenderTarget(beyond, small.get()));
        }
        device->SetRenderTarget(0, original.get());
        render_check(problems, "after the render target probes", device);
    }
    return problems.outcome();
}

Outcome FailChecks::bad_parameters(Context& context) {
    Problems problems;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        if (!Kit::open(rig, ex, error)) {
            if (ex) {
                continue;
            }
            return Outcome::fail(error);
        }
        IDirect3DDevice9* device = rig.dev();
        const std::string n = std::string("D3D") + Kit::label(ex) + " ";
        D3DCAPS9 caps{};
        device->GetDeviceCaps(&caps);
        Com<IDirect3DTexture9> texture;
        Kit::expect_failure(problems, n + "CreateTexture with pool 99", device->CreateTexture(32, 32, 1, 0, D3DFMT_A8R8G8B8, static_cast<D3DPOOL>(99), texture.put(), nullptr));
        Kit::expect_failure(problems, n + "CreateTexture with D3DFMT_UNKNOWN", device->CreateTexture(32, 32, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, texture.put(), nullptr));
        Kit::expect_failure(problems, n + "CreateTexture of size 0x0", device->CreateTexture(0, 0, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture.put(), nullptr));
        Com<IDirect3DVertexBuffer9> vb;
        Kit::expect_failure(problems, n + "CreateVertexBuffer of length 0", device->CreateVertexBuffer(0, 0, 0, D3DPOOL_DEFAULT, vb.put(), nullptr));
        Com<IDirect3DSurface9> plain;
        Kit::expect_failure(problems, n + "CreateOffscreenPlainSurface of size 0x0", device->CreateOffscreenPlainSurface(0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, plain.put(), nullptr));
        Com<IDirect3DVertexDeclaration9> declaration;
        Kit::expect_failure(problems, n + "SetRenderTarget(0, NULL)", device->SetRenderTarget(0, nullptr));
        Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 64, 64);
        Kit::expect_failure(problems, n + "SetRenderTarget(4, surface)", device->SetRenderTarget(4, rt.get()));
        Com<IDirect3DSurface9> original_rt;
        Com<IDirect3DSurface9> original_ds;
        device->GetRenderTarget(0, original_rt.put());
        device->GetDepthStencilSurface(original_ds.put());
        Com<IDirect3DSurface9> small_depth;
        device->CreateDepthStencilSurface(32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, small_depth.put(), nullptr);
        if (rt && small_depth) {
            device->SetRenderTarget(0, rt.get());
            const HRESULT set_depth = device->SetDepthStencilSurface(small_depth.get());
            const HRESULT cleared = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
            if (SUCCEEDED(set_depth) && SUCCEEDED(cleared)) {
                problems.add(n + "a 32x32 depth buffer with a 64x64 render target is accepted by both SetDepthStencilSurface and Clear");
            } else if (FAILED(cleared) && cleared != D3DERR_INVALIDCALL) {
                problems.add(n + "Clear with a depth buffer smaller than the render target returned " + Kit::hr(cleared));
            }
            context.measure(n + "mismatched_depth", Kit::hr(set_depth) + " / " + Kit::hr(cleared));
        }
        device->SetRenderTarget(0, original_rt.get());
        device->SetDepthStencilSurface(original_ds.get());
        D3DVIEWPORT9 viewport = {0, 0, 0, 64, 0.0f, 1.0f};
        device->SetRenderTarget(0, rt.get());
        Kit::expect_failure(problems, n + "SetViewport with width 0", device->SetViewport(&viewport));
        viewport = {32, 0, 64, 64, 0.0f, 1.0f};
        Kit::expect_failure(problems, n + "SetViewport reaching beyond the render target", device->SetViewport(&viewport));
        device->SetRenderTarget(0, original_rt.get());
        device->SetFVF(D3d9Tools::kTlFvf);
        Kit::check_usable(problems, n + "after the bad calls", device);
        render_check(problems, n + "after the bad calls", device);
    }
    return problems.outcome();
}

Outcome FailChecks::lifetime(Context& context) {
    const std::string mode = ChildMode::variable(L"LSW_SUITES_EXIT_MODE");
    std::string error;
    D3d9Rig rig;
    if (ChildMode::active() && !mode.empty()) {
        D3d9Rig* leaked = new D3d9Rig();
        D3d9Settings settings;
        settings.depth = false;
        if (!Kit::open(*leaked, true, error, settings)) {
            return Outcome::fail(error);
        }
        VideoMemory video;
        static_cast<void>(DxgiBudget::query(leaked->adapter.luid, true, video, error));
        const std::uint64_t want = std::min<std::uint64_t>(std::max<std::uint64_t>(video.budget / 4, 256ull << 20), sizeof(void*) == 4 ? (512ull << 20) : (1ull << 30));
        std::uint64_t total = 0;
        while (total < want) {
            Com<IDirect3DSurface9> s = D3d9Tools::target(leaked->dev(), 4096, 4096);
            if (!s) {
                break;
            }
            leaked->dev()->ColorFill(s.get(), nullptr, 0xFF303030u);
            total += 64ull << 20;
            static_cast<void>(s.detach());
        }
        static_cast<void>(D3d9Tools::wait_event(leaked->dev(), error));
        context.measure("held_mib", total >> 20);
        if (mode == "kill") {
            ::TerminateProcess(::GetCurrentProcess(), 0);
        }
        return Outcome::ok();
    }
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    Problems problems;
    {
        std::vector<int> order = {0, 1, 2, 3, 4, 5};
        for (int round = 0; round < 12; ++round) {
            std::rotate(order.begin(), order.begin() + (round % 5) + 1, order.end());
            if (round % 2 == 1) {
                std::reverse(order.begin(), order.end());
            }
            HRESULT created = rig.try_create(settings);
            if (FAILED(created)) {
                problems.add("CreateDevice failed in round " + std::to_string(round) + ": " + Kit::hr(created));
                break;
            }
            IDirect3DDevice9* device = rig.dev();
            IDirect3DVertexBuffer9* vb = nullptr;
            IDirect3DTexture9* texture = nullptr;
            IDirect3DSurface9* surface = nullptr;
            IDirect3DVertexShader9* shader = nullptr;
            IDirect3DQuery9* query = nullptr;
            device->CreateVertexBuffer(256, 0, 0, D3DPOOL_DEFAULT, &vb, nullptr);
            device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr);
            device->CreateRenderTarget(64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &surface, nullptr);
            std::vector<std::uint8_t> code;
            if (D3d9Tools::compile("float4 main(float4 p : POSITION) : POSITION { return p; }\n", "vs_2_0", code, error)) {
                device->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &shader);
            }
            device->CreateQuery(D3DQUERYTYPE_EVENT, &query);
            if (!vb || !texture || !surface || !shader || !query) {
                problems.add("round " + std::to_string(round) + ": the resources cannot be created");
            } else {
                IDirect3DSurface9* level = nullptr;
                texture->GetSurfaceLevel(0, &level);
                device->SetRenderTarget(0, surface);
                device->SetTexture(0, texture);
                device->SetStreamSource(0, vb, 0, 16);
                device->SetVertexShader(shader);
                device->ColorFill(level, nullptr, 0xFF102030u);
                query->Issue(D3DISSUE_END);
                if (level != nullptr) {
                    level->Release();
                }
            }
            IDirect3DDevice9* device_ptr = device;
            device_ptr->AddRef();
            IUnknown* objects[6] = {device_ptr, vb, texture, surface, shader, query};
            rig.close_device();
            for (const int index : order) {
                if (objects[index] != nullptr) {
                    objects[index]->Release();
                    objects[index] = nullptr;
                }
            }
        }
        context.note("12 release orders were exercised with the device released at varying positions");
    }
    {
        const HRESULT created = rig.try_create(settings);
        if (FAILED(created)) {
            return Outcome::fail("CreateDevice failed: " + Kit::hr(created));
        }
        IDirect3DDevice9* device = rig.dev();
        Com<IDirect3DSurface9> bound_target = D3d9Tools::target(device, 64, 64);
        Com<IDirect3DTexture9> bound_texture = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        static_cast<void>(D3d9Tools::write_texture_level(bound_texture.get(), 0, 17, error));
        device->SetRenderTarget(0, bound_target.get());
        device->SetTexture(0, bound_texture.get());
        bound_target.reset();
        bound_texture.reset();
        D3d9Tools::prepare(device, true);
        device->SetTexture(0, nullptr);
        Com<IDirect3DSurface9> current;
        device->GetRenderTarget(0, current.put());
        if (!current) {
            problems.add("releasing the application's reference to the current render target removed it from the device");
        } else {
            D3d9Tools::prepare(device, false);
            D3d9Tools::clear(device, 0xFF0000FFu);
            SceneImage image;
            std::string where;
            if (!D3d9Tools::read_surface(device, current.get(), image, error) || !D3d9Tools::uniform(image, 0xFF0000FFu, 0, where)) {
                problems.add("the render target released by the application no longer works: " + where + error);
            }
        }
        current.reset();
        device->SetRenderTarget(0, original.get());
        Kit::check_usable(problems, "after releasing bound resources", device);
        rig.close_device();
    }
    {
        AvAdapter adapter = rig.adapter;
        LeakLoop::run(context, problems, "device_cycles", adapter.luid, 40, [&](int, std::string& why) {
            const HRESULT created = rig.try_create(settings);
            if (FAILED(created)) {
                why = "CreateDevice failed with " + Kit::hr(created);
                return false;
            }
            Com<IDirect3DSurface9> s = D3d9Tools::target(rig.dev(), 256, 256);
            Com<IDirect3DTexture9> t = D3d9Tools::texture(rig.dev(), 128, 128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
            if (s) {
                rig.dev()->ColorFill(s.get(), nullptr, 0xFF555555u);
            }
            rig.close_device();
            return true;
        });
    }
    {
        std::vector<long long> held;
        for (const char* how : {"clean", "clean", "kill"}) {
            ChildRunOptions options;
            options.timeout_seconds = 120;
            options.env = {{"LSW_SUITES_EXIT_MODE", how}};
            VideoMemory before;
            if (!DxgiBudget::query(rig.adapter.luid, true, before, error)) {
                return Outcome::fail(error);
            }
            const CheckResult child = ChildRuns::run_self("sanity.d3d9.fail.lifetime", options);
            if (std::string(how) == "clean" && child.status != Status::Ok) {
                problems.add("the process that exits with a live device failed: " + child.reason);
                continue;
            }
            if (std::string(how) == "clean") {
                held.push_back(ChildRuns::number(child, "held_mib"));
            }
            const std::uint64_t free_before = before.budget > before.usage ? before.budget - before.usage : 0;
            VideoMemory after;
            bool restored = false;
            for (int poll = 0; poll < 150 && !restored; ++poll) {
                if (DxgiBudget::query(rig.adapter.luid, true, after, error)) {
                    const std::uint64_t free_after = after.budget > after.usage ? after.budget - after.usage : 0;
                    restored = free_after + (256ull << 20) >= free_before;
                }
                if (!restored) {
                    ::Sleep(100);
                }
            }
            if (!restored) {
                problems.add(std::string(how) + ": the memory of the exited process was not returned within 15 s");
            }
        }
        if (held.size() == 2) {
            context.measure("held_mib.first", held[0]);
            context.measure("held_mib.second", held[1]);
            if (held[1] < held[0]) {
                problems.add("the second process allocated " + std::to_string(held[1]) + " MiB, less than the first (" + std::to_string(held[0]) + " MiB), after the first exited with a live device");
            }
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
