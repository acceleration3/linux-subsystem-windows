#include "sanity/present/timing_checks.hpp"

#include "support/api_policy.hpp"
#include "sanity/present/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/swap_rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kSide = 128;
constexpr int kWarmup = 12;
constexpr int kMeasureMs = 2000;

bool paced_path(const std::string& id) {
    return id != "vk_immediate" && id != "vk_mailbox";
}

std::string percent(double value) {
    return std::to_string(static_cast<int>(value * 1000.0 + 0.5) / 10.0);
}

struct Pacing {
    Kit::Cadence cadence;
    int occluded = 0;
    int presents = 0;
    bool failed = false;
    std::string error;
};

Pacing run_paced(PresentPath& path, UINT interval, int duration_ms, double expected_ms) {
    Pacing out;
    std::vector<long long> stamps;
    const long long start = QpcClock::now();
    int frame = 0;
    while (QpcClock::ms_since(start) < static_cast<double>(duration_ms)) {
        const PresentResult result = path.present(frame % FrameImage::kFrames, interval);
        stamps.push_back(QpcClock::now());
        if (!result.ok) {
            out.failed = true;
            out.error = "frame " + std::to_string(frame) + ": " + result.error;
            break;
        }
        ++out.presents;
        out.occluded += result.occluded ? 1 : 0;
        ++frame;
    }
    std::string drain_error;
    static_cast<void>(path.drain(drain_error));
    out.cadence = Kit::cadence(stamps, static_cast<std::size_t>(kWarmup), expected_ms);
    return out;
}

void record(Context& context, const std::string& id, const std::string& what, const Pacing& pacing) {
    context.measure(id + "." + what + ".mean_ms", pacing.cadence.mean);
    context.measure(id + "." + what + ".stddev_ms", pacing.cadence.deviation);
    context.measure(id + "." + what + ".longest_ms", pacing.cadence.longest);
    context.measure(id + "." + what + ".presents", pacing.presents);
    context.measure(id + "." + what + ".occluded", pacing.occluded);
}

}  // namespace

Outcome TimingChecks::vsync_interval_equals_refresh(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    context.measure("refresh_hz.dwm", RefreshRate::hz());
    context.measure("refresh_hz.settings", RefreshRate::settings_hz());
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
        if (!paced_path(id)) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        std::string error;
        const Opened state = Kit::open(*path, kSide, kSide, error);
        if (state == Opened::Unsupported) {
            context.note(id + " is not exercised: " + error);
            continue;
        }
        if (state == Opened::Failed) {
            problems.add(id + ": " + error);
            continue;
        }
        if (!path->supports_interval(1)) {
            context.note(id + " has no controllable interval 1");
            path->close();
            continue;
        }
        ++exercised;
        const Pacing pacing = run_paced(*path, 1, kMeasureMs + kWarmup * 17, period);
        path->close();
        record(context, id, "interval1", pacing);
        if (pacing.failed) {
            problems.add(id + ": " + pacing.error);
            continue;
        }
        if (pacing.occluded > 0) {
            context.note(id + ": " + std::to_string(pacing.occluded) + " presents reported the window as occluded and are not a valid pacing sample");
            continue;
        }
        if (pacing.cadence.periods_ms.size() < 30) {
            problems.add(id + ": only " + std::to_string(pacing.cadence.periods_ms.size()) + " frame periods were measured");
            continue;
        }
        if (std::fabs(pacing.cadence.mean - period) > period * 0.05) {
            problems.add(id + ": interval 1 averages " + std::to_string(pacing.cadence.mean) + " ms per frame, more than 5 percent from the " + std::to_string(period) + " ms refresh period");
        }
        if (pacing.cadence.deviation > period * 0.25) {
            problems.add(id + ": the frame period deviates by " + std::to_string(pacing.cadence.deviation) + " ms, above 25 percent of the refresh period");
        }
        if (pacing.cadence.outside_fraction > 0.05) {
            problems.add(id + ": " + percent(pacing.cadence.outside_fraction) + " percent of the frame periods fall outside 0.5 to 1.5 refresh periods, above 5 percent");
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path with a controllable interval could be opened");
    }
    return problems.outcome();
}

