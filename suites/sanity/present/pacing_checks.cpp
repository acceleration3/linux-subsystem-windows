#include "sanity/present/pacing_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/present/kit.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/frame_intervals.hpp"
#include "support/frame_sequence.hpp"
#include "support/paced_run.hpp"
#include "support/problems.hpp"
#include "support/stress_load.hpp"
#include "support/timing_artifact.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kSide = 128;
constexpr int kRunMs = 3000;
constexpr std::size_t kMinIntervals = 30;
constexpr int kFrameLatency = 3;
constexpr double kBaseMismatchMs = 5000.0;
constexpr double kMissedCompositionFraction = 0.005;
constexpr int kLoadSeconds = 14;
constexpr int kLoadRampMs = 1500;
constexpr const char* kLoadCheck = "sanity.present.timing.pacing_under_gpu_load";
constexpr const char* kParallelCheck = "sanity.present.timing.pacing_with_parallel_presenters";

std::string ms(double value) {
    return FrameIntervals::number(value);
}

bool has_interval_one(const PresentPath& path) {
    return path.supports_interval(1);
}

struct Judged {
    std::string source;
    std::vector<double> intervals;
    IntervalSummary summary;
};

enum class Measured { Done, Skipped, Failed };

void write_histogram(TimingArtifact& histogram, const std::string& id, const std::string& source, const std::vector<double>& intervals, double period) {
    const std::vector<std::size_t> bins = FrameIntervals::histogram(intervals, period);
    for (std::size_t i = 0; i < bins.size(); ++i) {
        const bool overflow = i == FrameIntervals::kBins;
        const double low = static_cast<double>(i) * FrameIntervals::kBinPeriods;
        histogram.row({id, source, FrameIntervals::number(low), overflow ? "inf" : FrameIntervals::number(low + FrameIntervals::kBinPeriods), std::to_string(bins[i])});
    }
}

void write_run(Context& context, const std::string& id, const PacedRun& run, const Judged& judged) {
    TimingArtifact frames(context, "frames_" + id, "index,before_qpc,after_qpc,utc_ms,present_count,occluded");
    for (const PacedFrame& frame : run.frames()) {
        frames.row({std::to_string(frame.index), std::to_string(frame.before), std::to_string(frame.after), FrameIntervals::number(frame.utc_ms), std::to_string(frame.present_count), frame.occluded ? "1" : "0"});
    }
    frames.write();
    TimingArtifact intervals(context, "intervals_" + id, "n,source,interval_ms");
    for (std::size_t i = 0; i < judged.intervals.size(); ++i) {
        intervals.row({std::to_string(i), judged.source, FrameIntervals::number(judged.intervals[i])});
    }
    intervals.write();
}

Measured measure_path(Context& context, PresentPath& path, const std::string& id, double period, TimingArtifact* histogram, Judged& judged, Problems& problems) {
    PacedOptions options;
    options.duration_ms = kRunMs;
    options.interval = has_interval_one(path) ? 1 : 0;
    options.wgc = true;
    PacedRun run;
    std::string error;
    const bool ok = run.run(path, options, error);
    if (!ok) {
        problems.add(id + ": " + error);
        return Measured::Failed;
    }
    if (run.occluded() > 0) {
        context.note(id + ": " + std::to_string(run.occluded()) + " presents reported the window as occluded and are not a valid pacing sample");
        return Measured::Skipped;
    }
    if (run.stats_usable()) {
        judged.source = "frame_statistics";
        judged.intervals = run.displayed_by_stats(period);
    } else if (run.wgc_state() == WgcOpen::Ready && run.capture_problem().empty()) {
        judged.source = "wgc_capture";
        judged.intervals = run.displayed_by_wgc();
    } else {
        context.note(id + " has neither usable frame statistics nor a capture: " + run.capture_problem());
        return Measured::Skipped;
    }
    if (judged.intervals.size() < kMinIntervals) {
        problems.add(id + ": only " + std::to_string(judged.intervals.size()) + " displayed intervals were measured through " + judged.source);
        return Measured::Failed;
    }
    judged.summary = FrameIntervals::summarise(judged.intervals, period);
    write_run(context, id, run, judged);
    if (histogram != nullptr) {
        write_histogram(*histogram, id, judged.source, judged.intervals, period);
    }
    return Measured::Done;
}

