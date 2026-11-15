#include "support/sequence_judge.hpp"

#include "support/frame_probe.hpp"

namespace lsw::suites {

SequenceReport SequenceJudge::judge(const std::vector<Sighting>& sightings, double sample_limit_ms) {
    SequenceReport report;
    for (const Sighting& sighting : sightings) {
        if (sighting.value == FrameProbe::kTorn) {
            ++report.torn;
            continue;
        }
        if (sighting.value == FrameProbe::kNone) {
            if (!report.seen.empty()) {
                ++report.unrecognised;
            }
            continue;
        }
        if (report.seen.empty()) {
            report.seen.push_back(sighting.value);
            continue;
        }
        const int previous = report.seen.back();
        if (sighting.value == previous) {
            continue;
        }
        if (sighting.value < previous) {
            if (report.regressions == 0) {
                report.first_regression = "frame " + std::to_string(sighting.value) + " shown after frame " + std::to_string(previous);
            }
            ++report.regressions;
            continue;
        }
        for (int frame = previous + 1; frame < sighting.value; ++frame) {
            if (sighting.gap_ms < sample_limit_ms) {
                report.missing.push_back(frame);
            } else {
                ++report.inconclusive;
            }
        }
        report.seen.push_back(sighting.value);
    }
    return report;
}

std::string SequenceJudge::list(const std::vector<int>& frames) {
    std::string out;
    for (const int frame : frames) {
        out += (out.empty() ? "" : ",") + std::to_string(frame);
    }
    return out.empty() ? "none" : out;
}

std::string SequenceJudge::summary(const SequenceReport& report) {
    return "seen " + std::to_string(report.seen.size()) + " frames, missing [" + list(report.missing) + "], " + std::to_string(report.regressions) + " regressions, " +
           std::to_string(report.torn) + " torn, " + std::to_string(report.unrecognised) + " unrecognised";
}

}  // namespace lsw::suites
