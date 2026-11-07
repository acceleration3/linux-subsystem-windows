#pragma once

#include "support/capture_sample.hpp"
#include "support/duplication_session.hpp"
#include "support/frame_stats.hpp"
#include "support/present_path.hpp"
#include "support/wgc_session.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct PacedFrame {
    int index = 0;
    long long before = 0;
    long long after = 0;
    double utc_ms = 0.0;
    std::uint64_t present_count = 0;
    bool occluded = false;
};

struct StatSample {
    std::uint64_t present_count = 0;
    std::uint64_t present_refresh = 0;
    std::uint64_t sync_refresh = 0;
    long long sync_qpc = 0;
    long long taken = 0;
};

struct PacedOptions {
    int duration_ms = 3000;
    UINT interval = 1;
    bool wgc = false;
    bool duplication = false;
};

class PacedRun {
public:
    static constexpr int kWarmup = 8;
    static constexpr int kTailMs = 300;

    [[nodiscard]] bool run(PresentPath& path, const PacedOptions& options, std::string& error);

    [[nodiscard]] const std::vector<PacedFrame>& frames() const { return frames_; }

    [[nodiscard]] const std::vector<StatSample>& stats() const { return stats_; }

    [[nodiscard]] const std::vector<CaptureSample>& wgc() const { return wgc_; }

    [[nodiscard]] const std::vector<CaptureSample>& duplication() const { return duplication_; }

    [[nodiscard]] WgcOpen wgc_state() const { return wgc_state_; }

    [[nodiscard]] DuplicationOpen duplication_state() const { return duplication_state_; }

    [[nodiscard]] const std::string& capture_problem() const { return capture_problem_; }

    [[nodiscard]] int occluded() const;

    [[nodiscard]] int last_index() const { return static_cast<int>(frames_.size()) - 1; }

    [[nodiscard]] std::vector<double> displayed_by_stats(double period_ms) const;

    [[nodiscard]] std::vector<double> displayed_by_wgc() const;

    [[nodiscard]] std::vector<double> displayed_by_duplication() const;

    [[nodiscard]] bool stats_usable() const;

private:
    void sample_stats(PresentPath& path);

    std::vector<PacedFrame> frames_;
    std::vector<StatSample> stats_;
    std::vector<CaptureSample> wgc_;
    std::vector<CaptureSample> duplication_;
    WgcOpen wgc_state_ = WgcOpen::Unavailable;
    DuplicationOpen duplication_state_ = DuplicationOpen::Unavailable;
    std::string capture_problem_;
};
}  // namespace lsw::suites
