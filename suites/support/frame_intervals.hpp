#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lsw::suites {
struct IntervalSummary {
    std::size_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double longest = 0.0;
    std::size_t missed = 0;
    double missed_fraction = 0.0;
};

class FrameIntervals {
public:
    static constexpr double kMissedAtPeriods = 1.5;
    static constexpr double kBinPeriods = 0.25;
    static constexpr std::size_t kBins = 16;

    [[nodiscard]] static std::vector<double> deltas(const std::vector<double>& times_ms);

    [[nodiscard]] static double percentile(std::vector<double> values, double fraction);

    [[nodiscard]] static IntervalSummary summarise(const std::vector<double>& intervals_ms, double period_ms);

    [[nodiscard]] static std::vector<std::string> stutter_findings(const std::string& label, const IntervalSummary& summary, double period_ms);

    [[nodiscard]] static std::vector<std::string> band_findings(const std::string& label, const IntervalSummary& summary, double period_ms);

    [[nodiscard]] static std::vector<std::size_t> histogram(const std::vector<double>& intervals_ms, double period_ms);

    [[nodiscard]] static std::string number(double value);
};
}  // namespace lsw::suites
