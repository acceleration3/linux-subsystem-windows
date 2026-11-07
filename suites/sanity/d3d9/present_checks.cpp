#include "sanity/d3d9/present_checks.hpp"

#include "sanity/d3d9/kit.hpp"

#include <chrono>
#include <cstring>
#include <vector>

namespace lsw::suites::sanity::d3d9 {

namespace {

void rgb_of(std::uint32_t colour, int out[3]) {
    out[0] = static_cast<int>((colour >> 16) & 255u);
    out[1] = static_cast<int>((colour >> 8) & 255u);
    out[2] = static_cast<int>(colour & 255u);
}

bool screen_shows(D3d9Rig& rig, int x, int y, std::uint32_t colour, int timeout_ms = 2500) {
    int want[3];
    int got[3] = {0, 0, 0};
    rgb_of(colour, want);
    return D3d9Tools::wait_screen_color(rig.window.hwnd(), x, y, want, 2, timeout_ms, got);
}

std::string screen_text(D3d9Rig& rig, int x, int y) {
    POINT point{x, y};
    ::ClientToScreen(rig.window.hwnd(), &point);
    HDC screen = ::GetDC(nullptr);
    const COLORREF color = screen != nullptr ? ::GetPixel(screen, point.x, point.y) : CLR_INVALID;
    if (screen != nullptr) {
        ::ReleaseDC(nullptr, screen);
    }
    return "(" + std::to_string(GetRValue(color)) + "," + std::to_string(GetGValue(color)) + "," + std::to_string(GetBValue(color)) + ")";
}

constexpr DWORD kDoNotFlip = 0x00000004;
constexpr DWORD kForceImmediate = 0x00000100;

const char* effect_name(D3DSWAPEFFECT effect) {
    switch (effect) {
    case D3DSWAPEFFECT_DISCARD: return "DISCARD";
    case D3DSWAPEFFECT_FLIP: return "FLIP";
    case D3DSWAPEFFECT_COPY: return "COPY";
    case D3DSWAPEFFECT_FLIPEX: return "FLIPEX";
    default: return "other";
    }
}

}  // namespace

Outcome PresentChecks::swap_effects(Context& context) {
    Problems problems;
    int presented = 0;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        D3d9Settings settings;
        settings.depth = false;
        settings.width = 160;
        settings.height = 120;
        if (!rig.open(ex, error)) {
            if (ex) {
                context.note("Direct3D 9Ex is unavailable: " + error);
                continue;
            }
            return Outcome::fail(error);
        }
        if (!rig.make_window(settings, error)) {
            return Outcome::fail(error);
        }
        std::vector<D3DSWAPEFFECT> effects = {D3DSWAPEFFECT_DISCARD, D3DSWAPEFFECT_COPY};
        if (!ex) {
            effects.push_back(D3DSWAPEFFECT_FLIP);
        } else {
            effects.push_back(D3DSWAPEFFECT_FLIPEX);
        }
        for (const D3DSWAPEFFECT effect : effects) {
            for (UINT buffers = 1; buffers <= 3; ++buffers) {
                const std::string name = std::string("D3D") + Kit::label(ex) + " " + effect_name(effect) + " with " + std::to_string(buffers) + " back buffers";
                settings.effect = effect;
                settings.buffers = buffers;
                const HRESULT created = rig.try_create(settings);
                if (FAILED(created)) {
                    if ((effect == D3DSWAPEFFECT_FLIPEX && buffers == 1) || (effect == D3DSWAPEFFECT_COPY && buffers > 1 && created == D3DERR_INVALIDCALL)) {
                        context.note(name + " is rejected by CreateDevice: " + Kit::hr(created));
                        continue;
                    }
                    problems.add(name + ": CreateDevice failed with " + Kit::hr(created));
                    continue;
                }
                IDirect3DDevice9* device = rig.dev();
                std::uint32_t last = 0;
                for (int frame = 0; frame < 4; ++frame) {
                    last = Kit::frame_color(frame * 7 + static_cast<int>(buffers));
                    D3d9Tools::clear(device, last);
                    const HRESULT result = device->Present(nullptr, nullptr, nullptr, nullptr);
                    if (FAILED(result)) {
                        problems.add(name + ": Present failed with " + Kit::hr(result));
                        break;
                    }
                    ++presented;
                    if (effect == D3DSWAPEFFECT_COPY) {
                        Com<IDirect3DSurface9> back;
                        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put());
                        SceneImage image;
                        std::string where;
                        if (back && D3d9Tools::read_surface(device, back.get(), image, error)) {
                            if (!D3d9Tools::uniform(image, last | 0xFF000000u, 0, where, true)) {
                                problems.add(name + ": the back buffer does not keep its contents after Present: " + where);
                                break;
                            }
                        }
                    }
                    if (!screen_shows(rig, 80, 60, last)) {
                        problems.add(name + ": frame " + std::to_string(frame) + " is not shown, the window reads " + screen_text(rig, 80, 60) + " instead of " + D3d9Tools::color_text(last));
                        break;
                    }
                }
                if (effect != D3DSWAPEFFECT_COPY && buffers > 1) {
                    for (UINT i = 0; i < buffers; ++i) {
                        Com<IDirect3DSurface9> back;
                        if (FAILED(device->GetBackBuffer(0, i, D3DBACKBUFFER_TYPE_MONO, back.put()))) {
                            problems.add(name + ": GetBackBuffer(" + std::to_string(i) + ") failed");
                        }
                    }
                }
                D3DPRESENT_PARAMETERS pp{};
                Com<IDirect3DSwapChain9> chain;
                if (SUCCEEDED(device->GetSwapChain(0, chain.put())) && SUCCEEDED(chain->GetPresentParameters(&pp))) {
                    if (pp.SwapEffect != effect || pp.BackBufferCount != buffers) {
                        problems.add(name + ": the swap chain reports effect " + std::to_string(pp.SwapEffect) + " with " + std::to_string(pp.BackBufferCount) + " buffers");
                    }
                }
                Kit::check_usable(problems, name, device);
                rig.close_device();
            }
        }
    }
    context.measure("presents", presented);
    return problems.outcome();
}

