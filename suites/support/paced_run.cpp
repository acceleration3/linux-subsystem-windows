#include "support/paced_run.hpp"

#include "support/duplication_collector.hpp"
#include "support/frame_image.hpp"
#include "support/frame_intervals.hpp"
#include "support/qpc_clock.hpp"
#include "support/wgc_collector.hpp"

#include <windows.h>

#include <algorithm>

namespace lsw::suites {

namespace {

constexpr unsigned long long kUnixEpoch100ns = 116444736000000000ull;

double unix_ms(const FILETIME& wall) {
    const unsigned long long ticks = (static_cast<unsigned long long>(wall.dwHighDateTime) << 32) | wall.dwLowDateTime;
    return static_cast<double>(ticks - kUnixEpoch100ns) / 10000.0;
}

}  // namespace

bool PacedRun::run(PresentPath& path, const PacedOptions& options, std::string& error) {
    frames_.clear();
    stats_.clear();
    wgc_.clear();
    duplication_.clear();
    capture_problem_.clear();
    wgc_state_ = WgcOpen::Unavailable;
    duplication_state_ = DuplicationOpen::Unavailable;
    WgcCollector wgc;
    DuplicationCollector duplication;
    std::string detail;
    if (options.wgc) {
        wgc_state_ = wgc.open(path, detail);
        if (wgc_state_ != WgcOpen::Ready) {
            capture_problem_ += "WGC: " + detail + "; ";
        }
    }
    if (options.duplication) {
        detail.clear();
        duplication_state_ = duplication.open(path, detail);
        if (duplication_state_ != DuplicationOpen::Ready) {
            capture_problem_ += "duplication: " + detail + "; ";
        }
    }
    if (wgc_state_ == WgcOpen::Ready) {
        wgc.start();
    }
    if (duplication_state_ == DuplicationOpen::Ready) {
        duplication.start();
    }
    ::Sleep(80);
    bool ok = true;
    const long long start = QpcClock::now();
    int index = 0;
    while (QpcClock::ms_since(start) < static_cast<double>(options.duration_ms)) {
        PacedFrame frame;
        frame.index = index;
        frame.before = QpcClock::now();
        const PresentResult result = path.present(index % FrameImage::kFrames, options.interval);
        frame.after = QpcClock::now();
        FILETIME wall;
        ::GetSystemTimePreciseAsFileTime(&wall);
        frame.utc_ms = unix_ms(wall);
        if (!result.ok) {
            error = "frame " + std::to_string(index) + ": " + result.error;
            ok = false;
            break;
        }
        frame.occluded = result.occluded;
        std::uint64_t count = 0;
        if (path.last_present_count(count)) {
            frame.present_count = count;
        }
        frames_.push_back(frame);
        sample_stats(path);
        ++index;
    }
    std::string drain_error;
    if (!path.drain(drain_error) && ok) {
        error = drain_error;
        ok = false;
    }
    const long long tail = QpcClock::now();
    while (QpcClock::ms_since(tail) < static_cast<double>(kTailMs)) {
        sample_stats(path);
        ::Sleep(2);
    }
    if (wgc_state_ == WgcOpen::Ready) {
        wgc_ = wgc.stop();
        if (!wgc.failure().empty()) {
            capture_problem_ += "WGC: " + wgc.failure() + "; ";
        }
        std::string close_error;
        if (!wgc.close(close_error)) {
            capture_problem_ += "WGC close: " + close_error + "; ";
        }
    }
    if (duplication_state_ == DuplicationOpen::Ready) {
        duplication_ = duplication.stop();
        if (!duplication.failure().empty()) {
            capture_problem_ += "duplication: " + duplication.failure() + "; ";
        }
    }
    return ok;
}

void PacedRun::sample_stats(PresentPath& path) {
    FrameStats stats;
    if (!path.frame_stats(stats)) {
        return;
    }
    if (!stats_.empty() && stats_.back().present_count == stats.present_count) {
        return;
    }
    StatSample sample;
    sample.present_count = stats.present_count;
    sample.present_refresh = stats.present_refresh_count;
    sample.sync_refresh = stats.sync_refresh_count;
    sample.sync_qpc = stats.sync_qpc;
    sample.taken = QpcClock::now();
    stats_.push_back(sample);
}

int PacedRun::occluded() const {
    int count = 0;
    for (const PacedFrame& frame : frames_) {
        count += frame.occluded ? 1 : 0;
    }
    return count;
}

bool PacedRun::stats_usable() const {
    const std::size_t presented = frames_.size() > static_cast<std::size_t>(kWarmup) ? frames_.size() - static_cast<std::size_t>(kWarmup) : 0;
    return presented >= 30 && stats_.size() * 10 >= presented * 6;
}

std::vector<double> PacedRun::displayed_by_stats(double period_ms) const {
    std::vector<double> out;
    for (std::size_t i = static_cast<std::size_t>(kWarmup) + 1; i < stats_.size(); ++i) {
        if (stats_[i].present_count <= stats_[i - 1].present_count || stats_[i].present_refresh < stats_[i - 1].present_refresh) {
            continue;
        }
        const std::uint64_t frames = stats_[i].present_count - stats_[i - 1].present_count;
        const double each = static_cast<double>(stats_[i].present_refresh - stats_[i - 1].present_refresh) * period_ms / static_cast<double>(frames);
        for (std::uint64_t k = 0; k < frames; ++k) {
            out.push_back(each);
        }
    }
    return out;
}

std::vector<double> PacedRun::displayed_by_wgc() const {
    std::vector<double> times;
    for (std::size_t i = static_cast<std::size_t>(kWarmup); i < wgc_.size(); ++i) {
        times.push_back(wgc_[i].time_ms);
    }
    return FrameIntervals::deltas(times);
}

std::vector<double> PacedRun::displayed_by_duplication() const {
    std::vector<double> times;
    for (std::size_t i = static_cast<std::size_t>(kWarmup); i < duplication_.size(); ++i) {
        times.push_back(duplication_[i].time_ms);
    }
    return FrameIntervals::deltas(times);
}

}  // namespace lsw::suites
