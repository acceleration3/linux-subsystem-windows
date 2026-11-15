#include "sanity/present/path_checks.hpp"

#include "sanity/present/kit.hpp"

#include <algorithm>
#include <memory>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;
constexpr int kStepFrames = 24;
constexpr int kBurstFirst = 24;
constexpr int kBurstLast = 63;

void check_counters(const std::string& id, const std::vector<FrameStats>& samples, Problems& problems) {
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const FrameStats& a = samples[i - 1];
        const FrameStats& b = samples[i];
        if (b.present_count < a.present_count || b.present_refresh_count < a.present_refresh_count || b.sync_refresh_count < a.sync_refresh_count) {
            problems.add(id + ": the frame statistics went backwards between present " + std::to_string(i - 1) + " and " + std::to_string(i) + " (PresentCount " +
                         std::to_string(a.present_count) + " to " + std::to_string(b.present_count) + ", PresentRefreshCount " + std::to_string(a.present_refresh_count) + " to " +
                         std::to_string(b.present_refresh_count) + ")");
            return;
        }
    }
}

Outcome every_frame(Context& context, const std::string& id, bool statistics_required) {
    std::unique_ptr<PresentPath> path = PresentPath::make(id);
    std::string error;
    switch (Kit::open(*path, kWidth, kHeight, error)) {
        case Opened::Unsupported:
            return Context::skip(error);
        case Opened::Failed:
            return Outcome::fail(id + ": " + error);
        case Opened::Yes:
            break;
    }
    Problems problems;
    const double period = RefreshRate::period_ms();
    context.measure(id + ".refresh_period_ms", period);
    int occluded = 0;
    int total = 0;
    for (int frame = 0; frame < kStepFrames; ++frame) {
        if (FrameProbe::sample(path->client()) == frame) {
            problems.add("frame " + std::to_string(frame) + " is on the screen before it was presented");
            break;
        }
        const PresentResult result = path->present(frame, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
        ++total;
        occluded += result.occluded ? 1 : 0;
        if (!Kit::await_frame(*path, frame, 3000)) {
            problems.add("frame " + std::to_string(frame) + " never became visible after Present");
            break;
        }
    }
    if (!path->drain(error)) {
        problems.add(error);
    }
    std::string detail;
    if (problems.empty() && !Kit::settle(context, Kit::sanitize(id) + ".step_last", *path, FrameImage::render(kStepFrames - 1, kWidth, kHeight), 0, detail)) {
        problems.add("the last stepped frame is not exact: " + detail);
    }
    if (problems.empty()) {
        FrameWatcher watcher;
        watcher.start(path->client());
        ::Sleep(80);
        std::vector<FrameStats> samples;
        std::vector<long long> stamps;
        double slowest = 0.0;
        for (int frame = kBurstFirst; frame <= kBurstLast; ++frame) {
            const long long before = QpcClock::now();
            const PresentResult result = path->present(frame, 1);
            const long long after = QpcClock::now();
            stamps.push_back(after);
            slowest = std::max(slowest, QpcClock::ms(after - before));
            if (!result.ok) {
                problems.add("frame " + std::to_string(frame) + ": " + result.error);
                break;
            }
            ++total;
            occluded += result.occluded ? 1 : 0;
            FrameStats stats;
            if (path->frame_stats(stats)) {
                samples.push_back(stats);
            }
        }
        if (!path->drain(error)) {
            problems.add(error);
        }
        const bool final_shown = Kit::await_frame(*path, kBurstLast, 3000);
        const std::vector<Sighting> sightings = watcher.stop();
        const SequenceReport report = SequenceJudge::judge(sightings, period * 0.75);
        context.measure(id + ".burst.max_present_ms", slowest);
        context.measure(id + ".burst.seen", static_cast<long long>(report.seen.size()));
        context.measure(id + ".burst.inconclusive", report.inconclusive);
        context.measure(id + ".burst.watcher_max_gap_ms", watcher.max_gap_ms());
        if (!final_shown) {
            problems.add("the last frame " + std::to_string(kBurstLast) + " never became visible");
        }
        if (report.regressions != 0) {
            problems.add(report.first_regression);
        }
        if (report.torn != 0 || report.unrecognised != 0) {
            problems.add("the window showed " + std::to_string(report.torn) + " torn and " + std::to_string(report.unrecognised) + " unrecognised images while frames were presented at interval 1");
        }
        if (!report.missing.empty()) {
            problems.add("frames [" + SequenceJudge::list(report.missing) + "] were never shown although the capture sampled every " + std::to_string(static_cast<int>(period * 0.75)) +
                         " ms or faster (" + SequenceJudge::summary(report) + ")");
        }
        if (report.seen.empty() || report.seen.front() > kBurstFirst) {
            problems.add("the capture did not see the frames before " + std::to_string(kBurstFirst));
        }
        if (statistics_required && samples.empty()) {
            problems.add("the present statistics are not available on this path");
        }
        check_counters(id, samples, problems);
        std::uint64_t count = 0;
        if (path->last_present_count(count)) {
            context.measure(id + ".last_present_count", static_cast<long long>(count));
            if (!samples.empty() && samples.back().present_count > count) {
                problems.add("the displayed present count " + std::to_string(samples.back().present_count) + " is above the last present count " + std::to_string(count));
            }
            if (count < static_cast<std::uint64_t>(total)) {
                problems.add("the last present count " + std::to_string(count) + " is below the " + std::to_string(total) + " presents issued");
            }
        } else if (statistics_required) {
            problems.add("GetLastPresentCount failed");
        }
    }
    Kit::report_status(context, id, "interval1", occluded, total);
    path->close();
    return problems.outcome();
}

Outcome order_only(Context& context, const std::string& id, UINT interval_for_burst, bool interval_one_phase) {
    std::unique_ptr<PresentPath> path = PresentPath::make(id);
    std::string error;
    switch (Kit::open(*path, kWidth, kHeight, error)) {
        case Opened::Unsupported:
            return Context::skip(error);
        case Opened::Failed:
            return Outcome::fail(id + ": " + error);
        case Opened::Yes:
            break;
    }
    Problems problems;
    const double period = RefreshRate::period_ms();
    FrameWatcher watcher;
    watcher.start(path->client());
    ::Sleep(80);
    constexpr int kFirstBurst = 47;
    for (int frame = 0; frame <= kFirstBurst; ++frame) {
        const PresentResult result = path->present(frame, interval_for_burst);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
    }
    if (!path->drain(error)) {
        problems.add(error);
    }
    bool final_shown = Kit::await_frame(*path, kFirstBurst, 3000);
    std::vector<Sighting> sightings = watcher.stop();
    SequenceReport report = SequenceJudge::judge(sightings, period * 0.75);
    context.measure(id + ".burst.seen", static_cast<long long>(report.seen.size()));
    if (!final_shown) {
        problems.add("the last presented frame " + std::to_string(kFirstBurst) + " was never shown");
    }
    if (report.regressions != 0) {
        problems.add(report.first_regression);
    }
    if (report.torn != 0 || report.unrecognised != 0) {
        problems.add("the window showed " + std::to_string(report.torn) + " torn and " + std::to_string(report.unrecognised) + " unrecognised images during the burst");
    }
    if (interval_one_phase && problems.empty()) {
        watcher.start(path->client());
        ::Sleep(80);
        for (int frame = kFirstBurst + 1; frame <= kBurstLast; ++frame) {
            const PresentResult result = path->present(frame, 1);
            if (!result.ok) {
                problems.add("frame " + std::to_string(frame) + " at interval 1: " + result.error);
                break;
            }
        }
        if (!path->drain(error)) {
            problems.add(error);
        }
        final_shown = Kit::await_frame(*path, kBurstLast, 3000);
        sightings = watcher.stop();
        report = SequenceJudge::judge(sightings, period * 0.75);
        if (!final_shown) {
            problems.add("the last frame " + std::to_string(kBurstLast) + " presented at interval 1 was never shown");
        }
        if (report.regressions != 0) {
            problems.add(report.first_regression + " at interval 1");
        }
        if (report.torn != 0 || report.unrecognised != 0) {
            problems.add("the window showed torn or unrecognised images at interval 1");
        }
    }
    path->close();
    return problems.outcome();
}

}  // namespace

