#pragma once

#include "support/frame_watcher.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
struct SequenceReport {
    std::vector<int> seen;
    std::vector<int> missing;
    int inconclusive = 0;
    int regressions = 0;
    int torn = 0;
    int unrecognised = 0;
    std::string first_regression;
};

class SequenceJudge {
public:
    [[nodiscard]] static SequenceReport judge(const std::vector<Sighting>& sightings, double sample_limit_ms);

    [[nodiscard]] static std::string list(const std::vector<int>& frames);

    [[nodiscard]] static std::string summary(const SequenceReport& report);
};
}  // namespace lsw::suites
