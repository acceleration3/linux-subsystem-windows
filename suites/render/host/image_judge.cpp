#include "render/host/image_judge.hpp"

#include "render/common/png.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace lsw::suites::render {

std::string ImageJudge::label(unsigned frame, const std::string& capture) {
    char text[16];
    std::snprintf(text, sizeof text, "f%03u", frame);
    return capture.empty() ? std::string(text) : std::string(text) + "_" + capture;
}

void ImageJudge::compare(Context& context, Problems& problems, const std::string& name, const rr::Image& expected,
                         const rr::Image& actual, const rr::Tolerance& tolerance, bool uniform_ok,
                         std::vector<rr::CaptureRecord>* records) {
    bool uniform = !expected.empty() && !uniform_ok;
    const std::size_t pixel = rr::PixelInfo::bytes(expected.kind());
    for (std::size_t i = pixel; uniform && i < expected.bytes().size(); ++i) {
        uniform = expected.bytes()[i] == expected.bytes()[i % pixel];
    }
    if (uniform) {
        if (records != nullptr) {
            records->back().tolerance = tolerance;
        }
        problems.add(name + ": the reference image is a single value, so the scene drew nothing");
        return;
    }
    rr::Image diff;
    const rr::DiffResult result = rr::ImageDiff::compare(expected, actual, tolerance, &diff);
    context.measure("psnr." + name, result.psnr);
    context.measure("bad_pixels." + name, static_cast<double>(result.bad));
    context.measure("max_delta." + name, result.max_delta);
    context.measure("forgiven." + name, static_cast<double>(result.forgiven));
    if (records != nullptr) {
        records->back().tolerance = tolerance;
        records->back().judged = true;
        records->back().result = result;
    }
    if (result.pass) {
        return;
    }
    problems.add(name + ": " + result.detail);
    const std::filesystem::path dir = context.artifacts_dir();
    const struct {
        const char* suffix;
        const rr::Image* image;
    } files[] = {{".expected.png", &expected}, {".actual.png", &actual}, {".diff.png", &diff}};
    for (const auto& file : files) {
        if (file.image->empty()) {
            continue;
        }
        const std::filesystem::path path = dir / (name + file.suffix);
        if (rr::Png::write(path, *file.image)) {
            context.artifact(path);
        }
    }
}

rr::Image ImageJudge::bracketed(const rr::Image& expected, const rr::Image& low, const rr::Image& high, const rr::Image& actual,
                                const rr::Tolerance& tolerance) {
    const bool shaped = low.width() == expected.width() && low.height() == expected.height() && low.kind() == expected.kind() &&
                        high.width() == expected.width() && high.height() == expected.height() && high.kind() == expected.kind() &&
                        actual.width() == expected.width() && actual.height() == expected.height() && actual.kind() == expected.kind();
    if (!shaped || rr::PixelInfo::is_integer(expected.kind())) {
        return expected;
    }
    const bool byte_class = rr::PixelInfo::channel_class(expected.kind()) == rr::ChannelClass::Unorm8;
    const double slack = byte_class ? static_cast<double>(tolerance.byte_delta) : static_cast<double>(tolerance.absolute);
    rr::Image out = expected;
    const unsigned channels = rr::PixelInfo::channels(expected.kind());
    for (std::uint32_t y = 0; y < expected.height(); ++y) {
        for (std::uint32_t x = 0; x < expected.width(); ++x) {
            for (unsigned c = 0; c < channels; ++c) {
                const double a = low.channel(x, y, c);
                const double b = high.channel(x, y, c);
                const double m = expected.channel(x, y, c);
                const double lo = std::min({a, b, m}) - slack;
                const double hi = std::max({a, b, m}) + slack;
                const double v = actual.channel(x, y, c);
                if (v >= lo && v <= hi) {
                    out.set_channel(x, y, c, v);
                }
            }
        }
    }
    return out;
}

void ImageJudge::compare_frames(Context& context, Problems& problems, const rr::CaseSpec& spec, const rr::FrameCaptures& expected,
                                const rr::FrameCaptures& actual, bool oracle, const rr::FrameCaptures* low, const rr::FrameCaptures* high) {
    const bool dump = rr::CaptureDump::enabled();
    std::vector<rr::CaptureRecord> records;
    std::set<unsigned> frames;
    for (const auto& entry : expected) {
        frames.insert(entry.first);
    }
    for (const auto& entry : actual) {
        frames.insert(entry.first);
    }
    for (unsigned frame : frames) {
        const auto want = expected.find(frame);
        const auto got = actual.find(frame);
        if (want == expected.end() || got == actual.end()) {
            problems.add("frame " + std::to_string(frame) + " is captured by the " + (want == expected.end() ? "scene only" : "reference only"));
            continue;
        }
        std::set<std::string> labels;
        for (const auto& item : want->second.items()) {
            labels.insert(item.first);
        }
        for (const auto& item : got->second.items()) {
            labels.insert(item.first);
        }
        for (const std::string& capture : labels) {
            const rr::Image* reference = nullptr;
            const rr::Image* image = nullptr;
            for (const auto& item : want->second.items()) {
                if (item.first == capture) {
                    reference = &item.second;
                }
            }
            for (const auto& item : got->second.items()) {
                if (item.first == capture) {
                    image = &item.second;
                }
            }
            const std::string name = label(frame, capture);
            if (reference == nullptr || image == nullptr) {
                problems.add(name + ": captured by the " + (reference == nullptr ? "scene only" : "reference only"));
                continue;
            }
            if (dump) {
                rr::CaptureRecord record;
                record.frame = frame;
                record.label = capture;
                record.actual = image;
                record.oracle = oracle ? reference : nullptr;
                records.push_back(record);
            }
            const rr::Tolerance tolerance = spec.resolve(capture, reference->kind());
            rr::Image adjusted;
            const rr::Image* judged_reference = reference;
            if (low != nullptr && high != nullptr) {
                const auto lo_frame = low->find(frame);
                const auto hi_frame = high->find(frame);
                const rr::Image* lo_image = nullptr;
                const rr::Image* hi_image = nullptr;
                if (lo_frame != low->end()) {
                    for (const auto& item : lo_frame->second.items()) {
                        if (item.first == capture) {
                            lo_image = &item.second;
                        }
                    }
                }
                if (hi_frame != high->end()) {
                    for (const auto& item : hi_frame->second.items()) {
                        if (item.first == capture) {
                            hi_image = &item.second;
                        }
                    }
                }
                if (lo_image != nullptr && hi_image != nullptr) {
                    adjusted = bracketed(*reference, *lo_image, *hi_image, *image, tolerance);
                    judged_reference = &adjusted;
                }
            }
            compare(context, problems, name, *judged_reference, *image, tolerance, spec.uniform_allowed(capture), dump ? &records : nullptr);
        }
    }
    if (dump) {
        for (const std::filesystem::path& path :
             rr::CaptureDump::write(context.artifacts_dir(), context.id(), spec, records, "guest", problems.empty() ? "pass" : "fail", true)) {
            context.artifact(path);
        }
    }
}

}  // namespace lsw::suites::render
