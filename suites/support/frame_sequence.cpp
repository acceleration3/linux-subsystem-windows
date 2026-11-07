#include "support/frame_sequence.hpp"

#include "support/frame_image.hpp"

namespace lsw::suites {

std::vector<long long> FrameSequence::unwrap(const std::vector<CaptureSample>& samples) {
    constexpr int kSpan = FrameImage::kFrames;
    constexpr int kBack = 8;
    std::vector<long long> out(samples.size(), -1);
    long long last = -1;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const int raw = samples[i].raw;
        if (raw < 0) {
            continue;
        }
        if (last < 0) {
            last = raw;
        } else {
            int step = (raw - static_cast<int>(last % kSpan) + kSpan) % kSpan;
            if (step > kSpan - kBack) {
                step -= kSpan;
            }
            last += step;
        }
        out[i] = last;
    }
    return out;
}

SequenceTally FrameSequence::tally(const std::vector<CaptureSample>& samples, int first_index, int last_index) {
    SequenceTally out;
    out.unwrapped = unwrap(samples);
    const int span = last_index >= first_index ? last_index - first_index + 1 : 0;
    out.first_sample.assign(static_cast<std::size_t>(span), -1);
    long long previous = -1;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (out.unwrapped[i] < 0) {
            ++out.unrecognised;
            continue;
        }
        if (out.unwrapped[i] == previous) {
            ++out.duplicates;
        }
        previous = out.unwrapped[i];
        const long long index = out.unwrapped[i] - first_index;
        if (index >= 0 && index < span && out.first_sample[static_cast<std::size_t>(index)] < 0) {
            out.first_sample[static_cast<std::size_t>(index)] = static_cast<int>(i);
            ++out.captured;
        }
    }
    out.dropped = span - out.captured;
    return out;
}

}  // namespace lsw::suites