void record_summary(Context& context, const std::string& id, const Judged& judged) {
    context.measure(id + ".source", judged.source);
    context.measure(id + ".intervals", static_cast<long long>(judged.summary.count));
    context.measure(id + ".p50_ms", judged.summary.p50);
    context.measure(id + ".p99_ms", judged.summary.p99);
    context.measure(id + ".longest_ms", judged.summary.longest);
    context.measure(id + ".missed_vsyncs", static_cast<long long>(judged.summary.missed));
}

std::string brief(const std::string& id, const Judged& judged) {
    return id + " via " + judged.source + ": n=" + std::to_string(judged.summary.count) + " p50=" + ms(judged.summary.p50) + " p99=" + ms(judged.summary.p99) + " longest=" + ms(judged.summary.longest) + " missed=" + std::to_string(judged.summary.missed);
}

Outcome load_child(Context& context) {
    const std::string role = ChildMode::variable(L"LSW_SUITES_PACING_ROLE");
    const double seconds = std::stod(ChildMode::variable(L"LSW_SUITES_PACING_SECONDS"));
    if (role == "render") {
        return StressLoad::render(context, seconds);
    }
    const std::vector<std::uint64_t> sizes = {4ull << 20, 16ull << 20};
    return StressLoad::roundtrips(context, seconds, sizes);
}

ChildRunOptions load_role(const std::string& role) {
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_PACING_ROLE", role}, {"LSW_SUITES_PACING_SECONDS", std::to_string(kLoadSeconds)}};
    return options;
}

Outcome presenter_child(Context& context) {
    const std::string path_id = ChildMode::variable(L"LSW_SUITES_PACING_PATH");
    HANDLE ready = ::OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_PACING_READY")).c_str());
    HANDLE go = ::OpenEventW(SYNCHRONIZE, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_PACING_GO")).c_str());
    if (ready == nullptr || go == nullptr) {
        return Outcome::fail("the start gate cannot be opened");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make(path_id);
    std::string error;
    const int x = std::stoi(ChildMode::variable(L"LSW_SUITES_PACING_X"));
    if (!path || !path->open(x, 60, kSide, kSide, error)) {
        ::ReleaseSemaphore(ready, 1, nullptr);
        ::CloseHandle(ready);
        ::CloseHandle(go);
        if (path) {
            path->close();
        }
        return error.rfind("unsupported: ", 0) == 0 ? Context::skip(error.substr(13)) : Outcome::fail(path_id + ": " + error);
    }
    ::ReleaseSemaphore(ready, 1, nullptr);
    Problems problems;
    if (::WaitForSingleObject(go, 60000) != WAIT_OBJECT_0) {
        problems.add("the start signal never came");
    } else {
        const double period = RefreshRate::period_ms();
        Judged judged;
        const Measured state = measure_path(context, *path, path_id, period, nullptr, judged, problems);
        if (state == Measured::Done) {
            record_summary(context, path_id, judged);
            context.measure("summary", brief(path_id, judged));
            for (const std::string& finding : FrameIntervals::stutter_findings(path_id, judged.summary, period)) {
                problems.add(finding);
            }
        } else if (state == Measured::Skipped) {
            static_cast<void>(path->drain(error));
            path->close();
            ::CloseHandle(ready);
            ::CloseHandle(go);
            return Context::skip("the process could not measure its pacing");
        }
    }
    static_cast<void>(path->drain(error));
    path->close();
    ::CloseHandle(ready);
    ::CloseHandle(go);
    return problems.outcome();
}

}  // namespace

Outcome PacingChecks::stutter_per_path(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    context.measure("refresh_period_ms", period);
    TimingArtifact histogram(context, "histogram", "path,source,bin_low_periods,bin_high_periods,count");
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
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
        Judged judged;
        const Measured measured = measure_path(context, *path, id, period, &histogram, judged, problems);
        path->close();
        if (measured != Measured::Done) {
            continue;
        }
        ++exercised;
        record_summary(context, id, judged);
        for (const std::string& finding : FrameIntervals::stutter_findings(id + " via " + judged.source, judged.summary, period)) {
            problems.add(finding);
        }
    }
    histogram.write();
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened and measured");
    }
    return problems.outcome();
}

