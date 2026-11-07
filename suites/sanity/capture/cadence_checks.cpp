#include "sanity/capture/cadence_checks.hpp"

#include "sanity/present/kit.hpp"
#include "support/api_policy.hpp"
#include "support/frame_intervals.hpp"
#include "support/frame_sequence.hpp"
#include "support/paced_run.hpp"
#include "support/problems.hpp"
#include "support/timing_artifact.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace lsw::suites::sanity::capture {

namespace {

constexpr int kSide = 128;
constexpr int kRunMs = 3000;
constexpr int kDriftMs = 10000;
constexpr std::size_t kMinIntervals = 30;
constexpr double kLatencyPeriods = 4.0;
constexpr double kLatencyExtraMs = 20.0;
constexpr double kBaseMismatchMs = 5000.0;
constexpr long long kMaxLagFrames = 6;
constexpr double kMaxSlopePerSecond = 0.1;

struct ApiPath {
    const char* label;
    const char* id;
};

const ApiPath kPaths[] = {{"d3d11", "dxgi_flip_sequential"}, {"d3d12", "d3d12_flip"}, {"gl", "gl_swapbuffers"}, {"vulkan", "vk_fifo"}, {"d3d9ex", "d3d9ex_flip"}};

std::string ms(double value) {
    return FrameIntervals::number(value);
}

bool enabled(const ApiPath& path) {
    return !ApiPolicy::is_skipped(PresentPath::api_of(path.id));
}

struct Prepared {
    std::unique_ptr<PresentPath> path;
    bool usable = false;
};

Prepared open_path(Context& context, Problems& problems, const ApiPath& entry) {
    Prepared out;
    out.path = PresentPath::make(entry.id);
    std::string error;
    const present::Opened state = present::Kit::open(*out.path, kSide, kSide, error);
    if (state == present::Opened::Unsupported) {
        context.note(std::string(entry.label) + " is not exercised: " + error);
    } else if (state == present::Opened::Failed) {
        problems.add(std::string(entry.label) + ": " + error);
    } else {
        out.usable = true;
    }
    return out;
}

bool run_valid(Context& context, Problems& problems, const ApiPath& entry, PresentPath& path, PacedRun& run, const PacedOptions& options) {
    std::string error;
    if (!run.run(path, options, error)) {
        problems.add(std::string(entry.label) + ": " + error);
        return false;
    }
    if (run.occluded() > 0) {
        context.note(std::string(entry.label) + ": " + std::to_string(run.occluded()) + " presents reported the window as occluded and are not a valid sample");
        return false;
    }
    return true;
}

void write_samples(Context& context, const std::string& name, const std::vector<CaptureSample>& samples, const std::vector<long long>& unwrapped) {
    TimingArtifact csv(context, name, "sample,time_ms,arrival_qpc,raw_frame,frame_index,accumulated");
    for (std::size_t i = 0; i < samples.size(); ++i) {
        csv.row({std::to_string(i), FrameIntervals::number(samples[i].time_ms), std::to_string(samples[i].arrival), std::to_string(samples[i].raw), std::to_string(unwrapped[i]), std::to_string(samples[i].accumulated)});
    }
    csv.write();
}

void write_presents(Context& context, const std::string& name, const PacedRun& run) {
    TimingArtifact csv(context, name, "index,before_qpc,after_qpc,utc_ms,present_count");
    for (const PacedFrame& frame : run.frames()) {
        csv.row({std::to_string(frame.index), std::to_string(frame.before), std::to_string(frame.after), FrameIntervals::number(frame.utc_ms), std::to_string(frame.present_count)});
    }
    csv.write();
}

std::string skip_text(const PacedRun& run) {
    return run.capture_problem();
}

}  // namespace