Outcome TimingChecks::interval_two(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
        if (!paced_path(id)) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        std::string error;
        const Opened state = Kit::open(*path, kSide, kSide, error);
        if (state == Opened::Unsupported) {
            context.note(id + " is not exercised: " + error);
            continue;
        }
        if (state == Opened::Failed) {
            problems.add(id + ": " + error);
            continue;
        }
        if (!path->supports_interval(2)) {
            context.note(id + " has no presentation interval 2");
            path->close();
            continue;
        }
        ++exercised;
        FrameWatcher watcher;
        watcher.start(path->client());
        ::Sleep(80);
        std::vector<long long> stamps;
        int occluded = 0;
        std::string failure;
        for (int frame = 0; frame < 60; ++frame) {
            const PresentResult result = path->present(frame, 2);
            stamps.push_back(QpcClock::now());
            if (!result.ok) {
                failure = "frame " + std::to_string(frame) + ": " + result.error;
                break;
            }
            occluded += result.occluded ? 1 : 0;
        }
        std::string drain_error;
        static_cast<void>(path->drain(drain_error));
        const bool last_shown = Kit::await_frame(*path, 59, 3000);
        const SequenceReport report = SequenceJudge::judge(watcher.stop(), period * 1.5);
        path->close();
        if (!failure.empty()) {
            problems.add(id + ": " + failure);
            continue;
        }
        const Kit::Cadence cadence = Kit::cadence(stamps, 8, period * 2.0);
        context.measure(id + ".interval2.mean_ms", cadence.mean);
        context.measure(id + ".interval2.occluded", occluded);
        if (occluded == 0 && std::fabs(cadence.mean - period * 2.0) > period * 2.0 * 0.10) {
            problems.add(id + ": interval 2 averages " + std::to_string(cadence.mean) + " ms per frame, more than 10 percent from twice the " + std::to_string(period) + " ms refresh period");
        }
        if (!last_shown) {
            problems.add(id + ": the last frame was never shown");
        }
        if (report.regressions != 0 || report.torn != 0 || !report.missing.empty()) {
            problems.add(id + ": at interval 2 every frame must be shown once and in order (" + SequenceJudge::summary(report) + ")");
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path with interval 2 could be opened");
    }
    return problems.outcome();
}