Outcome PacingChecks::pacing_under_gpu_load(Context& context) {
    if (ChildMode::active() && !ChildMode::variable(L"LSW_SUITES_PACING_ROLE").empty()) {
        return load_child(context);
    }
    std::vector<std::string> chosen;
    for (const char* id : {"dxgi_flip_sequential", "vk_fifo"}) {
        for (const std::string& available : PresentPath::ids()) {
            if (available == id) {
                chosen.push_back(id);
            }
        }
    }
    if (chosen.empty()) {
        return Context::skip("neither Direct3D 11 nor Vulkan is enabled");
    }
    const double period = RefreshRate::period_ms();
    context.measure("refresh_period_ms", period);
    CheckResult render;
    CheckResult copies;
    std::thread render_thread([&] { render = ChildRuns::run_self(kLoadCheck, load_role("render")); });
    std::thread copy_thread([&] { copies = ChildRuns::run_self(kLoadCheck, load_role("roundtrips")); });
    ::Sleep(kLoadRampMs);
    Problems problems;
    TimingArtifact histogram(context, "histogram", "path,source,bin_low_periods,bin_high_periods,count");
    int exercised = 0;
    for (const std::string& id : chosen) {
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
        Judged judged;
        const Measured measured = measure_path(context, *path, id, period, &histogram, judged, problems);
        path->close();
        if (measured != Measured::Done) {
            continue;
        }
        ++exercised;
        record_summary(context, id, judged);
        for (const std::string& finding : FrameIntervals::stutter_findings(id + " under load via " + judged.source, judged.summary, period)) {
            problems.add(finding);
        }
    }
    histogram.write();
    render_thread.join();
    copy_thread.join();
    context.measure("render_load.status", Outcome::name(render.status));
    context.measure("copy_load.status", Outcome::name(copies.status));
    if (render.status != Status::Ok) {
        problems.add("render load process: " + std::string(Outcome::name(render.status)) + " " + render.reason);
    }
    if (copies.status != Status::Ok) {
        problems.add("copy load process: " + std::string(Outcome::name(copies.status)) + " " + copies.reason);
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened and measured under load");
    }
    return problems.outcome();
}

Outcome PacingChecks::pacing_with_parallel_presenters(Context& context) {
    if (ChildMode::active() && !ChildMode::variable(L"LSW_SUITES_PACING_PATH").empty()) {
        return presenter_child(context);
    }
    std::vector<std::string> chosen;
    for (const char* id : {"dxgi_flip_sequential", "d3d12_flip", "vk_fifo", "gl_swapbuffers", "d3d9ex_flip"}) {
        for (const std::string& available : PresentPath::ids()) {
            if (available == id && chosen.size() < 4) {
                chosen.push_back(id);
            }
        }
    }
    if (chosen.size() < 2) {
        return Context::skip("fewer than two presentation APIs are enabled; the row needs several processes with different APIs");
    }
    const std::string stem = "Local\\lsw-suites-pacing-" + std::to_string(::GetCurrentProcessId());
    HANDLE ready = ::CreateSemaphoreW(nullptr, 0, 16, Text::wide(stem + "-ready").c_str());
    HANDLE go = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(stem + "-go").c_str());
    if (ready == nullptr || go == nullptr) {
        return Outcome::fail("the start gate cannot be created");
    }
    std::vector<CheckResult> results(chosen.size());
    std::vector<std::thread> runners;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        ChildRunOptions options;
        options.timeout_seconds = 120;
        options.env = {{"LSW_SUITES_PACING_PATH", chosen[i]},
                       {"LSW_SUITES_PACING_X", std::to_string(20 + static_cast<int>(i) * 180)},
                       {"LSW_SUITES_PACING_READY", stem + "-ready"},
                       {"LSW_SUITES_PACING_GO", stem + "-go"}};
        runners.emplace_back([&results, i, options] { results[i] = ChildRuns::run_self(kParallelCheck, options); });
    }
    Problems problems;
    std::size_t arrived = 0;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (::WaitForSingleObject(ready, 60000) != WAIT_OBJECT_0) {
            break;
        }
        ++arrived;
    }
    if (arrived != chosen.size()) {
        problems.add("only " + std::to_string(arrived) + " of " + std::to_string(chosen.size()) + " processes opened their windows");
    }
    DWM_TIMING_INFO before{};
    const bool have_before = RefreshRate::dwm_timing(before);
    ::SetEvent(go);
    for (std::thread& runner : runners) {
        runner.join();
    }
    DWM_TIMING_INFO after{};
    const bool have_after = RefreshRate::dwm_timing(after);
    int exercised = 0;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (results[i].status == Status::Skip) {
            context.note(chosen[i] + " process: " + results[i].reason);
            continue;
        }
        ++exercised;
        context.measure(chosen[i] + ".summary", ChildRuns::text(results[i], "summary"));
        if (results[i].status != Status::Ok) {
            problems.add(chosen[i] + " process: " + results[i].reason);
        }
    }
    ::CloseHandle(ready);
    ::CloseHandle(go);
    context.measure("processes", exercised);
    if (have_before && have_after && after.cRefresh > before.cRefresh) {
        const double refreshes = static_cast<double>(after.cRefresh - before.cRefresh);
        const double missed = static_cast<double>(after.cFramesMissed - before.cFramesMissed);
        context.measure("dwm.refreshes", static_cast<long long>(refreshes));
        context.measure("dwm.frames_missed", static_cast<long long>(missed));
        context.measure("dwm.frames_late", static_cast<long long>(after.cFramesLate - before.cFramesLate));
        context.measure("dwm.frames_dropped", static_cast<long long>(after.cFramesDropped - before.cFramesDropped));
        if (missed > refreshes * kMissedCompositionFraction) {
            problems.add("composition missed " + ms(missed) + " of " + ms(refreshes) + " refreshes while " + std::to_string(exercised) + " processes presented, above 0.5 percent");
        }
    } else {
        context.note("DwmGetCompositionTimingInfo did not report an advancing refresh count; the composition half is not judged");
    }
    if (exercised < 2 && problems.empty()) {
        return Context::skip("fewer than two of the processes could present");
    }
    return problems.outcome();
}

