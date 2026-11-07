#include "sanity/d3d9/share_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d9/kit.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <thread>
#include <vector>

namespace lsw::suites::sanity::d3d9 {

namespace {

constexpr UINT kSize = 128;

bool draw_frame(IDirect3DDevice9* device, IDirect3DSurface9* surface, int k) {
    Com<IDirect3DSurface9> original;
    device->GetRenderTarget(0, original.put());
    device->SetRenderTarget(0, surface);
    D3d9Tools::prepare(device, false);
    D3d9Tools::clear(device, Kit::frame_color(k));
    const bool drawn = D3d9Tools::draw_quad(device, 32, 32, 96, 96, Kit::frame_color(k + 50));
    device->SetRenderTarget(0, original.get());
    return drawn;
}

std::string frame_problem(const SceneImage& image, int k) {
    std::string where;
    if (!D3d9Tools::region(image, 0, 0, kSize, 32, Kit::frame_color(k), 0, where) || !D3d9Tools::region(image, 0, 96, kSize, kSize, Kit::frame_color(k), 0, where) ||
        !D3d9Tools::region(image, 32, 32, 96, 96, Kit::frame_color(k + 50), 0, where)) {
        return where;
    }
    return {};
}

std::wstring event_name(const char* role, const std::string& id) {
    return Text::wide("Local\\lsw-suites-d3d9-" + std::string(role) + "-" + id);
}

}  // namespace

Outcome ShareChecks::cross_device(Context& context) {
    D3d9Rig a;
    D3d9Rig b;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!a.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!a.create(settings, error) || !b.open(true, error) || !b.create(settings, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    HANDLE handle = nullptr;
    HRESULT result = S_OK;
    Com<IDirect3DTexture9> produced = D3d9Tools::texture(a.dev(), kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result, &handle);
    if (!produced || handle == nullptr) {
        return Outcome::fail("a shared render target texture cannot be created on device A: " + Kit::hr(result));
    }
    Com<IDirect3DTexture9> opened = D3d9Tools::texture(b.dev(), kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result, &handle);
    if (!opened) {
        return Outcome::fail("the shared texture cannot be opened on device B: " + Kit::hr(result));
    }
    Com<IDirect3DSurface9> surface = D3d9Tools::level(produced.get(), 0);
    for (int k = 0; k < 24; ++k) {
        if (!draw_frame(a.dev(), surface.get(), k)) {
            problems.add("frame " + std::to_string(k) + " failed to draw on device A");
            break;
        }
        if (!D3d9Tools::wait_event(a.dev(), error)) {
            return Outcome::fail(error);
        }
        SceneImage image;
        if (!D3d9Tools::sample(b.dev(), opened.get(), kSize, kSize, image, error)) {
            problems.add("frame " + std::to_string(k) + " cannot be read on device B: " + error);
            break;
        }
        const std::string wrong = frame_problem(image, k);
        if (!wrong.empty()) {
            problems.add("frame " + std::to_string(k) + " seen through device B differs from what A rendered: " + wrong);
            break;
        }
    }
    D3DSURFACE_DESC desc_a{};
    D3DSURFACE_DESC desc_b{};
    produced->GetLevelDesc(0, &desc_a);
    opened->GetLevelDesc(0, &desc_b);
    if (desc_a.Width != desc_b.Width || desc_a.Format != desc_b.Format || desc_a.Usage != desc_b.Usage) {
        problems.add("the opened texture has a different description");
    }
    Kit::check_usable(problems, "device A", a.dev());
    Kit::check_usable(problems, "device B", b.dev());
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ShareChecks::cross_process(Context& context) {
    constexpr int kFrames = 200;
    const std::string handle_text = ChildMode::variable(L"LSW_SUITES_D3D9_HANDLE");
    const std::string id = ChildMode::variable(L"LSW_SUITES_D3D9_ID");
    std::string error;
    if (ChildMode::active() && !handle_text.empty()) {
        D3d9Rig rig;
        D3d9Settings settings;
        settings.depth = false;
        if (!Kit::open(rig, true, error, settings)) {
            return Outcome::fail(error);
        }
        HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(handle_text)));
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> opened = D3d9Tools::texture(rig.dev(), kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result, &handle);
        if (!opened) {
            return Outcome::fail("the consumer process cannot open the shared texture: " + Kit::hr(result));
        }
        HANDLE ready = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, event_name("ready", id).c_str());
        HANDLE consumed = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, event_name("consumed", id).c_str());
        if (ready == nullptr || consumed == nullptr) {
            return Outcome::fail("the consumer process cannot open the frame events");
        }
        Outcome outcome = Outcome::ok();
        for (int k = 0; k < kFrames; ++k) {
            if (::WaitForSingleObject(ready, 30000) != WAIT_OBJECT_0) {
                outcome = Outcome::fail("frame " + std::to_string(k) + " was never announced");
                break;
            }
            SceneImage image;
            if (!D3d9Tools::sample(rig.dev(), opened.get(), kSize, kSize, image, error)) {
                outcome = Outcome::fail("frame " + std::to_string(k) + ": " + error);
                break;
            }
            const std::string wrong = frame_problem(image, k);
            if (!wrong.empty()) {
                outcome = Outcome::fail("frame " + std::to_string(k) + " is torn or wrong in the consumer: " + wrong);
                ::SetEvent(consumed);
                break;
            }
            ::SetEvent(consumed);
        }
        ::CloseHandle(ready);
        ::CloseHandle(consumed);
        return outcome;
    }
    D3d9Rig rig;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    HANDLE handle = nullptr;
    HRESULT result = S_OK;
    Com<IDirect3DTexture9> produced = D3d9Tools::texture(rig.dev(), kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result, &handle);
    if (!produced || handle == nullptr) {
        return Outcome::fail("a shared render target texture cannot be created: " + Kit::hr(result));
    }
    Com<IDirect3DSurface9> surface = D3d9Tools::level(produced.get(), 0);
    const std::string run_id = std::to_string(::GetCurrentProcessId());
    HANDLE ready = ::CreateEventW(nullptr, FALSE, FALSE, event_name("ready", run_id).c_str());
    HANDLE consumed = ::CreateEventW(nullptr, FALSE, FALSE, event_name("consumed", run_id).c_str());
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_D3D9_HANDLE", std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(handle)))}, {"LSW_SUITES_D3D9_ID", run_id}};
    std::atomic<bool> finished{false};
    CheckResult child;
    std::thread consumer([&] {
        child = ChildRuns::run_self("sanity.d3d9.share.cross_process", options);
        finished = true;
    });
    Problems problems;
    int produced_frames = 0;
    for (int k = 0; k < kFrames && !finished; ++k) {
        if (!draw_frame(rig.dev(), surface.get(), k)) {
            problems.add("frame " + std::to_string(k) + " failed to draw");
            break;
        }
        if (!D3d9Tools::wait_event(rig.dev(), error)) {
            problems.add(error);
            break;
        }
        ::SetEvent(ready);
        bool taken = false;
        while (!taken && !finished) {
            taken = ::WaitForSingleObject(consumed, 200) == WAIT_OBJECT_0;
        }
        if (!taken) {
            break;
        }
        ++produced_frames;
    }
    consumer.join();
    ::CloseHandle(ready);
    ::CloseHandle(consumed);
    context.measure("frames_consumed", produced_frames);
    if (child.status != Status::Ok) {
        problems.add("the consumer process failed: " + child.reason);
    } else if (produced_frames != kFrames) {
        problems.add("only " + std::to_string(produced_frames) + " of " + std::to_string(kFrames) + " frames were consumed");
    }
    Kit::check_usable(problems, "the producer", rig.dev());
    return problems.outcome();
}

