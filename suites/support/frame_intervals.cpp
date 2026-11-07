#include "support/frame_intervals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lsw::suites {

std::vector<double> FrameIntervals::deltas(const std::vector<double>& times_ms) {
    std::vector<double> out;
    for (std::size_t i = 1; i < times_ms.size(); ++i) {
        out.push_back(times_ms[i] - times_ms[i - 1]);
    }
    return out;
}

double FrameIntervals::percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = std::min(values.size() - 1, low + 1);
    return values[low] + (values[high] - values[low]) * (position - static_cast<double>(low));
}

IntervalSummary FrameIntervals::summarise(const std::vector<double>& intervals_ms, double period_ms) {
    IntervalSummary out;
    out.count = intervals_ms.size();
    if (intervals_ms.empty()) {
        return out;
    }
    double sum = 0.0;
    for (const double value : intervals_ms) {
        sum += value;
        out.longest = std::max(out.longest, value);
        if (value >= period_ms * kMissedAtPeriods) {
            ++out.missed;
        }
    }
    out.mean = sum / static_cast<double>(out.count);
    out.p50 = percentile(intervals_ms, 0.5);
    out.p99 = percentile(intervals_ms, 0.99);
    out.missed_fraction = static_cast<double>(out.missed) / static_cast<double>(out.count);
    return out;
}

std::vector<std::string> FrameIntervals::stutter_findings(const std::string& label, const IntervalSummary& summary, double period_ms) {
    std::vector<std::string> out;
    if (summary.missed_fraction > 0.005) {
        out.push_back(label + ": " + std::to_string(summary.missed) + " of " + std::to_string(summary.count) + " displayed intervals span 2 or more refresh periods, above 0.5 percent");
    }
    if (summary.longest >= period_ms * 3.0) {
        out.push_back(label + ": the longest displayed interval is " + number(summary.longest) + " ms, not under 3 refresh periods (" + number(period_ms * 3.0) + " ms)");
    }
    if (summary.p99 >= period_ms * 1.5) {
        out.push_back(label + ": the p99 frame time is " + number(summary.p99) + " ms, not under 1.5 refresh periods (" + number(period_ms * 1.5) + " ms)");
    }
    return out;
}

std::vector<std::string> FrameIntervals::band_findings(const std::string& label, const IntervalSummary& summary, double period_ms) {
    std::vector<std::string> out;
    if (std::fabs(summary.p50 - period_ms) > period_ms * 0.10) {
        out.push_back(label + ": the median interval is " + number(summary.p50) + " ms, more than 10 percent from the " + number(period_ms) + " ms refresh period");
    }
    if (summary.p99 >= period_ms * 2.5) {
        out.push_back(label + ": the p99 interval is " + number(summary.p99) + " ms, not under 2.5 refresh periods (" + number(period_ms * 2.5) + " ms)");
    }
    return out;
}

std::vector<std::size_t> FrameIntervals::histogram(const std::vector<double>& intervals_ms, double period_ms) {
    std::vector<std::size_t> bins(kBins + 1, 0);
    for (const double value : intervals_ms) {
        const double position = std::max(0.0, value) / (period_ms * kBinPeriods);
        bins[std::min<std::size_t>(kBins, static_cast<std::size_t>(position))] += 1;
    }
    return bins;
}

std::string FrameIntervals::number(double value) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.3f", value);
    return text;
}

}  // namespace lsw::suites
