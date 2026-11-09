#include "render/host/image_judge.hpp"

#include "render/common/png.hpp"

#include <cstdio>

namespace lsw::suites::render {

std::string ImageJudge::label(unsigned frame, const std::string& capture) {
    char text[16];
    std::snprintf(text, sizeof text, "f%03u", frame);
    return capture.empty() ? std::string(text) : std::string(text) + "_" + capture;
}

void ImageJudge::compare(Context& context, Problems& problems, const std::string& name, const rr::Image& expected,
                         const rr::Image& actual, const rr::Tolerance& tolerance) {
    rr::Image diff;
    const rr::DiffResult result = rr::ImageDiff::compare(expected, actual, tolerance, &diff);
    context.measure("psnr." + name, result.psnr);
    context.measure("bad_pixels." + name, static_cast<double>(result.bad));
    context.measure("max_delta." + name, result.max_delta);
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

void ImageJudge::compare_frames(Context& context, Problems& problems, const rr::FrameCaptures& expected,
                                const rr::FrameCaptures& actual, const rr::Tolerance& tolerance) {
    for (const auto& [frame, set] : actual) {
        const auto found = expected.find(frame);
        if (found == expected.end()) {
            problems.add("frame " + std::to_string(frame) + " has no reference capture");
            continue;
        }
        for (const auto& [capture, image] : set.items()) {
            const rr::Image* reference = nullptr;
            for (const auto& [ref_label, ref_image] : found->second.items()) {
                if (ref_label == capture) {
                    reference = &ref_image;
                }
            }
            if (reference == nullptr) {
                problems.add(label(frame, capture) + ": no reference capture of that name");
                continue;
            }
            compare(context, problems, label(frame, capture), *reference, image, tolerance);
        }
    }
}

}  // namespace lsw::suites::render