Outcome TimingChecks::immediate_ordering(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
        if (id == "vk_mailbox") {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        std::string error;
        const Opened state = Kit::open(*path, kSide, kSide, error);
        if (state == Opened::Unsupported) {
            context.note(id + " is not exercised: " + error);
            continue;
        }
        if (state == Opened::Failed) {
            problems.add(id + ": " + error);
            continue;
        }
        if (!path->supports_interval(0)) {
            context.note(id + " has no immediate presentation");
            path->close();
            continue;
        }
        ++exercised;
        FrameWatcher watcher;
        watcher.start(path->client());
        ::Sleep(80);
        const long long start = QpcClock::now();
        std::string failure;
        for (int frame = 0; frame < 60; ++frame) {
            const PresentResult result = path->present(frame, 0);
            if (!result.ok) {
                failure = "frame " + std::to_string(frame) + ": " + result.error;
                break;
            }
        }
        const double seconds = QpcClock::ms_since(start) / 1000.0;
        std::string drain_error;
        static_cast<void>(path->drain(drain_error));
        const bool last_shown = Kit::await_frame(*path, 59, 3000);
        const SequenceReport report = SequenceJudge::judge(watcher.stop(), period * 0.75);
        path->close();
        context.measure(id + ".immediate.presents_per_second", 60.0 / seconds);
        if (!failure.empty()) {
            problems.add(id + ": " + failure);
            continue;
        }
        if (!last_shown) {
            problems.add(id + ": the last frame presented at interval 0 was never shown");
        }
        if (report.regressions != 0 || report.torn != 0 || report.unrecognised != 0) {
            problems.add(id + ": interval 0 broke the order (" + SequenceJudge::summary(report) + ")");
        }
    }
    if (ApiPolicy::is_skipped("d3d11")) {
        context.note("the ALLOW_TEARING half needs Direct3D 11 and is excluded by LSW_SUITES_SKIP_APIS");
        if (exercised == 0 && problems.empty()) {
            return Context::skip("no presentation path with interval 0 could be opened");
        }
        return problems.outcome();
    }
    SwapRig rig;
    SwapOptions options;
    options.width = kSide;
    options.height = kSide;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    std::string error;
    if (!rig.open(options, error)) {
        problems.add(error);
        return problems.outcome();
    }
    Com<IDXGIFactory5> factory5 = rig.factory.as<IDXGIFactory5>();
    BOOL tearing = FALSE;
    const bool queried = factory5 && SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)));
    context.measure("allow_tearing.queried", queried);
    context.measure("allow_tearing.supported", tearing != FALSE);
    rig.chain.reset();
    SwapOptions tearing_options = rig.options;
    tearing_options.flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    tearing_options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const HRESULT created = rig.try_create(tearing_options, rig.window.hwnd(), rig.chain);
    context.measure("allow_tearing.create_hr", Kit::hr(created));
    if (queried && tearing != FALSE) {
        if (FAILED(created)) {
            problems.add("CheckFeatureSupport reports PRESENT_ALLOW_TEARING but a flip chain with the ALLOW_TEARING flag cannot be created: " + Kit::hr(created));
        } else {
            D3d11FrameDraw draw;
            FrameWatcher watcher;
            if (!draw.open(rig.device, error)) {
                problems.add(error);
            } else {
                watcher.start(ScreenCapture::client_on_screen(rig.window.hwnd()));
                ::Sleep(80);
                for (int frame = 0; frame < 48; ++frame) {
                    Com<ID3D11RenderTargetView> view = rig.back_view(0);
                    draw.frame(view.get(), kSide, kSide, frame);
                    rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
                    view.reset();
                    const HRESULT presented = rig.chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
                    if (FAILED(presented)) {
                        problems.add("Present(0, ALLOW_TEARING) returned " + Kit::hr(presented));
                        break;
                    }
                }
                static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
                const bool last_shown = Kit::await_frame_in(ScreenCapture::client_on_screen(rig.window.hwnd()), 47, 3000);
                const SequenceReport report = SequenceJudge::judge(watcher.stop(), period * 0.75);
                if (!last_shown || report.regressions != 0 || report.unrecognised != 0) {
                    problems.add("frames presented with ALLOW_TEARING lost their order or the last frame (" + SequenceJudge::summary(report) + ")");
                }
                draw.release();
            }
        }
    } else if (SUCCEEDED(created)) {
        problems.add("a flip chain with the ALLOW_TEARING flag was created although CheckFeatureSupport does not report PRESENT_ALLOW_TEARING");
    } else if (created != DXGI_ERROR_INVALID_CALL && created != E_INVALIDARG && created != DXGI_ERROR_UNSUPPORTED) {
        problems.add("creating a chain with an unsupported ALLOW_TEARING flag returned " + Kit::hr(created) + " instead of a documented invalid-argument code");
    }
    rig.chain.reset();
    SwapOptions plain = rig.options;
    plain.flags = 0;
    if (SUCCEEDED(rig.try_create(plain, rig.window.hwnd(), rig.chain))) {
        const HRESULT presented = rig.chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        context.measure("allow_tearing.present_without_flag_hr", Kit::hr(presented));
        if (presented != DXGI_ERROR_INVALID_CALL) {
            problems.add("Present with DXGI_PRESENT_ALLOW_TEARING on a chain created without the flag returned " + Kit::hr(presented) + " instead of DXGI_ERROR_INVALID_CALL");
        }
    }
    rig.chain.reset();
    static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path with interval 0 could be opened");
    }
    return problems.outcome();
}