Outcome PresentChecks::intervals(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(false, error) || !rig.make_window(settings, error)) {
        return Outcome::fail(error);
    }
    D3DCAPS9 caps{};
    static_cast<void>(rig.caps(caps));
    D3DDISPLAYMODE mode{};
    rig.d3d->GetAdapterDisplayMode(rig.ordinal, &mode);
    const double refresh = mode.RefreshRate > 1 ? static_cast<double>(mode.RefreshRate) : 60.0;
    const double period_ms = 1000.0 / refresh;
    context.measure("refresh_hz", refresh);
    constexpr int kPresents = 40;
    double one_ms = 0;
    double immediate_ms = 0;
    Problems problems;
    for (const UINT interval : {static_cast<UINT>(D3DPRESENT_INTERVAL_ONE), static_cast<UINT>(D3DPRESENT_INTERVAL_IMMEDIATE)}) {
        const bool one = interval == D3DPRESENT_INTERVAL_ONE;
        if ((caps.PresentationIntervals & interval) == 0) {
            context.note(std::string(one ? "INTERVAL_ONE" : "INTERVAL_IMMEDIATE") + " is not in PresentationIntervals");
            continue;
        }
        settings.interval = interval;
        const HRESULT created = rig.try_create(settings);
        if (FAILED(created)) {
            problems.add(std::string(one ? "INTERVAL_ONE" : "INTERVAL_IMMEDIATE") + ": CreateDevice failed with " + Kit::hr(created));
            continue;
        }
        IDirect3DDevice9* device = rig.dev();
        for (int warm = 0; warm < 3; ++warm) {
            D3d9Tools::clear(device, Kit::frame_color(warm));
            device->Present(nullptr, nullptr, nullptr, nullptr);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kPresents; ++i) {
            D3d9Tools::clear(device, Kit::frame_color(i));
            const HRESULT result = device->Present(nullptr, nullptr, nullptr, nullptr);
            if (FAILED(result)) {
                problems.add("Present failed with " + Kit::hr(result));
                break;
            }
        }
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        (one ? one_ms : immediate_ms) = elapsed;
        context.measure(std::string(one ? "interval_one" : "interval_immediate") + ".total_ms", elapsed);
        rig.close_device();
    }
    if (one_ms > 0) {
        const double minimum = (kPresents - 1) * period_ms * 0.8;
        if (one_ms < minimum) {
            problems.add(std::to_string(kPresents) + " presents with INTERVAL_ONE took " + std::to_string(one_ms) + " ms, less than " + std::to_string(minimum) + " ms for " + std::to_string(refresh) + " Hz");
        }
    }
    if (one_ms > 0 && immediate_ms > 0 && immediate_ms > one_ms * 1.05) {
        problems.add("INTERVAL_IMMEDIATE (" + std::to_string(immediate_ms) + " ms) is slower than INTERVAL_ONE (" + std::to_string(one_ms) + " ms)");
    }
    if (one_ms == 0 && immediate_ms == 0) {
        return Context::skip("neither presentation interval is in the caps");
    }
    return problems.outcome();
}