Outcome ShareChecks::d3dimage_pattern(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3d11Device consumer;
    if (!consumer.create(rig.adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDirect3DSurface9> surfaces[2];
    HANDLE handles[2] = {nullptr, nullptr};
    Com<ID3D11Texture2D> views[2];
    for (int i = 0; i < 2; ++i) {
        const HRESULT created = device->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, surfaces[i].put(), &handles[i]);
        if (FAILED(created) || handles[i] == nullptr) {
            return Outcome::fail("a shared render target cannot be created with a shared handle: " + Kit::hr(created));
        }
        device->ColorFill(surfaces[i].get(), nullptr, 0xFF000000u);
        const HRESULT opened = consumer.device->OpenSharedResource(handles[i], __uuidof(ID3D11Texture2D), views[i].put_void());
        if (FAILED(opened)) {
            return Outcome::fail("D3D11 cannot open the D3D9Ex shared render target: " + D3d11Tools::hr(opened));
        }
        D3D11_TEXTURE2D_DESC desc{};
        views[i]->GetDesc(&desc);
        if (desc.Width != kSize || desc.Height != kSize || (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8X8_UNORM)) {
            problems.add("the D3D11 view of the shared surface is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " with format " + std::to_string(desc.Format));
        }
    }
    if (!D3d9Tools::wait_event(device, error)) {
        return Outcome::fail(error);
    }
    std::vector<std::uint32_t> model[2] = {std::vector<std::uint32_t>(kSize * kSize, 0xFF000000u), std::vector<std::uint32_t>(kSize * kSize, 0xFF000000u)};
    std::vector<std::uint32_t> pending_model;
    int pending_surface = -1;
    int pending_frame = -1;
    auto paint = [&](int surface_index, int tile, int colour_index) {
        const LONG x = (tile % 4) * 32;
        const LONG y = ((tile / 4) % 4) * 32;
        const RECT rect = {x, y, x + 32, y + 32};
        device->ColorFill(surfaces[surface_index].get(), &rect, Kit::frame_color(colour_index));
        for (LONG row = y; row < y + 32; ++row) {
            for (LONG col = x; col < x + 32; ++col) {
                model[surface_index][static_cast<std::size_t>(row) * kSize + static_cast<std::size_t>(col)] = Kit::frame_color(colour_index);
            }
        }
    };
    auto consume = [&]() {
        if (pending_surface < 0) {
            return;
        }
        std::vector<std::uint8_t> bytes;
        if (!D3d11Tools::read_pixels(consumer, views[pending_surface].get(), bytes, error)) {
            problems.add("frame " + std::to_string(pending_frame) + " cannot be read through D3D11: " + error);
            pending_surface = -1;
            return;
        }
        for (std::size_t i = 0; i < pending_model.size(); ++i) {
            std::uint32_t got = 0;
            std::memcpy(&got, bytes.data() + i * 4, 4);
            if ((got & 0xFFFFFFu) != (pending_model[i] & 0xFFFFFFu)) {
                problems.add("frame " + std::to_string(pending_frame) + " read through D3D11 differs at pixel (" + std::to_string(i % kSize) + "," + std::to_string(i / kSize) + "): " + D3d9Tools::color_text(got) + " instead of " + D3d9Tools::color_text(pending_model[i]));
                break;
            }
        }
        pending_surface = -1;
    };
    constexpr int kFrames = 64;
    int consumed = 0;
    for (int k = 0; k < kFrames && problems.empty(); ++k) {
        const int s = k & 1;
        if (k >= 1) {
            paint(s, (k - 1) % 16, k - 1);
        }
        paint(s, k % 16, k);
        Com<IDirect3DQuery9> done;
        device->CreateQuery(D3DQUERYTYPE_EVENT, done.put());
        done->Issue(D3DISSUE_END);
        BOOL flag = FALSE;
        done->GetData(&flag, sizeof(flag), D3DGETDATA_FLUSH);
        consume();
        ++consumed;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (done->GetData(&flag, sizeof(flag), D3DGETDATA_FLUSH) == S_FALSE && std::chrono::steady_clock::now() < deadline) {
            ::SwitchToThread();
        }
        pending_surface = s;
        pending_frame = k;
        pending_model = model[s];
    }
    consume();
    context.measure("frames", consumed);
    D3d9Rig second;
    if (problems.empty() && second.open(true, error) && second.create(settings, error)) {
        Com<IDirect3DSurface9> reopened;
        HANDLE handle = handles[(kFrames - 1) & 1];
        const HRESULT result = second.dev()->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, reopened.put(), &handle);
        if (FAILED(result)) {
            problems.add("a second D3D9Ex device cannot open the shared render target: " + Kit::hr(result));
        } else {
            SceneImage image;
            if (D3d9Tools::read_surface(second.dev(), reopened.get(), image, error)) {
                const std::vector<std::uint32_t>& last = model[(kFrames - 1) & 1];
                for (std::size_t i = 0; i < last.size(); ++i) {
                    if ((D3d9Tools::pixel(image, static_cast<UINT>(i % kSize), static_cast<UINT>(i / kSize)) & 0xFFFFFFu) != (last[i] & 0xFFFFFFu)) {
                        problems.add("the second D3D9Ex device reads a different last frame at pixel " + std::to_string(i));
                        break;
                    }
                }
            } else {
                problems.add("the second D3D9Ex device cannot read the shared surface: " + error);
            }
        }
    }
    Kit::check_usable(problems, "the producer", device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