Outcome TimingChecks::frame_latency_limits(Context& context) {
    SwapRig rig;
    SwapOptions options;
    options.width = kSide;
    options.height = kSide;
    options.buffers = 3;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    options.flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGISwapChain2> chain2 = rig.chain.as<IDXGISwapChain2>();
    if (!chain2) {
        return Context::skip("IDXGISwapChain2 is not exposed, which the API reports as an older DXGI runtime");
    }
    HANDLE waitable = chain2->GetFrameLatencyWaitableObject();
    if (waitable == nullptr) {
        return Outcome::fail("a chain created with FRAME_LATENCY_WAITABLE_OBJECT returns no waitable object");
    }
    D3d11FrameDraw draw;
    if (!draw.open(rig.device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const UINT latency : {1u, 2u, 3u}) {
        const HRESULT set = chain2->SetMaximumFrameLatency(latency);
        UINT read = 0;
        const HRESULT got = chain2->GetMaximumFrameLatency(&read);
        if (FAILED(set) || FAILED(got) || read != latency) {
            problems.add("SetMaximumFrameLatency(" + std::to_string(latency) + ") returned " + Kit::hr(set) + " and GetMaximumFrameLatency reads " + std::to_string(read));
            continue;
        }
        long long max_ahead = 0;
        std::vector<long long> stamps;
        std::vector<double> waits;
        std::string failure;
        for (int frame = 0; frame < 70; ++frame) {
            const long long before = QpcClock::now();
            if (::WaitForSingleObject(waitable, 2000) != WAIT_OBJECT_0) {
                failure = "the waitable object was not signalled within 2 s before frame " + std::to_string(frame);
                break;
            }
            waits.push_back(QpcClock::ms_since(before));
            Com<ID3D11RenderTargetView> view = rig.back_view(0);
            draw.frame(view.get(), kSide, kSide, frame % FrameImage::kFrames);
            rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
            view.reset();
            const HRESULT presented = rig.chain->Present(1, 0);
            stamps.push_back(QpcClock::now());
            if (FAILED(presented)) {
                failure = "Present returned " + Kit::hr(presented);
                break;
            }
            UINT issued = 0;
            DXGI_FRAME_STATISTICS stats{};
            if (frame >= 6 && SUCCEEDED(rig.chain->GetLastPresentCount(&issued)) && SUCCEEDED(rig.chain->GetFrameStatistics(&stats)) && issued >= stats.PresentCount) {
                max_ahead = std::max(max_ahead, static_cast<long long>(issued - stats.PresentCount));
            }
        }
        if (!failure.empty()) {
            problems.add("latency " + std::to_string(latency) + ": " + failure);
            continue;
        }
        const std::string key = "latency" + std::to_string(latency);
        const Kit::Cadence cadence = Kit::cadence(stamps, 8, RefreshRate::period_ms());
        double wait_sum = 0.0;
        for (const double wait : waits) {
            wait_sum += wait;
        }
        context.measure(key + ".max_frames_ahead", max_ahead);
        context.measure(key + ".mean_period_ms", cadence.mean);
        context.measure(key + ".mean_wait_ms", wait_sum / static_cast<double>(waits.size()));
        if (max_ahead > static_cast<long long>(latency) + 1) {
            problems.add("with a maximum frame latency of " + std::to_string(latency) + " the application ran " + std::to_string(max_ahead) + " frames ahead of the display, above " + std::to_string(latency + 1));
        }
    }
    std::string drain_error;
    static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, drain_error));
    ::CloseHandle(waitable);
    draw.release();
    return problems.outcome();
}