Outcome PathChecks::d3d9_blt(Context& context) {
    return every_frame(context, "d3d9_blt", false);
}

Outcome PathChecks::d3d9ex_flip(Context& context) {
    return every_frame(context, "d3d9ex_flip", true);
}

Outcome PathChecks::dxgi_blt_sequential(Context& context) {
    return every_frame(context, "dxgi_blt_sequential", false);
}

Outcome PathChecks::dxgi_blt_discard(Context& context) {
    return order_only(context, "dxgi_blt_discard", 0, true);
}

Outcome PathChecks::dxgi_flip_sequential(Context& context) {
    return every_frame(context, "dxgi_flip_sequential", false);
}

Outcome PathChecks::dxgi_flip_discard(Context& context) {
    return order_only(context, "dxgi_flip_discard", 0, true);
}

Outcome PathChecks::d3d12_flip(Context& context) {
    return every_frame(context, "d3d12_flip", false);
}

Outcome PathChecks::gl_swapbuffers(Context& context) {
    std::unique_ptr<PresentPath> probe = PresentPath::make("gl_swapbuffers");
    std::string error;
    if (Kit::open(*probe, kWidth, kHeight, error) == Opened::Yes) {
        const bool controlled = probe->supports_interval(1);
        probe->close();
        if (!controlled) {
            context.note("WGL_EXT_swap_control is not advertised, so the swap interval is the driver default; frames are still checked for order and content");
        }
    } else {
        probe->close();
    }
    return every_frame(context, "gl_swapbuffers", false);
}

Outcome PathChecks::vk_fifo(Context& context) {
    return every_frame(context, "vk_fifo", false);
}

Outcome PathChecks::vk_immediate_mailbox(Context& context) {
    Problems problems;
    int exercised = 0;
    std::string reasons;
    for (const char* id : {"vk_immediate", "vk_mailbox"}) {
        const Outcome outcome = order_only(context, id, 0, false);
        if (outcome.status() == Status::Skip) {
            reasons += std::string(reasons.empty() ? "" : "; ") + id + ": " + outcome.reason();
            continue;
        }
        ++exercised;
        if (outcome.status() != Status::Ok) {
            problems.add(outcome.reason());
        }
    }
    if (exercised == 0) {
        return Context::skip("neither IMMEDIATE nor MAILBOX is reported (" + reasons + ")");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