Outcome PresentChecks::presentex_flags(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.make_window(settings, error)) {
        return Outcome::fail(error);
    }
    settings.effect = D3DSWAPEFFECT_FLIPEX;
    settings.buffers = 2;
    if (FAILED(rig.try_create(settings))) {
        settings.effect = D3DSWAPEFFECT_DISCARD;
        settings.buffers = 1;
        context.note("FLIPEX with two buffers is not available, using DISCARD");
        const HRESULT created = rig.try_create(settings);
        if (FAILED(created)) {
            return Outcome::fail("CreateDeviceEx failed with " + Kit::hr(created));
        }
    }
    IDirect3DDevice9Ex* device = rig.device_ex.get();
    D3DCAPS9 caps{};
    static_cast<void>(rig.caps(caps));
    Problems problems;
    Com<IDirect3DSwapChain9> base_chain;
    device->GetSwapChain(0, base_chain.put());
    Com<IDirect3DSwapChain9Ex> chain = base_chain.as<IDirect3DSwapChain9Ex>();
    if (!chain) {
        return Outcome::fail("the swap chain of a 9Ex device does not expose IDirect3DSwapChain9Ex");
    }
    UINT previous = 0;
    const HRESULT initial_count = chain->GetLastPresentCount(&previous);
    if (settings.effect == D3DSWAPEFFECT_FLIPEX) {
        Kit::expect_success(problems, "GetLastPresentCount", initial_count);
    } else {
        context.measure("initial_get_last_present_count", Kit::hr(initial_count));
    }
    UINT first_count = previous;
    UINT64 previous_stats = 0;
    bool stats_seen = false;
    for (int i = 0; i < 12; ++i) {
        D3d9Tools::clear(rig.dev(), Kit::frame_color(i));
        const HRESULT result = device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
        if (FAILED(result)) {
            problems.add("PresentEx failed with " + Kit::hr(result));
            break;
        }
        UINT count = 0;
        chain->GetLastPresentCount(&count);
        if (count < previous) {
            problems.add("GetLastPresentCount went backwards: " + std::to_string(previous) + " then " + std::to_string(count));
        }
        previous = count;
        D3DPRESENTSTATS stats{};
        const HRESULT stats_result = chain->GetPresentStats(&stats);
        if (stats_result == D3D_OK) {
            if (stats_seen && stats.PresentCount < previous_stats) {
                problems.add("PresentCount in GetPresentStats went backwards");
            }
            previous_stats = stats.PresentCount;
            stats_seen = true;
        } else if (stats_result != D3DERR_INVALIDCALL && stats_result != D3DERR_PRESENT_STATISTICS_DISJOINT && stats_result != D3DERR_WASSTILLDRAWING) {
            problems.add("GetPresentStats returned " + Kit::hr(stats_result));
        }
    }
    context.measure("present_count_delta", static_cast<long long>(previous) - static_cast<long long>(first_count));
    if (settings.effect == D3DSWAPEFFECT_FLIPEX && previous <= first_count) {
        problems.add("GetLastPresentCount did not advance over 12 presents (" + std::to_string(first_count) + " to " + std::to_string(previous) + ")");
    }
    int still_drawing = 0;
    for (int i = 0; i < 60; ++i) {
        D3d9Tools::clear(rig.dev(), Kit::frame_color(i + 30));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        HRESULT result = S_OK;
        while ((result = device->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT)) == D3DERR_WASSTILLDRAWING) {
            ++still_drawing;
            if (std::chrono::steady_clock::now() > deadline) {
                break;
            }
            ::Sleep(1);
        }
        if (result != S_OK && result != S_PRESENT_OCCLUDED && result != S_PRESENT_MODE_CHANGED) {
            problems.add("PresentEx(DONOTWAIT) returned " + Kit::hr(result) + " instead of success after retries");
            break;
        }
    }
    context.measure("was_still_drawing", still_drawing);
    const struct {
        const char* name;
        DWORD flag;
    } flags[] = {{"DONOTFLIP", kDoNotFlip}, {"FORCEIMMEDIATE", kForceImmediate}, {"LINEAR_CONTENT", D3DPRESENT_LINEAR_CONTENT}};
    for (const auto& f : flags) {
        D3d9Tools::clear(rig.dev(), 0xFF204060u);
        const HRESULT result = device->PresentEx(nullptr, nullptr, nullptr, nullptr, f.flag);
        context.measure(std::string("flag.") + f.name, Kit::hr(result));
        const bool allowed = result == S_OK || result == S_PRESENT_OCCLUDED || result == S_PRESENT_MODE_CHANGED || result == D3DERR_INVALIDCALL;
        if (!allowed) {
            problems.add(std::string("PresentEx(") + f.name + ") returned " + Kit::hr(result));
        }
        if (f.flag == D3DPRESENT_LINEAR_CONTENT && (caps.Caps3 & D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION) != 0 && result != S_OK && result != S_PRESENT_OCCLUDED && result != S_PRESENT_MODE_CHANGED) {
            problems.add("PresentEx(LINEAR_CONTENT) fails with " + Kit::hr(result) + " although the caps report D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION");
        }
    }
    const HRESULT state = device->CheckDeviceState(rig.window.hwnd());
    if (state != S_OK && state != S_PRESENT_OCCLUDED && state != S_PRESENT_MODE_CHANGED) {
        problems.add("CheckDeviceState returned " + Kit::hr(state));
    }
    Kit::check_usable(problems, "after the presents", rig.dev());
    return problems.outcome();
}