Outcome TimingChecks::present_stats_vs_capture(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    int exercised = 0;
    for (const char* id : {"dxgi_flip_sequential", "d3d12_flip", "d3d9ex_flip"}) {
        if (ApiPolicy::is_skipped(PresentPath::api_of(id))) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        std::string error;
        const Opened state = Kit::open(*path, kSide, kSide, error);
        if (state == Opened::Unsupported) {
            context.note(std::string(id) + " is not exercised: " + error);
            continue;
        }
        if (state == Opened::Failed) {
            problems.add(std::string(id) + ": " + error);
            continue;
        }
        ++exercised;
        const std::string tag = id;
        std::string failure;
        constexpr int kWarm = 5;
        constexpr int kTotal = 40;
        for (int frame = 0; frame < kWarm && failure.empty(); ++frame) {
            const PresentResult result = path->present(frame, 1);
            if (!result.ok) {
                failure = result.error;
            } else if (!Kit::await_frame(*path, frame, 3000)) {
                failure = "frame " + std::to_string(frame) + " never became visible";
            }
        }
        if (!failure.empty()) {
            problems.add(tag + ": " + failure);
            path->close();
            continue;
        }
        ::Sleep(100);
        FrameStats first;
        DWM_TIMING_INFO dwm_first{};
        const bool have_first = path->frame_stats(first);
        const bool have_dwm = RefreshRate::dwm_timing(dwm_first);
        FrameWatcher watcher;
        watcher.start(path->client());
        ::Sleep(40);
        const long long begin = QpcClock::now();
        for (int frame = kWarm; frame < kTotal && failure.empty(); ++frame) {
            const PresentResult result = path->present(frame, 1);
            if (!result.ok) {
                failure = "frame " + std::to_string(frame) + ": " + result.error;
            }
        }
        std::string drain_error;
        static_cast<void>(path->drain(drain_error));
        const bool shown = Kit::await_frame(*path, kTotal - 1, 3000);
        ::Sleep(150);
        const double elapsed = QpcClock::ms_since(begin);
        FrameStats last;
        DWM_TIMING_INFO dwm_last{};
        const bool have_last = path->frame_stats(last);
        const bool have_dwm_last = RefreshRate::dwm_timing(dwm_last);
        const std::vector<Sighting> sightings = watcher.stop();
        std::uint64_t issued = 0;
        const bool counted = path->last_present_count(issued);
        path->close();
        if (!failure.empty()) {
            problems.add(tag + ": " + failure);
            continue;
        }
        if (!shown) {
            problems.add(tag + ": the last frame never became visible");
            continue;
        }
        const SequenceReport report = SequenceJudge::judge(sightings, period * 0.75);
        long long first_seen = 0;
        long long last_seen = 0;
        for (const Sighting& sighting : sightings) {
            if (sighting.value == kWarm) {
                first_seen = sighting.qpc;
            }
            if (sighting.value == kTotal - 1) {
                last_seen = sighting.qpc;
            }
        }
        if (!report.missing.empty() || report.regressions != 0) {
            problems.add(tag + ": the capture lost or reordered presented frames (" + SequenceJudge::summary(report) + ")");
        }
        if (have_first && have_last && counted) {
            const long long frames = static_cast<long long>(last.present_count) - static_cast<long long>(first.present_count);
            const long long refreshes = static_cast<long long>(last.present_refresh_count) - static_cast<long long>(first.present_refresh_count);
            context.measure(tag + ".stats.frames", frames);
            context.measure(tag + ".stats.refreshes", refreshes);
            if (frames != kTotal - kWarm) {
                problems.add(tag + ": the frame statistics count " + std::to_string(frames) + " presented frames between the two samples, the application presented " + std::to_string(kTotal - kWarm));
            }
            if (std::llabs(refreshes - frames) > 1) {
                problems.add(tag + ": interval-1 frames advanced the presented refresh count by " + std::to_string(refreshes) + " for " + std::to_string(frames) + " frames, more than 1 apart");
            }
            if (issued != static_cast<std::uint64_t>(kTotal)) {
                problems.add(tag + ": GetLastPresentCount is " + std::to_string(issued) + " after " + std::to_string(kTotal) + " presents");
            }
            if (last_seen != 0 && first_seen != 0) {
                const double captured = QpcClock::ms(last_seen - first_seen) / period;
                context.measure(tag + ".capture_refreshes", captured);
                if (std::fabs(captured - static_cast<double>(refreshes)) > 2.5) {
                    problems.add(tag + ": the capture spans " + std::to_string(captured) + " refresh periods between the first and last frame, the statistics " + std::to_string(refreshes));
                }
            }
        } else if (std::string(id) == "dxgi_flip_sequential" || std::string(id) == "d3d9ex_flip") {
            problems.add(tag + ": the frame or present statistics are not available");
        }
        if (have_dwm && have_dwm_last) {
            const long long refreshed = static_cast<long long>(dwm_last.cRefresh) - static_cast<long long>(dwm_first.cRefresh);
            const double expected = (elapsed + 100.0) / period;
            context.measure(tag + ".dwm.refreshes", refreshed);
            context.measure(tag + ".dwm.expected_refreshes", expected);
            if (std::fabs(static_cast<double>(refreshed) - expected) > 4.0 + expected * 0.05) {
                problems.add(tag + ": DwmGetCompositionTimingInfo counts " + std::to_string(refreshed) + " refreshes over " + std::to_string(elapsed + 100.0) + " ms, expected about " + std::to_string(expected));
            }
        } else {
            context.note(tag + ": DwmGetCompositionTimingInfo is unavailable");
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no flip presentation path could be opened");
    }
    return problems.outcome();
}

Outcome TimingChecks::high_refresh_rates(Context& context) {
    const std::vector<int> rates = RefreshRate::mode_rates();
    if (rates.size() < 2) {
        return Context::skip("the display offers " + std::to_string(rates.size()) + " refresh rate at the current mode; nothing to switch between");
    }
    Problems problems;
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current);
    const int original = static_cast<int>(current.dmDisplayFrequency);
    for (const int rate : rates) {
        DEVMODEW mode = current;
        mode.dmDisplayFrequency = static_cast<DWORD>(rate);
        mode.dmFields = DM_DISPLAYFREQUENCY | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        const LONG changed = ::ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_FULLSCREEN, nullptr);
        if (changed != DISP_CHANGE_SUCCESSFUL) {
            problems.add(std::to_string(rate) + " Hz is offered by EnumDisplaySettings but ChangeDisplaySettingsEx returned " + std::to_string(changed));
            continue;
        }
        ::Sleep(800);
        const double period = 1000.0 / static_cast<double>(rate);
        std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
        std::string error;
        if (Kit::open(*path, kSide, kSide, error) != Opened::Yes) {
            problems.add(std::to_string(rate) + " Hz: " + error);
            continue;
        }
        FrameWatcher watcher;
        watcher.start(path->client());
        ::Sleep(60);
        std::vector<long long> stamps;
        int frame = 0;
        const long long start = QpcClock::now();
        while (frame < 60 && QpcClock::ms_since(start) < 4000.0) {
            const PresentResult result = path->present(frame, 1);
            stamps.push_back(QpcClock::now());
            if (!result.ok) {
                problems.add(std::to_string(rate) + " Hz: " + result.error);
                break;
            }
            ++frame;
        }
        std::string drain_error;
        static_cast<void>(path->drain(drain_error));
        const bool shown = Kit::await_frame(*path, frame - 1, 3000);
        const SequenceReport report = SequenceJudge::judge(watcher.stop(), period * 0.75);
        path->close();
        const Kit::Cadence cadence = Kit::cadence(stamps, 10, period);
        context.measure(std::to_string(rate) + "hz.mean_ms", cadence.mean);
        context.measure(std::to_string(rate) + "hz.dwm_hz", RefreshRate::hz());
        if (std::fabs(cadence.mean - period) > period * 0.05) {
            problems.add(std::to_string(rate) + " Hz: vsync-paced presents average " + std::to_string(cadence.mean) + " ms, more than 5 percent from " + std::to_string(period));
        }
        if (!shown || !report.missing.empty() || report.regressions != 0) {
            problems.add(std::to_string(rate) + " Hz: frames were lost or reordered (" + SequenceJudge::summary(report) + ")");
        }
    }
    ::ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    ::Sleep(800);
    DEVMODEW after{};
    after.dmSize = sizeof(after);
    ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &after);
    if (static_cast<int>(after.dmDisplayFrequency) != original) {
        problems.add("the display mode was not restored: " + std::to_string(after.dmDisplayFrequency) + " Hz instead of " + std::to_string(original));
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
