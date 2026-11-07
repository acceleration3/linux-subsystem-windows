#include "sanity/dxgi/swap_checks.hpp"

#include "core/text.hpp"
#include "support/d3d11_tools.hpp"
#include "support/dyn_library.hpp"
#include "support/format_codec.hpp"
#include "support/problems.hpp"
#include "support/swap_rig.hpp"

#include <dcomp.h>
#include <dxgi1_6.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>

namespace lsw::suites::sanity::dxgi {

namespace {

const char* effect_name(DXGI_SWAP_EFFECT e) {
    switch (e) {
    case DXGI_SWAP_EFFECT_DISCARD: return "bitblt-discard";
    case DXGI_SWAP_EFFECT_SEQUENTIAL: return "bitblt-sequential";
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "flip-sequential";
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "flip-discard";
    }
    return "?";
}

bool is_flip(DXGI_SWAP_EFFECT e) {
    return e == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || e == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

int encode(double linear) {
    return static_cast<int>(std::lround(FormatCodec::linear_to_srgb(linear) * 255.0));
}

std::uint32_t pack(const int c[3]) {
    return static_cast<std::uint32_t>(c[0]) | (static_cast<std::uint32_t>(c[1]) << 8) | (static_cast<std::uint32_t>(c[2]) << 16) | 0xFF000000u;
}

std::string rgb_text(const int c[3]) {
    return std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]);
}

}  // namespace

Outcome SwapChecks::model_matrix(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions base;
    base.width = 160;
    base.height = 160;
    if (!rig.open(base, error)) {
        return Outcome::fail(error);
    }
    rig.chain.reset();
    rig.window.destroy();
    Problems problems;
    const DXGI_SWAP_EFFECT effects[] = {DXGI_SWAP_EFFECT_DISCARD, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_FLIP_DISCARD};
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
    int created = 0;
    int shown = 0;
    for (DXGI_SWAP_EFFECT effect : effects) {
        for (UINT count = 1; count <= 8; ++count) {
            for (DXGI_FORMAT format : formats) {
                SwapOptions options = base;
                options.effect = effect;
                options.buffers = count;
                options.format = format;
                rig.chain.reset();
                TestWindow window;
                TestWindowConfig config;
                config.x = base.x;
                config.y = base.y;
                TestWindow::outer_size(config.x, config.y, 160, 160, config.width, config.height);
                if (!window.create(config, error)) {
                    return Outcome::fail(error);
                }
                Com<IDXGISwapChain1> chain;
                const HRESULT result = rig.try_create(options, window.hwnd(), chain);
                const bool flip_allowed = format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && count >= 2;
                const std::string label = std::string(effect_name(effect)) + " x" + std::to_string(count) + " format " + std::to_string(format);
                if (is_flip(effect)) {
                    if (flip_allowed != SUCCEEDED(result)) {
                        problems.add(label + ": creation " + (SUCCEEDED(result) ? "succeeded" : "failed with " + D3d11Tools::hr(result)) + " but the flip model rules say it " + (flip_allowed ? "must succeed" : "must fail"));
                    }
                } else if (FAILED(result) && (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM)) {
                    problems.add(label + ": a plain 8-bit bit-block-transfer chain is refused with " + D3d11Tools::hr(result));
                }
                if (FAILED(result)) {
                    continue;
                }
                ++created;
                DXGI_SWAP_CHAIN_DESC1 got{};
                chain->GetDesc1(&got);
                if (got.BufferCount != count || got.Format != format || got.Width != options.width) {
                    problems.add(label + ": GetDesc1 does not return the requested description");
                }
                if ((count == 1 || count == 2 || count == 8) && (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_R10G10B10A2_UNORM)) {
                    Com<ID3D11Texture2D> buffer;
                    chain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
                    UINT index = 0;
                    Com<IDXGISwapChain3> chain3 = chain.as<IDXGISwapChain3>();
                    if (chain3) {
                        index = chain3->GetCurrentBackBufferIndex();
                        buffer.reset();
                        chain->GetBuffer(index, __uuidof(ID3D11Texture2D), buffer.put_void());
                    }
                    Com<ID3D11RenderTargetView> view;
                    rig.device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put());
                    if (!view) {
                        problems.add(label + ": a render target view over the back buffer cannot be created");
                        continue;
                    }
                    D3d11Tools::clear(rig.device, view.get(), 0.2f, 0.4f, 0.6f, 1.0f);
                    const HRESULT presented = chain->Present(1, 0);
                    if (FAILED(presented)) {
                        problems.add(label + ": Present failed with " + D3d11Tools::hr(presented));
                        continue;
                    }
                    const bool linear = format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
                    const int want[3] = {linear ? encode(0.2) : 51, linear ? encode(0.4) : 102, linear ? encode(0.6) : 153};
                    POINT point{};
                    ::ClientToScreen(window.hwnd(), &(point = {80, 80}));
                    int got_rgb[3] = {};
                    bool matched = false;
                    for (int poll = 0; poll < 100 && !matched; ++poll) {
                        matched = SwapRig::screen_rgb(point.x, point.y, got_rgb) && std::abs(got_rgb[0] - want[0]) <= 4 && std::abs(got_rgb[1] - want[1]) <= 4 && std::abs(got_rgb[2] - want[2]) <= 4;
                        if (!matched) {
                            ::Sleep(20);
                        }
                    }
                    ++shown;
                    if (!matched) {
                        problems.add(label + ": the screen shows " + rgb_text(got_rgb) + ", expected " + rgb_text(want));
                    }
                }
            }
        }
    }
    context.measure("created", created);
    context.measure("displayed", shown);
    return problems.outcome();
}