Outcome PacingChecks::present_to_display(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    const double bound = static_cast<double>(kFrameLatency + 1) * period + period;
    context.measure("refresh_period_ms", period);
    context.measure("p99_bound_ms", bound);
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
        if (id == "vk_immediate" || id == "vk_mailbox") {
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
        PacedOptions options;
        options.duration_ms = kRunMs;
        options.interval = path->supports_interval(1) ? 1 : 0;
        options.wgc = true;
        PacedRun run;
        const bool ok = run.run(*path, options, error);
        path->close();
        if (!ok) {
            problems.add(id + ": " + error);
            continue;
        }
        if (run.occluded() > 0) {
            context.note(id + ": " + std::to_string(run.occluded()) + " presents reported the window as occluded and are not a valid latency sample");
            continue;
        }
        std::vector<double> by_stats;
        std::vector<double> by_capture;
        TimingArtifact csv(context, "latency_" + id, "index,present_before_ms,statistics_latency_ms,capture_latency_ms");
        std::vector<std::string> stats_cells(run.frames().size());
        std::vector<std::string> capture_cells(run.frames().size());
        for (const StatSample& stat : run.stats()) {
            for (std::size_t i = static_cast<std::size_t>(PacedRun::kWarmup); i < run.frames().size(); ++i) {
                if (run.frames()[i].present_count != 0 && run.frames()[i].present_count == stat.present_count) {
                    const double shown = QpcClock::ms(stat.sync_qpc) + (static_cast<double>(stat.present_refresh) - static_cast<double>(stat.sync_refresh)) * period;
                    const double latency = shown - QpcClock::ms(run.frames()[i].before);
                    by_stats.push_back(latency);
                    stats_cells[i] = FrameIntervals::number(latency);
                    break;
                }
            }
        }
        if (run.wgc_state() == WgcOpen::Ready) {
            const SequenceTally tally = FrameSequence::tally(run.wgc(), PacedRun::kWarmup, run.last_index());
            for (int index = PacedRun::kWarmup; index <= run.last_index(); ++index) {
                const int sample = tally.first_sample[static_cast<std::size_t>(index - PacedRun::kWarmup)];
                if (sample < 0) {
                    continue;
                }
                const double latency = run.wgc()[static_cast<std::size_t>(sample)].time_ms - QpcClock::ms(run.frames()[static_cast<std::size_t>(index)].before);
                by_capture.push_back(latency);
                capture_cells[static_cast<std::size_t>(index)] = FrameIntervals::number(latency);
            }
        }
        for (std::size_t i = 0; i < run.frames().size(); ++i) {
            csv.row({std::to_string(i), FrameIntervals::number(QpcClock::ms(run.frames()[i].before)), stats_cells[i], capture_cells[i]});
        }
        csv.write();
        const bool use_stats = by_stats.size() >= kMinIntervals;
        const std::vector<double>& chosen = use_stats ? by_stats : by_capture;
        if (use_stats) {
            context.measure(id + ".statistics.latency_p50_ms", FrameIntervals::percentile(by_stats, 0.5));
            context.measure(id + ".statistics.latency_p99_ms", FrameIntervals::percentile(by_stats, 0.99));
        }
        if (by_capture.size() >= kMinIntervals) {
            context.measure(id + ".capture.latency_p50_ms", FrameIntervals::percentile(by_capture, 0.5));
            context.measure(id + ".capture.latency_p99_ms", FrameIntervals::percentile(by_capture, 0.99));
        }
        if (chosen.size() < kMinIntervals) {
            context.note(id + " has neither frame statistics for its present counts nor a capture: " + run.capture_problem());
            continue;
        }
        ++exercised;
        const double p50 = FrameIntervals::percentile(chosen, 0.5);
        const double p99 = FrameIntervals::percentile(chosen, 0.99);
        const std::string source = use_stats ? "frame statistics" : "capture";
        context.measure(id + ".source", source);
        context.measure(id + ".latency_p50_ms", p50);
        context.measure(id + ".latency_p99_ms", p99);
        if (std::fabs(p50) > kBaseMismatchMs) {
            problems.add(id + ": the " + source + " clock differs from the QPC by " + ms(p50) + " ms");
            continue;
        }
        if (p99 > bound) {
            problems.add(id + " via " + source + ": the p99 present-to-display latency is " + ms(p99) + " ms, above " + std::to_string(kFrameLatency + 1) + " queued frames plus one period (" + ms(bound) + " ms)");
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened and measured");
    }
    return problems.outcome();
}

Outcome PacingChecks::mirror_host_cadence(Context& context) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("Direct3D 11 is excluded by LSW_SUITES_SKIP_APIS");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (Kit::open(*path, kSide, kSide, error) != Opened::Yes) {
        return Outcome::fail(error);
    }
    PacedOptions options;
    options.duration_ms = kRunMs;
    PacedRun run;
    Problems problems;
    if (!run.run(*path, options, error)) {
        problems.add(error);
    }
    const int last = run.last_index();
    if (last < 0) {
        path->close();
        return problems.empty() ? Outcome::fail("no frame was presented") : problems.outcome();
    }
    if (!Kit::await_frame(*path, last % FrameImage::kFrames, 3000)) {
        problems.add("the last presented frame never became visible in the guest");
    }
    Json list = Json::array();
    std::string file = "[";
    for (const PacedFrame& frame : run.frames()) {
        Json row = Json::array();
        row.push(frame.index).push(frame.after).push(frame.utc_ms);
        list.push(std::move(row));
        file += (frame.index == 0 ? "\n[" : ",\n[") + std::to_string(frame.index) + "," + std::to_string(frame.after) + "," + FrameIntervals::number(frame.utc_ms) + "]";
    }
    file += "\n]\n";
    TimingArtifact::text(context, "mirror_frames.json", file);
    context.measure("frames", std::move(list));
    context.measure("frame_count", last + 1);
    context.measure("period_ms", RefreshRate::period_ms());
    context.measure("qpc_frequency", QpcClock::frequency());
    context.measure("occluded_presents", run.occluded());
    const RECT area = path->client();
    context.measure("client_rect", std::to_string(area.left) + "," + std::to_string(area.top) + "," + std::to_string(area.right) + "," + std::to_string(area.bottom));
    ::Sleep(1500);
    context.note("the mirror's arrival cadence is judged by the matrix host phase from the frames measure and the remotewin frame timing CSVs");
    path->close();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