Outcome CadenceChecks::wgc_cadence_matches_present(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    context.measure("refresh_period_ms", period);
    int exercised = 0;
    for (const ApiPath& entry : kPaths) {
        if (!enabled(entry)) {
            continue;
        }
        Prepared prepared = open_path(context, problems, entry);
        if (!prepared.usable) {
            continue;
        }
        PacedOptions options;
        options.duration_ms = kRunMs;
        options.wgc = true;
        PacedRun run;
        const bool valid = run_valid(context, problems, entry, *prepared.path, run, options);
        prepared.path->close();
        if (run.wgc_state() == WgcOpen::Unavailable) {
            return Context::skip("Windows.Graphics.Capture is not creatable: " + skip_text(run));
        }
        if (run.wgc_state() == WgcOpen::Failed) {
            problems.add(std::string(entry.label) + ": " + skip_text(run));
            continue;
        }
        if (!valid) {
            continue;
        }
        ++exercised;
        const std::string tag = entry.label;
        const SequenceTally tally = FrameSequence::tally(run.wgc(), PacedRun::kWarmup, run.last_index());
        write_samples(context, "wgc_" + tag, run.wgc(), tally.unwrapped);
        write_presents(context, "presents_" + tag, run);
        const std::vector<double> intervals = run.displayed_by_wgc();
        const IntervalSummary summary = FrameIntervals::summarise(intervals, period);
        context.measure(tag + ".interval_p50_ms", summary.p50);
        context.measure(tag + ".interval_p99_ms", summary.p99);
        context.measure(tag + ".interval_longest_ms", summary.longest);
        context.measure(tag + ".presented", run.last_index() + 1 - PacedRun::kWarmup);
        context.measure(tag + ".captured", tally.captured);
        context.measure(tag + ".dropped", tally.dropped);
        context.measure(tag + ".duplicates", tally.duplicates);
        context.measure(tag + ".unrecognised", tally.unrecognised);
        if (!run.capture_problem().empty()) {
            problems.add(tag + ": " + run.capture_problem());
            continue;
        }
        if (intervals.size() < kMinIntervals) {
            problems.add(tag + ": only " + std::to_string(intervals.size()) + " capture intervals were measured");
            continue;
        }
        for (const std::string& finding : FrameIntervals::band_findings(tag, summary, period)) {
            problems.add(finding);
        }
        const int span = run.last_index() + 1 - PacedRun::kWarmup;
        if (span > 0 && static_cast<double>(tally.dropped) > 0.01 * static_cast<double>(span)) {
            problems.add(tag + ": " + std::to_string(tally.dropped) + " of " + std::to_string(span) + " presented frames were never captured, above 1 percent");
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation API could be opened with the APIs enabled");
    }
    return problems.outcome();
}

Outcome CadenceChecks::duplication_cadence(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    context.measure("refresh_period_ms", period);
    int exercised = 0;
    for (const ApiPath& entry : kPaths) {
        if (!enabled(entry)) {
            continue;
        }
        Prepared prepared = open_path(context, problems, entry);
        if (!prepared.usable) {
            continue;
        }
        PacedOptions options;
        options.duration_ms = kRunMs;
        options.duplication = true;
        PacedRun run;
        const bool valid = run_valid(context, problems, entry, *prepared.path, run, options);
        prepared.path->close();
        if (run.duplication_state() == DuplicationOpen::Unavailable) {
            return Context::skip("output duplication is not available: " + skip_text(run));
        }
        if (run.duplication_state() == DuplicationOpen::Failed) {
            problems.add(std::string(entry.label) + ": " + skip_text(run));
            continue;
        }
        if (!valid) {
            continue;
        }
        ++exercised;
        const std::string tag = entry.label;
        const std::vector<CaptureSample>& samples = run.duplication();
        const SequenceTally tally = FrameSequence::tally(samples, PacedRun::kWarmup, run.last_index());
        write_samples(context, "duplication_" + tag, samples, tally.unwrapped);
        write_presents(context, "presents_" + tag, run);
        if (!run.capture_problem().empty()) {
            problems.add(tag + ": " + run.capture_problem());
            continue;
        }
        std::vector<double> normalised;
        for (std::size_t i = static_cast<std::size_t>(PacedRun::kWarmup) + 1; i < samples.size(); ++i) {
            normalised.push_back((samples[i].time_ms - samples[i - 1].time_ms) / static_cast<double>(std::max(1u, samples[i].accumulated)));
        }
        if (normalised.size() < kMinIntervals) {
            problems.add(tag + ": only " + std::to_string(normalised.size()) + " duplicated frames were measured");
            continue;
        }
        const IntervalSummary summary = FrameIntervals::summarise(normalised, period);
        context.measure(tag + ".interval_p50_ms", summary.p50);
        context.measure(tag + ".interval_p99_ms", summary.p99);
        for (const std::string& finding : FrameIntervals::band_findings(tag, summary, period)) {
            problems.add(finding);
        }
        std::size_t first = samples.size();
        std::size_t last = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (tally.unwrapped[i] >= 0) {
                first = std::min(first, i);
                last = i;
            }
        }
        if (first >= last) {
            problems.add(tag + ": no duplicated frame decoded as a presented frame");
            continue;
        }
        long long accumulated = 0;
        for (std::size_t i = first + 1; i <= last; ++i) {
            accumulated += samples[i].accumulated;
        }
        const long long advanced = tally.unwrapped[last] - tally.unwrapped[first];
        const long long allowed = std::max<long long>(2, advanced / 50);
        context.measure(tag + ".accumulated_sum", accumulated);
        context.measure(tag + ".presents_between", advanced);
        context.measure(tag + ".duplicates", tally.duplicates);
        if (std::llabs(accumulated - advanced) > allowed) {
            problems.add(tag + ": AccumulatedFrames sums to " + std::to_string(accumulated) + " over " + std::to_string(advanced) + " presents, more than " + std::to_string(allowed) + " apart");
        }
        const long long first_index = tally.unwrapped[first];
        const long long last_index = tally.unwrapped[last];
        if (first_index < static_cast<long long>(run.frames().size()) && last_index < static_cast<long long>(run.frames().size())) {
            const double present_span = QpcClock::ms(run.frames()[static_cast<std::size_t>(last_index)].after - run.frames()[static_cast<std::size_t>(first_index)].after);
            const double reported_span = samples[last].time_ms - samples[first].time_ms;
            context.measure(tag + ".present_span_ms", present_span);
            context.measure(tag + ".last_present_time_span_ms", reported_span);
            if (std::fabs(present_span - reported_span) > present_span * 0.03 + period * 2.0) {
                problems.add(tag + ": LastPresentTime spans " + ms(reported_span) + " ms while the present QPC stamps span " + ms(present_span) + " ms");
            }
        }
        for (std::size_t i = first; i <= last; ++i) {
            if (tally.unwrapped[i] < 0 || tally.unwrapped[i] >= static_cast<long long>(run.frames().size())) {
                continue;
            }
            const double presented_at = QpcClock::ms(run.frames()[static_cast<std::size_t>(tally.unwrapped[i])].before);
            if (samples[i].time_ms < presented_at - period) {
                problems.add(tag + ": frame " + std::to_string(tally.unwrapped[i]) + " has LastPresentTime " + ms(presented_at - samples[i].time_ms) + " ms before its Present was called");
                break;
            }
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation API could be opened with the APIs enabled");
    }
    return problems.outcome();
}

Outcome CadenceChecks::latency_present_to_capture(Context& context) {
    Problems problems;
    const double period = RefreshRate::period_ms();
    const double bound = kLatencyPeriods * period + kLatencyExtraMs;
    context.measure("refresh_period_ms", period);
    context.measure("p99_bound_ms", bound);
    int exercised = 0;
    for (const ApiPath& entry : kPaths) {
        if (!enabled(entry)) {
            continue;
        }
        Prepared prepared = open_path(context, problems, entry);
        if (!prepared.usable) {
            continue;
        }
        PacedOptions options;
        options.duration_ms = kRunMs;
        options.wgc = true;
        options.duplication = true;
        PacedRun run;
        const bool valid = run_valid(context, problems, entry, *prepared.path, run, options);
        prepared.path->close();
        if (!valid) {
            continue;
        }
        const std::string tag = entry.label;
        if (!run.capture_problem().empty()) {
            context.note(tag + ": " + run.capture_problem());
        }
        write_presents(context, "presents_" + tag, run);
        TimingArtifact csv(context, "latency_" + tag, "index,present_after_ms,wgc_latency_ms,duplication_latency_ms");
        struct Source {
            const char* name;
            const std::vector<CaptureSample>* samples;
            bool available;
        };
        const Source sources[] = {{"wgc", &run.wgc(), run.wgc_state() == WgcOpen::Ready}, {"duplication", &run.duplication(), run.duplication_state() == DuplicationOpen::Ready}};
        std::vector<std::vector<double>> latencies(2);
        std::vector<std::vector<std::string>> cells(static_cast<std::size_t>(run.last_index() + 1), std::vector<std::string>(2));
        for (std::size_t s = 0; s < 2; ++s) {
            if (!sources[s].available) {
                continue;
            }
            const SequenceTally tally = FrameSequence::tally(*sources[s].samples, PacedRun::kWarmup, run.last_index());
            for (int index = PacedRun::kWarmup; index <= run.last_index(); ++index) {
                const int sample = tally.first_sample[static_cast<std::size_t>(index - PacedRun::kWarmup)];
                if (sample < 0) {
                    continue;
                }
                const double after = QpcClock::ms(run.frames()[static_cast<std::size_t>(index)].after);
                const double latency = (*sources[s].samples)[static_cast<std::size_t>(sample)].time_ms - after;
                latencies[s].push_back(latency);
                cells[static_cast<std::size_t>(index)][s] = FrameIntervals::number(latency);
            }
        }
        for (int index = 0; index <= run.last_index(); ++index) {
            csv.row({std::to_string(index), FrameIntervals::number(QpcClock::ms(run.frames()[static_cast<std::size_t>(index)].after)), cells[static_cast<std::size_t>(index)][0], cells[static_cast<std::size_t>(index)][1]});
        }
        csv.write();
        bool judged = false;
        for (std::size_t s = 0; s < 2; ++s) {
            if (!sources[s].available || latencies[s].size() < kMinIntervals) {
                if (sources[s].available) {
                    problems.add(tag + " " + sources[s].name + ": only " + std::to_string(latencies[s].size()) + " presented frames were reported by the capture");
                }
                continue;
            }
            judged = true;
            const double p50 = FrameIntervals::percentile(latencies[s], 0.5);
            const double p99 = FrameIntervals::percentile(latencies[s], 0.99);
            const std::string key = tag + "." + sources[s].name;
            context.measure(key + ".latency_p50_ms", p50);
            context.measure(key + ".latency_p99_ms", p99);
            if (std::fabs(p50) > kBaseMismatchMs) {
                problems.add(key + ": the capture clock differs from the QPC by " + ms(p50) + " ms, the two are not on one time base");
                continue;
            }
            if (p99 >= bound) {
                problems.add(key + ": the p99 present-to-capture latency is " + ms(p99) + " ms, not under 4 refresh periods plus 20 ms (" + ms(bound) + " ms)");
            }
        }
        if (judged) {
            ++exercised;
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("neither Windows.Graphics.Capture nor output duplication could capture any presentation API");
    }
    return problems.outcome();
}

Outcome CadenceChecks::steady_no_drift(Context& context) {
    const ApiPath* chosen = nullptr;
    for (const ApiPath& entry : kPaths) {
        if (enabled(entry)) {
            chosen = &entry;
            break;
        }
    }
    if (chosen == nullptr) {
        return Context::skip("no presentation API is enabled");
    }
    Problems problems;
    Prepared prepared = open_path(context, problems, *chosen);
    if (!prepared.usable) {
        return problems.empty() ? Context::skip("the presentation path is unsupported") : problems.outcome();
    }
    PacedOptions options;
    options.duration_ms = kDriftMs;
    options.wgc = true;
    PacedRun run;
    const bool valid = run_valid(context, problems, *chosen, *prepared.path, run, options);
    prepared.path->close();
    if (run.wgc_state() == WgcOpen::Unavailable) {
        return Context::skip("Windows.Graphics.Capture is not creatable: " + skip_text(run));
    }
    if (!valid || !run.capture_problem().empty()) {
        if (!run.capture_problem().empty()) {
            problems.add(run.capture_problem());
        }
        return problems.outcome();
    }
    const std::vector<CaptureSample>& samples = run.wgc();
    const std::vector<long long> unwrapped = FrameSequence::unwrap(samples);
    TimingArtifact csv(context, "drift_lag", "sample,arrival_s,captured_frame,presented_frame,lag_frames");
    std::vector<double> times;
    std::vector<double> lags;
    const long long origin = run.frames().front().before;
    std::size_t started = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (unwrapped[i] < 0) {
            continue;
        }
        while (started < run.frames().size() && run.frames()[started].before <= samples[i].arrival) {
            ++started;
        }
        const long long presented = static_cast<long long>(started) - 1;
        const long long lag = presented - unwrapped[i];
        const double seconds = QpcClock::ms(samples[i].arrival - origin) / 1000.0;
        csv.row({std::to_string(i), FrameIntervals::number(seconds), std::to_string(unwrapped[i]), std::to_string(presented), std::to_string(lag)});
        if (i >= static_cast<std::size_t>(PacedRun::kWarmup)) {
            times.push_back(seconds);
            lags.push_back(static_cast<double>(lag));
        }
    }
    csv.write();
    if (lags.size() < kMinIntervals) {
        return Outcome::fail("only " + std::to_string(lags.size()) + " captured frames decoded as presented frames in " + std::to_string(kDriftMs / 1000) + " s");
    }
    double mean_t = 0.0;
    double mean_l = 0.0;
    double worst = lags.front();
    double best = lags.front();
    for (std::size_t i = 0; i < lags.size(); ++i) {
        mean_t += times[i];
        mean_l += lags[i];
        worst = std::max(worst, lags[i]);
        best = std::min(best, lags[i]);
    }
    mean_t /= static_cast<double>(lags.size());
    mean_l /= static_cast<double>(lags.size());
    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t i = 0; i < lags.size(); ++i) {
        covariance += (times[i] - mean_t) * (lags[i] - mean_l);
        variance += (times[i] - mean_t) * (times[i] - mean_t);
    }
    const double slope = variance > 0.0 ? covariance / variance : 0.0;
    context.measure("lag_max_frames", worst);
    context.measure("lag_min_frames", best);
    context.measure("lag_mean_frames", mean_l);
    context.measure("lag_slope_frames_per_s", slope);
    context.measure("captured_samples", static_cast<long long>(lags.size()));
    if (best < 0.0) {
        problems.add("a captured frame was numbered ahead of every Present started so far (lag " + ms(best) + ")");
    }
    if (worst > static_cast<double>(kMaxLagFrames)) {
        problems.add("the captured frame trailed the presented frame by " + ms(worst) + " frames, above " + std::to_string(kMaxLagFrames));
    }
    if (std::fabs(slope) > kMaxSlopePerSecond) {
        problems.add("the lag drifts by " + ms(slope) + " frames per second, above " + ms(kMaxSlopePerSecond));
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::capture