Outcome SwapChecks::buffer_contents(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    options.buffers = 2;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    auto read_center = [&](UINT index, std::uint32_t& value) {
        Com<ID3D11Texture2D> buffer = rig.back_buffer(index);
        TexelImage image;
        if (!buffer || !D3d11Tools::read_texture(rig.device, buffer.get(), 0, image, error)) {
            return false;
        }
        value = image.pixel(64, 64);
        return true;
    };
    if (!rig.fill(1.0f, 0.0f, 0.0f, 1.0f, error) || FAILED(rig.chain->Present(0, 0))) {
        return Outcome::fail("the first frame cannot be presented");
    }
    std::uint32_t value = 0;
    if (!read_center(0, value)) {
        return Outcome::fail(error);
    }
    if (value != 0xFF0000FFu) {
        problems.add("a bit-block-transfer sequential chain does not keep the back buffer after Present: 0x" + Text::hex(value, 8));
    }
    for (DXGI_SWAP_EFFECT effect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
        SwapOptions flip = options;
        flip.effect = effect;
        flip.buffers = 3;
        if (!rig.create_chain(flip, error)) {
            problems.add(std::string(effect_name(effect)) + ": " + error);
            continue;
        }
        Com<IDXGISwapChain3> chain3 = rig.chain.as<IDXGISwapChain3>();
        if (!chain3) {
            problems.add("IDXGISwapChain3 is not available");
            continue;
        }
        UINT previous = chain3->GetCurrentBackBufferIndex();
        const int colors[6][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {0, 255, 255}, {255, 0, 255}};
        for (int frame = 0; frame < 6; ++frame) {
            if (!rig.fill(static_cast<float>(colors[frame][0]) / 255.0f, static_cast<float>(colors[frame][1]) / 255.0f, static_cast<float>(colors[frame][2]) / 255.0f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
                problems.add(std::string(effect_name(effect)) + ": frame " + std::to_string(frame) + " cannot be presented");
                break;
            }
            const UINT now = chain3->GetCurrentBackBufferIndex();
            if (now != (previous + 1) % 3) {
                problems.add(std::string(effect_name(effect)) + ": the back buffer index went from " + std::to_string(previous) + " to " + std::to_string(now) + " after a Present");
            }
            previous = now;
            int got[3] = {};
            if (!rig.wait_for_color(64, 64, colors[frame], 4, 3000, got)) {
                problems.add(std::string(effect_name(effect)) + ": frame " + std::to_string(frame) + " shows " + rgb_text(got) + " instead of " + rgb_text(colors[frame]));
                break;
            }
            if (effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
                std::uint32_t kept = 0;
                const UINT shown = (now + 2) % 3;
                if (read_center(shown, kept) && kept != pack(colors[frame])) {
                    problems.add("flip-sequential: the buffer that was presented does not keep its contents");
                }
            }
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome SwapChecks::present1(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 256;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    options.buffers = 2;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::vector<std::uint32_t> pattern(256 * 128);
    for (UINT y = 0; y < 128; ++y) {
        for (UINT x = 0; x < 256; ++x) {
            pattern[y * 256 + x] = 0xFF000000u | (x << 16) | (255 - x);
        }
    }
    auto shade = [](UINT x) { return std::array<int, 3>{static_cast<int>(x), 0, static_cast<int>(255 - x)}; };
    Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
    rig.device.context->UpdateSubresource(buffer.get(), 0, nullptr, pattern.data(), 256 * 4, 0);
    if (FAILED(rig.chain->Present(1, 0))) {
        return Outcome::fail("the first frame cannot be presented");
    }
    int got[3] = {};
    const int first[3] = {shade(50)[0], 0, shade(50)[2]};
    if (!rig.wait_for_color(50, 20, first, 3, 3000, got)) {
        problems.add("the first frame shows " + rgb_text(got) + " instead of " + rgb_text(first));
    }
    std::vector<std::uint32_t> green(32 * 128, 0xFF00FF00u);
    const D3D11_BOX strip{0, 0, 0, 32, 128, 1};
    rig.device.context->UpdateSubresource(buffer.get(), 0, &strip, green.data(), 32 * 4, 0);
    RECT dirty{0, 0, 32, 128};
    RECT scroll{0, 0, 96, 128};
    POINT offset{32, 0};
    DXGI_PRESENT_PARAMETERS parameters{1, &dirty, &scroll, &offset};
    rig.chain.get()->AddRef();
    Com<IDXGISwapChain1> chain(rig.chain.get());
    const HRESULT result = chain->Present1(1, 0, &parameters);
    if (FAILED(result)) {
        problems.add("Present1 with dirty and scroll rectangles failed: " + D3d11Tools::hr(result));
    } else {
        const int in_strip[3] = {0, 255, 0};
        const int scrolled[3] = {shade(18)[0], 0, shade(18)[2]};
        const int far_scrolled[3] = {shade(88)[0], 0, shade(88)[2]};
        const int untouched[3] = {shade(200)[0], 0, shade(200)[2]};
        if (!rig.wait_for_color(10, 20, in_strip, 4, 3000, got)) {
            problems.add("the dirty strip shows " + rgb_text(got) + " instead of green");
        }
        if (!rig.wait_for_color(50, 20, scrolled, 4, 1000, got)) {
            problems.add("the scrolled area at x=50 shows " + rgb_text(got) + " instead of the old column 18 " + rgb_text(scrolled));
        }
        if (!rig.wait_for_color(120, 20, far_scrolled, 4, 1000, got)) {
            problems.add("the scrolled area at x=120 shows " + rgb_text(got) + " instead of the old column 88");
        }
        if (!rig.wait_for_color(200, 20, untouched, 4, 1000, got)) {
            problems.add("pixels outside the dirty and scroll rectangles changed to " + rgb_text(got));
        }
    }
    RECT beyond{0, 0, 9999, 9999};
    DXGI_PRESENT_PARAMETERS bad{1, &beyond, nullptr, nullptr};
    if (SUCCEEDED(chain->Present1(1, 0, &bad))) {
        problems.add("a dirty rectangle beyond the back buffer is accepted");
    }
    RECT negative{-5, 0, 10, 10};
    DXGI_PRESENT_PARAMETERS bad_negative{1, &negative, nullptr, nullptr};
    if (SUCCEEDED(chain->Present1(1, 0, &bad_negative))) {
        problems.add("a dirty rectangle with a negative origin is accepted");
    }
    RECT inverted{50, 50, 10, 10};
    DXGI_PRESENT_PARAMETERS bad_inverted{1, &inverted, nullptr, nullptr};
    if (SUCCEEDED(chain->Present1(1, 0, &bad_inverted))) {
        problems.add("an inverted dirty rectangle is accepted");
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome SwapChecks::allow_tearing(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDXGIFactory5> factory5 = rig.factory.as<IDXGIFactory5>();
    BOOL supported = FALSE;
    const HRESULT queried = factory5 ? factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported)) : E_NOINTERFACE;
    if (FAILED(queried)) {
        supported = FALSE;
        context.note("CheckFeatureSupport(PRESENT_ALLOW_TEARING) is not available: " + D3d11Tools::hr(queried));
    }
    context.measure("tearing_supported", supported != FALSE);
    options.flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    std::string create_error;
    const bool created = rig.create_chain(options, create_error);
    if (created != (supported != FALSE)) {
        problems.add(std::string("tearing is ") + (supported ? "reported" : "not reported") + " but creating the flagged chain " + (created ? "succeeds" : "fails (" + create_error + ")"));
    }
    if (created) {
        if (!rig.fill(0.1f, 0.2f, 0.3f, 1.0f, error)) {
            return Outcome::fail(error);
        }
        const HRESULT tearing = rig.chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        if (FAILED(tearing)) {
            problems.add("Present(0, ALLOW_TEARING) failed on a chain created with the flag: " + D3d11Tools::hr(tearing));
        }
        if (SUCCEEDED(rig.chain->Present(1, DXGI_PRESENT_ALLOW_TEARING))) {
            problems.add("Present(1, ALLOW_TEARING) is accepted although tearing needs a sync interval of 0");
        }
    } else {
        options.flags = 0;
        if (!rig.create_chain(options, error)) {
            return Outcome::fail("a plain chain cannot be created: " + error);
        }
        if (!rig.fill(0.1f, 0.2f, 0.3f, 1.0f, error)) {
            return Outcome::fail(error);
        }
        if (SUCCEEDED(rig.chain->Present(0, DXGI_PRESENT_ALLOW_TEARING))) {
            problems.add("Present with ALLOW_TEARING is accepted on a chain created without the flag");
        }
    }
    return problems.outcome();
}

Outcome SwapChecks::do_not_wait(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.buffers = 2;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDXGISwapChain3> chain3 = rig.chain.as<IDXGISwapChain3>();
    int first_busy = -1;
    for (int i = 0; i < 200; ++i) {
        if (!rig.fill(0.5f, 0.0f, 0.5f, 1.0f, error)) {
            return Outcome::fail(error);
        }
        const HRESULT result = rig.chain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
            first_busy = i;
            break;
        }
        if (FAILED(result)) {
            return Outcome::fail("Present(DO_NOT_WAIT) returned " + D3d11Tools::hr(result));
        }
    }
    context.measure("first_still_drawing_present", first_busy);
    if (first_busy < 0) {
        problems.add("200 DO_NOT_WAIT presents never reported WAS_STILL_DRAWING");
    }
    ::Sleep(600);
    HRESULT after = DXGI_ERROR_WAS_STILL_DRAWING;
    for (int i = 0; i < 10 && after == DXGI_ERROR_WAS_STILL_DRAWING; ++i) {
        (void)rig.fill(0.0f, 0.5f, 0.5f, 1.0f, error);
        after = rig.chain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
        ::Sleep(100);
    }
    if (FAILED(after)) {
        problems.add("a DO_NOT_WAIT present still fails after the queue drained: " + D3d11Tools::hr(after));
    }
    UINT count_before = 0;
    UINT count_after = 0;
    rig.chain->GetLastPresentCount(&count_before);
    const UINT index_before = chain3 ? chain3->GetCurrentBackBufferIndex() : 0;
    const HRESULT test = rig.chain->Present(0, DXGI_PRESENT_TEST);
    rig.chain->GetLastPresentCount(&count_after);
    if (FAILED(test) && test != DXGI_STATUS_OCCLUDED) {
        problems.add("Present(TEST) returned " + D3d11Tools::hr(test));
    }
    if (count_after != count_before || (chain3 && chain3->GetCurrentBackBufferIndex() != index_before)) {
        problems.add("Present(TEST) advanced the present count or the back buffer index");
    }
    return problems.outcome();
}

Outcome SwapChecks::waitable_latency(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.buffers = 3;
    options.flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDXGISwapChain2> chain2 = rig.chain.as<IDXGISwapChain2>();
    if (!chain2) {
        return Outcome::fail("IDXGISwapChain2 is not available");
    }
    for (UINT latency : {1u, 2u, 3u}) {
        UINT back = 0;
        if (FAILED(chain2->SetMaximumFrameLatency(latency)) || FAILED(chain2->GetMaximumFrameLatency(&back)) || back != latency) {
            problems.add("SetMaximumFrameLatency(" + std::to_string(latency) + ") does not round trip, reads " + std::to_string(back));
        }
    }
    chain2->SetMaximumFrameLatency(2);
    HANDLE waitable = chain2->GetFrameLatencyWaitableObject();
    if (waitable == nullptr) {
        return Outcome::fail("no waitable object is returned");
    }
    int immediate = 0;
    while (immediate < 8 && ::WaitForSingleObject(waitable, 150) == WAIT_OBJECT_0) {
        ++immediate;
    }
    context.measure("signals_before_first_present", immediate);
    if (immediate != 2) {
        problems.add("the waitable object signalled " + std::to_string(immediate) + " times before any Present, expected the latency of 2");
    }
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 60; ++frame) {
        if (::WaitForSingleObject(waitable, 2000) != WAIT_OBJECT_0) {
            problems.add("frame " + std::to_string(frame) + ": the waitable object did not signal within 2 s while frames were presented");
            break;
        }
        if (!rig.fill(static_cast<float>(frame) / 60.0f, 0.2f, 0.4f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add("frame " + std::to_string(frame) + " cannot be presented");
            break;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    context.measure("sixty_frames_ms", static_cast<long long>(elapsed));
    if (elapsed > 15000) {
        problems.add("sixty frames took " + std::to_string(elapsed) + " ms");
    }
    return problems.outcome();
}

Outcome SwapChecks::frame_statistics(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.buffers = 2;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    ::GetMonitorInfoW(::MonitorFromWindow(rig.window.hwnd(), MONITOR_DEFAULTTOPRIMARY), &info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    double hz = 60.0;
    if (::EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
        hz = mode.dmDisplayFrequency;
    }
    UINT previous_count = 0;
    (void)rig.fill(0.1f, 0.1f, 0.1f, 1.0f, error);
    rig.chain->Present(1, 0);
    rig.chain->GetLastPresentCount(&previous_count);
    DXGI_FRAME_STATISTICS first{};
    bool have_first = false;
    const auto start = std::chrono::steady_clock::now();
    DXGI_FRAME_STATISTICS last{};
    constexpr int kFrames = 60;
    for (int i = 0; i < kFrames; ++i) {
        (void)rig.fill(static_cast<float>(i) / 60.0f, 0.1f, 0.1f, 1.0f, error);
        if (FAILED(rig.chain->Present(1, 0))) {
            return Outcome::fail("Present failed");
        }
        UINT count = 0;
        rig.chain->GetLastPresentCount(&count);
        if (count != previous_count + 1) {
            problems.add("GetLastPresentCount went from " + std::to_string(previous_count) + " to " + std::to_string(count) + " over one Present");
        }
        previous_count = count;
        DXGI_FRAME_STATISTICS stats{};
        if (SUCCEEDED(rig.chain->GetFrameStatistics(&stats))) {
            if (have_first && (stats.PresentCount < last.PresentCount || stats.SyncRefreshCount < last.SyncRefreshCount)) {
                problems.add("frame statistics decrease between two Present calls");
            }
            if (!have_first) {
                first = stats;
                have_first = true;
            }
            last = stats;
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!have_first) {
        problems.add("GetFrameStatistics never succeeds");
    } else {
        const double refreshes = static_cast<double>(last.SyncRefreshCount - first.SyncRefreshCount);
        const double expected = seconds * hz;
        context.measure("refreshes", refreshes);
        context.measure("expected_refreshes", expected);
        if (std::fabs(refreshes - expected) > std::max(5.0, expected * 0.2)) {
            problems.add("SyncRefreshCount advanced by " + std::to_string(refreshes) + " over " + std::to_string(seconds) + " s at " + std::to_string(hz) + " Hz, expected about " + std::to_string(expected));
        }
    }
    return problems.outcome();
}

Outcome SwapChecks::resize_buffers(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 200;
    options.height = 120;
    options.buffers = 3;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Texture2D> held = rig.back_buffer(0);
    const HRESULT busy = rig.chain->ResizeBuffers(0, 240, 140, DXGI_FORMAT_UNKNOWN, 0);
    if (busy != DXGI_ERROR_INVALID_CALL) {
        problems.add("ResizeBuffers with a live reference to a back buffer returned " + D3d11Tools::hr(busy) + " instead of DXGI_ERROR_INVALID_CALL");
    }
    held.reset();
    rig.device.context->ClearState();
    rig.device.context->Flush();
    const struct {
        UINT w, h, count;
        DXGI_FORMAT format;
    } steps[] = {{240, 140, 0, DXGI_FORMAT_UNKNOWN}, {320, 200, 4, DXGI_FORMAT_R8G8B8A8_UNORM}, {64, 64, 2, DXGI_FORMAT_B8G8R8A8_UNORM}, {200, 120, 3, DXGI_FORMAT_R16G16B16A16_FLOAT}};
    for (const auto& step : steps) {
        const HRESULT result = rig.chain->ResizeBuffers(step.count, step.w, step.h, step.format, 0);
        if (FAILED(result)) {
            problems.add("ResizeBuffers to " + std::to_string(step.w) + "x" + std::to_string(step.h) + " failed: " + D3d11Tools::hr(result));
            break;
        }
        DXGI_SWAP_CHAIN_DESC1 desc{};
        rig.chain->GetDesc1(&desc);
        if (desc.Width != step.w || desc.Height != step.h || (step.format != DXGI_FORMAT_UNKNOWN && desc.Format != step.format) || (step.count != 0 && desc.BufferCount != step.count)) {
            problems.add("after ResizeBuffers to " + std::to_string(step.w) + "x" + std::to_string(step.h) + " GetDesc1 reports " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height));
        }
        Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
        D3D11_TEXTURE2D_DESC td{};
        if (buffer) {
            buffer->GetDesc(&td);
        }
        if (td.Width != step.w || td.Height != step.h) {
            problems.add("the back buffer is " + std::to_string(td.Width) + "x" + std::to_string(td.Height) + " after resizing to " + std::to_string(step.w) + "x" + std::to_string(step.h));
        }
        buffer.reset();
        if (!rig.fill(0.3f, 0.6f, 0.9f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add("the chain does not render and present after resizing");
            break;
        }
        rig.device.context->ClearState();
        rig.device.context->Flush();
    }
    rig.chain->ResizeBuffers(3, 200, 120, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    DXGI_MODE_DESC target{};
    target.Width = 220;
    target.Height = 160;
    target.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    const HRESULT resized = rig.chain->ResizeTarget(&target);
    if (FAILED(resized)) {
        problems.add("ResizeTarget failed in windowed mode: " + D3d11Tools::hr(resized));
    } else {
        RECT client{};
        bool matched = false;
        for (int poll = 0; poll < 100 && !matched; ++poll) {
            ::GetClientRect(rig.window.hwnd(), &client);
            matched = client.right == 220 && client.bottom == 160;
            if (!matched) {
                ::Sleep(30);
            }
        }
        if (!matched) {
            problems.add("after ResizeTarget(220x160) the window client area is " + std::to_string(client.right) + "x" + std::to_string(client.bottom));
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome SwapChecks::fullscreen_transition(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 320;
    options.height = 240;
    options.buffers = 2;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<IDXGIOutput> output;
    if (FAILED(rig.chain->GetContainingOutput(output.put()))) {
        return Outcome::fail("the output containing the window cannot be found");
    }
    DXGI_OUTPUT_DESC od{};
    output->GetDesc(&od);
    DEVMODEW original{};
    original.dmSize = sizeof(original);
    ::EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &original);
    RECT window_before{};
    ::GetWindowRect(rig.window.hwnd(), &window_before);
    UINT count = 0;
    output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, nullptr);
    std::vector<DXGI_MODE_DESC> modes(count);
    output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, modes.data());
    DXGI_MODE_DESC chosen{};
    chosen.Width = original.dmPelsWidth;
    chosen.Height = original.dmPelsHeight;
    chosen.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    for (const DXGI_MODE_DESC& m : modes) {
        if (m.Width >= 800 && m.Width < chosen.Width && m.Height >= 600 && m.Height < chosen.Height) {
            chosen = m;
        }
    }
    context.measure("requested_mode", std::to_string(chosen.Width) + "x" + std::to_string(chosen.Height));
    const HRESULT entered = rig.chain->SetFullscreenState(TRUE, nullptr);
    if (FAILED(entered)) {
        return Outcome::fail("SetFullscreenState(TRUE) failed: " + D3d11Tools::hr(entered));
    }
    BOOL state = FALSE;
    rig.chain->GetFullscreenState(&state, nullptr);
    if (state != TRUE) {
        problems.add("GetFullscreenState is not TRUE after entering fullscreen");
    }
    if (FAILED(rig.chain->ResizeTarget(&chosen))) {
        problems.add("ResizeTarget failed in fullscreen");
    }
    DEVMODEW now{};
    now.dmSize = sizeof(now);
    bool matched = false;
    for (int poll = 0; poll < 60 && !matched; ++poll) {
        ::EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &now);
        matched = now.dmPelsWidth == chosen.Width && now.dmPelsHeight == chosen.Height;
        if (!matched) {
            ::Sleep(50);
        }
    }
    if (!matched) {
        problems.add("the display mode is " + std::to_string(now.dmPelsWidth) + "x" + std::to_string(now.dmPelsHeight) + " after requesting " + std::to_string(chosen.Width) + "x" + std::to_string(chosen.Height));
    }
    rig.device.context->ClearState();
    if (SUCCEEDED(rig.chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH))) {
        if (!rig.fill(0.2f, 0.4f, 0.6f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add("the fullscreen chain cannot present");
        }
    }
    const HRESULT left = rig.chain->SetFullscreenState(FALSE, nullptr);
    if (FAILED(left)) {
        problems.add("SetFullscreenState(FALSE) failed: " + D3d11Tools::hr(left));
    }
    rig.chain->GetFullscreenState(&state, nullptr);
    if (state != FALSE) {
        problems.add("GetFullscreenState is not FALSE after leaving fullscreen");
    }
    matched = false;
    for (int poll = 0; poll < 100 && !matched; ++poll) {
        ::EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &now);
        matched = now.dmPelsWidth == original.dmPelsWidth && now.dmPelsHeight == original.dmPelsHeight;
        if (!matched) {
            ::Sleep(50);
        }
    }
    if (!matched) {
        problems.add("the desktop mode is " + std::to_string(now.dmPelsWidth) + "x" + std::to_string(now.dmPelsHeight) + " after leaving fullscreen, it was " + std::to_string(original.dmPelsWidth) + "x" + std::to_string(original.dmPelsHeight));
    }
    RECT window_after{};
    matched = false;
    for (int poll = 0; poll < 60 && !matched; ++poll) {
        ::GetWindowRect(rig.window.hwnd(), &window_after);
        matched = window_after.left == window_before.left && window_after.top == window_before.top && window_after.right == window_before.right && window_after.bottom == window_before.bottom;
        if (!matched) {
            ::Sleep(50);
        }
    }
    if (!matched) {
        problems.add("the window rectangle is not restored after leaving fullscreen");
    }
    return problems.outcome();
}

Outcome SwapChecks::hdr_colorspace(Context& context) {
    Problems problems;
    for (DXGI_FORMAT format : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM}) {
        SwapRig rig;
        std::string error;
        SwapOptions options;
        options.width = 128;
        options.height = 128;
        options.format = format;
        options.buffers = 2;
        options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (!rig.open(options, error)) {
            problems.add("format " + std::to_string(format) + ": " + error);
            continue;
        }
        Com<IDXGISwapChain3> chain3 = rig.chain.as<IDXGISwapChain3>();
        if (!chain3) {
            problems.add("IDXGISwapChain3 is not available");
            continue;
        }
        int accepted = 0;
        for (int space = 0; space <= 25; ++space) {
            UINT support = 0;
            const DXGI_COLOR_SPACE_TYPE type = static_cast<DXGI_COLOR_SPACE_TYPE>(space);
            if (FAILED(chain3->CheckColorSpaceSupport(type, &support))) {
                continue;
            }
            const bool present = (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
            const HRESULT result = chain3->SetColorSpace1(type);
            if (present && FAILED(result)) {
                problems.add("format " + std::to_string(format) + ": colour space " + std::to_string(space) + " is reported presentable but SetColorSpace1 returned " + D3d11Tools::hr(result));
            } else if (!present && SUCCEEDED(result)) {
                problems.add("format " + std::to_string(format) + ": colour space " + std::to_string(space) + " is not reported presentable but SetColorSpace1 accepts it");
            } else if (!present && result != E_INVALIDARG) {
                problems.add("format " + std::to_string(format) + ": colour space " + std::to_string(space) + " is refused with " + D3d11Tools::hr(result) + " instead of E_INVALIDARG");
            }
            if (present && SUCCEEDED(result)) {
                ++accepted;
                const UINT index = chain3->GetCurrentBackBufferIndex();
                Com<ID3D11Texture2D> buffer = rig.back_buffer(index);
                Com<ID3D11RenderTargetView> view = rig.back_view(index);
                D3d11Tools::clear(rig.device, view.get(), 0.25f, 0.5f, 1.0f, 1.0f);
                TexelImage image;
                if (!D3d11Tools::read_texture(rig.device, buffer.get(), 0, image, error)) {
                    problems.add(error);
                    continue;
                }
                const FormatSpec spec = FormatCodec::spec(format);
                const double want[4] = {0.25, 0.5, 1.0, 1.0};
                std::string why;
                if (!FormatCodec::matches(spec, image.data.data() + static_cast<std::size_t>(spec.bytes) * (64 * 128 + 64), want, why)) {
                    problems.add("format " + std::to_string(format) + " colour space " + std::to_string(space) + ": the written colour does not read back: " + why);
                }
                view.reset();
                buffer.reset();
                if (FAILED(rig.chain->Present(1, 0))) {
                    problems.add("Present failed after SetColorSpace1(" + std::to_string(space) + ")");
                }
            }
        }
        context.measure("accepted_spaces_" + std::to_string(format), accepted);
        if (SUCCEEDED(chain3->SetColorSpace1(static_cast<DXGI_COLOR_SPACE_TYPE>(0x7777)))) {
            problems.add("SetColorSpace1 accepts an undefined colour space");
        }
    }
    return problems.outcome();
}

Outcome SwapChecks::getdc(Context& context) {
    Problems problems;
    for (DXGI_SWAP_EFFECT effect : {DXGI_SWAP_EFFECT_SEQUENTIAL, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL}) {
        SwapRig rig;
        std::string error;
        SwapOptions options;
        options.width = 128;
        options.height = 128;
        options.buffers = 2;
        options.effect = effect;
        options.flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
        if (!rig.open(options, error)) {
            problems.add(std::string(effect_name(effect)) + ": " + error);
            continue;
        }
        (void)rig.fill(0, 0, 0, 1, error);
        Com<IDXGISurface1> surface;
        rig.chain->GetBuffer(0, __uuidof(IDXGISurface1), surface.put_void());
        HDC dc = nullptr;
        if (!surface || FAILED(surface->GetDC(FALSE, &dc)) || dc == nullptr) {
            problems.add(std::string(effect_name(effect)) + ": a GDI compatible buffer yields no device context");
            continue;
        }
        HBRUSH brush = ::CreateSolidBrush(RGB(200, 30, 90));
        const RECT area{10, 10, 60, 60};
        ::FillRect(dc, &area, brush);
        ::DeleteObject(brush);
        surface->ReleaseDC(nullptr);
        surface.reset();
        Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
        TexelImage image;
        if (buffer && D3d11Tools::read_texture(rig.device, buffer.get(), 0, image, error)) {
            if ((image.pixel(30, 30) & 0xFFFFFFu) != 0xC81E5Au) {
                problems.add(std::string(effect_name(effect)) + ": the GDI fill reads back as 0x" + Text::hex(image.pixel(30, 30) & 0xFFFFFFu, 6) + " instead of 0xC81E5A");
            }
            if ((image.pixel(100, 100) & 0xFFFFFFu) != 0u) {
                problems.add(std::string(effect_name(effect)) + ": GDI drawing leaked outside the filled rectangle");
            }
        }
        buffer.reset();
        if (FAILED(rig.chain->Present(1, 0))) {
            problems.add(std::string(effect_name(effect)) + ": Present failed after GDI drawing");
            continue;
        }
        const int want[3] = {200, 30, 90};
        int got[3] = {};
        if (!rig.wait_for_color(30, 30, want, 4, 3000, got)) {
            problems.add(std::string(effect_name(effect)) + ": the screen shows " + rgb_text(got) + " where GDI drew " + rgb_text(want));
        }
    }
    {
        SwapRig plain;
        std::string error;
        SwapOptions options;
        options.width = 64;
        options.height = 64;
        options.effect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        options.buffers = 2;
        if (plain.open(options, error)) {
            Com<IDXGISurface1> surface;
            plain.chain->GetBuffer(0, __uuidof(IDXGISurface1), surface.put_void());
            HDC dc = nullptr;
            const HRESULT result = surface ? surface->GetDC(FALSE, &dc) : E_FAIL;
            if (SUCCEEDED(result)) {
                problems.add("GetDC succeeds on a chain created without GDI_COMPATIBLE");
                surface->ReleaseDC(nullptr);
            } else if (result != DXGI_ERROR_INVALID_CALL) {
                problems.add("GetDC on a non-compatible chain returned " + D3d11Tools::hr(result) + " instead of DXGI_ERROR_INVALID_CALL");
            }
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome SwapChecks::background_alpha_composition(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 160;
    options.height = 160;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    DynLibrary library;
    using CreateDevice = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);
    CreateDevice create = library.load("dcomp.dll") ? library.symbol<CreateDevice>("DCompositionCreateDevice") : nullptr;
    if (create == nullptr) {
        return Context::skip("dcomp.dll is not available, so DirectComposition is not installed");
    }
    Com<IDXGIDevice> dxgi_device;
    rig.device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    Com<IDCompositionDevice> dcomp;
    if (FAILED(create(dxgi_device.get(), __uuidof(IDCompositionDevice), dcomp.put_void()))) {
        return Context::skip("DCompositionCreateDevice fails, so DirectComposition is not available");
    }
    Problems problems;
    rig.chain.reset();
    auto make = [&](UINT size, DXGI_ALPHA_MODE alpha, Com<IDXGISwapChain1>& out) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = size;
        desc.Height = size;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = alpha;
        desc.Scaling = DXGI_SCALING_STRETCH;
        return rig.factory->CreateSwapChainForComposition(rig.device.device, &desc, nullptr, out.put());
    };
    Com<IDXGISwapChain1> bottom;
    Com<IDXGISwapChain1> top;
    if (FAILED(make(160, DXGI_ALPHA_MODE_IGNORE, bottom)) || FAILED(make(64, DXGI_ALPHA_MODE_PREMULTIPLIED, top))) {
        return Outcome::fail("CreateSwapChainForComposition fails although DirectComposition is available");
    }
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual> root;
    Com<IDCompositionVisual> overlay;
    if (FAILED(dcomp->CreateTargetForHwnd(rig.window.hwnd(), TRUE, target.put())) || FAILED(dcomp->CreateVisual(root.put())) || FAILED(dcomp->CreateVisual(overlay.put()))) {
        return Outcome::fail("the composition visual tree cannot be created");
    }
    root->SetContent(bottom.get());
    overlay->SetContent(top.get());
    overlay->SetOffsetX(48.0f);
    overlay->SetOffsetY(48.0f);
    root->AddVisual(overlay.get(), TRUE, nullptr);
    target->SetRoot(root.get());
    auto paint = [&](IDXGISwapChain1* chain, float r, float g, float b, float a) {
        Com<ID3D11Texture2D> buffer;
        chain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
        Com<ID3D11RenderTargetView> view;
        rig.device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put());
        D3d11Tools::clear(rig.device, view.get(), r, g, b, a);
        rig.device.context->Flush();
        return chain->Present(1, 0);
    };
    if (FAILED(paint(bottom.get(), 0.0f, 0.0f, 1.0f, 1.0f)) || FAILED(paint(top.get(), 0.5f, 0.0f, 0.0f, 0.5f))) {
        return Outcome::fail("the composition chains cannot present");
    }
    dcomp->Commit();
    const int blended[3] = {128, 0, 128};
    const int backdrop[3] = {0, 0, 255};
    int got[3] = {};
    if (!rig.wait_for_color(80, 80, blended, 4, 4000, got)) {
        problems.add("a premultiplied (0.5, 0, 0, 0.5) pixel over blue composites to " + rgb_text(got) + " instead of " + rgb_text(blended));
    }
    if (!rig.wait_for_color(10, 10, backdrop, 4, 1000, got)) {
        problems.add("the opaque backdrop shows " + rgb_text(got) + " instead of blue");
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome SwapChecks::multiple_and_orphan(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    {
        TestWindow second;
        TestWindowConfig config;
        config.x = 300;
        config.y = 80;
        if (!second.create(config, error)) {
            return Outcome::fail(error);
        }
        Com<IDXGISwapChain1> other;
        const HRESULT result = rig.try_create(options, second.hwnd(), other);
        if (FAILED(result)) {
            problems.add("a second chain on a second window of the same device fails: " + D3d11Tools::hr(result));
        } else {
            Com<ID3D11Texture2D> buffer;
            other->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
            Com<ID3D11RenderTargetView> view;
            rig.device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put());
            D3d11Tools::clear(rig.device, view.get(), 1, 0, 0, 1);
            if (FAILED(other->Present(1, 0)) || !rig.fill(0, 1, 0, 1, error) || FAILED(rig.chain->Present(1, 0))) {
                problems.add("two chains on one device do not both present");
            }
        }
    }
    {
        Com<IDXGISwapChain1> duplicate;
        const HRESULT result = rig.try_create(options, rig.window.hwnd(), duplicate);
        context.measure("second_chain_same_window", D3d11Tools::hr(result));
        if (FAILED(result) && result != DXGI_ERROR_INVALID_CALL) {
            problems.add("a second chain on the same window fails with " + D3d11Tools::hr(result) + " instead of DXGI_ERROR_INVALID_CALL");
        }
        duplicate.reset();
        if (!rig.fill(0.5f, 0.5f, 0.5f, 1, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add("the first chain stops working after a second chain was attempted on its window");
        }
    }
    {
        rig.chain.get()->AddRef();
        Com<IDXGISwapChain1> chain(rig.chain.get());
        auto future = std::async(std::launch::async, [raw = chain.get()] { return raw->Present(0, 0); });
        if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            return Outcome::fail("a Present from another thread did not return within 15 s");
        }
        const HRESULT result = future.get();
        if (FAILED(result) && result != DXGI_ERROR_INVALID_CALL) {
            problems.add("Present from another thread returned " + D3d11Tools::hr(result));
        }
    }
    rig.window.destroy();
    {
        rig.chain.get()->AddRef();
        Com<IDXGISwapChain1> chain(rig.chain.get());
        auto future = std::async(std::launch::async, [raw = chain.get()] {
            HRESULT last = S_OK;
            for (int i = 0; i < 3; ++i) {
                last = raw->Present(1, 0);
            }
            return last;
        });
        if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            return Outcome::fail("Present on a chain whose window was destroyed hangs");
        }
        const HRESULT result = future.get();
        context.measure("present_after_window_destroyed", D3d11Tools::hr(result));
        if (FAILED(result) && result != DXGI_ERROR_INVALID_CALL && result != DXGI_ERROR_DEVICE_REMOVED) {
            problems.add("Present after the window was destroyed returned the undocumented " + D3d11Tools::hr(result));
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::dxgi
