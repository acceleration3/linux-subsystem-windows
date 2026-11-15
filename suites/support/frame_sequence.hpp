#pragma once

#include "support/capture_sample.hpp"

#include <vector>

namespace lsw::suites {
struct SequenceTally {
    std::vector<long long> unwrapped;
    std::vector<int> first_sample;
    int captured = 0;
    int dropped = 0;
    int duplicates = 0;
    int unrecognised = 0;
};

class FrameSequence {
public:
    [[nodiscard]] static std::vector<long long> unwrap(const std::vector<CaptureSample>& samples);

    [[nodiscard]] static SequenceTally tally(const std::vector<CaptureSample>& samples, int first_index, int last_index);
};
}  // namespace lsw::suites