Outcome PresentChecks::src_dst_rects(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings window_settings;
    window_settings.depth = false;
    window_settings.width = 240;
    window_settings.height = 200;
    if (!rig.open(false, error) || !rig.make_window(window_settings, error)) {
        return Outcome::fail(error);
    }
    D3d9Settings settings = window_settings;
    settings.width = 64;
    settings.height = 64;
    settings.effect = D3DSWAPEFFECT_COPY;
    HRESULT created = rig.try_create(settings);
    if (FAILED(created)) {
        return Outcome::fail("CreateDevice with a COPY swap chain failed with " + Kit::hr(created));
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Com<IDirect3DSurface9> back;
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put());
    const std::uint32_t colours[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u};
    const RECT quadrants[4] = {{0, 0, 32, 32}, {32, 0, 64, 32}, {0, 32, 32, 64}, {32, 32, 64, 64}};
    for (int i = 0; i < 4; ++i) {
        device->ColorFill(back.get(), &quadrants[i], colours[i]);
    }
    const RECT destinations[2] = {{100, 100, 164, 164}, {20, 90, 52, 122}};
    const int sources[2] = {0, 3};
    for (int pass = 0; pass < 2; ++pass) {
        const RECT& destination = destinations[pass];
        const std::uint32_t expected = colours[sources[pass]];
        const HRESULT result = device->Present(&quadrants[sources[pass]], &destination, nullptr, nullptr);
        if (FAILED(result)) {
            problems.add("Present with a source and destination rect failed with " + Kit::hr(result));
            continue;
        }
        const int cx = static_cast<int>((destination.left + destination.right) / 2);
        const int cy = static_cast<int>((destination.top + destination.bottom) / 2);
        if (!screen_shows(rig, cx, cy, expected)) {
            problems.add("the destination rect centre shows " + screen_text(rig, cx, cy) + " instead of the source quadrant colour " + D3d9Tools::color_text(expected));
            continue;
        }
        bool exact = true;
        const int probes[4][2] = {{static_cast<int>(destination.left) + 1, static_cast<int>(destination.top) + 1},
                                  {static_cast<int>(destination.right) - 2, static_cast<int>(destination.top) + 1},
                                  {static_cast<int>(destination.left) + 1, static_cast<int>(destination.bottom) - 2},
                                  {static_cast<int>(destination.right) - 2, static_cast<int>(destination.bottom) - 2}};
        for (const auto& p : probes) {
            exact = exact && screen_shows(rig, p[0], p[1], expected, 500);
        }
        if (!exact) {
            problems.add("the corners of the destination rect do not all show the source quadrant colour");
        }
        const int outside[2][2] = {{static_cast<int>(destination.left) - 3, static_cast<int>(destination.top) - 3}, {static_cast<int>(destination.right) + 2, static_cast<int>(destination.bottom) + 2}};
        for (const auto& p : outside) {
            if (screen_shows(rig, p[0], p[1], expected, 100)) {
                problems.add("a pixel outside the destination rect at (" + std::to_string(p[0]) + "," + std::to_string(p[1]) + ") shows the presented colour");
            }
        }
    }
    D3d9Settings full = window_settings;
    full.effect = D3DSWAPEFFECT_COPY;
    created = rig.try_create(full);
    if (FAILED(created)) {
        return Outcome::fail("CreateDevice with a client-size COPY swap chain failed with " + Kit::hr(created));
    }
    device = rig.dev();
    D3d9Tools::clear(device, 0xFFFF0000u);
    Kit::expect_success(problems, "the full Present", device->Present(nullptr, nullptr, nullptr, nullptr));
    if (!screen_shows(rig, 150, 150, 0xFFFF0000u)) {
        problems.add("the full Present is not shown: " + screen_text(rig, 150, 150));
    }
    D3d9Tools::clear(device, 0xFF0000FFu);
    struct {
        RGNDATAHEADER header;
        RECT rect;
    } region{};
    region.rect = {20, 20, 60, 60};
    region.header.dwSize = sizeof(RGNDATAHEADER);
    region.header.iType = RDH_RECTANGLES;
    region.header.nCount = 1;
    region.header.nRgnSize = sizeof(RECT);
    region.header.rcBound = region.rect;
    const HRESULT dirty = device->Present(nullptr, nullptr, nullptr, reinterpret_cast<const RGNDATA*>(&region));
    if (FAILED(dirty)) {
        problems.add("Present with a dirty region failed with " + Kit::hr(dirty));
    } else {
        if (!screen_shows(rig, 40, 40, 0xFF0000FFu)) {
            problems.add("the dirty region is not updated: (40,40) shows " + screen_text(rig, 40, 40));
        }
        if (!screen_shows(rig, 150, 150, 0xFFFF0000u, 400)) {
            problems.add("a pixel outside the dirty region was updated: (150,150) shows " + screen_text(rig, 150, 150));
        }
    }
    context.measure("window_client", "240x200");
    Kit::check_usable(problems, "after the presents", device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
