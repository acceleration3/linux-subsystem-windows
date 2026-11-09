#pragma once

#include "core/context.hpp"
#include "render/common/capture_dump.hpp"
#include "render/host/guest_types.hpp"
#include "support/problems.hpp"

#include <string>
#include <vector>

namespace lsw::suites::render {
class ImageJudge {
public:
    [[nodiscard]] static std::string label(unsigned frame, const std::string& capture);

    static void compare(Context& context, Problems& problems, const std::string& label, const rr::Image& expected,
                        const rr::Image& actual, const rr::Tolerance& tolerance, bool uniform_ok,
                        std::vector<rr::CaptureRecord>* records = nullptr);

    static void compare_frames(Context& context, Problems& problems, const rr::CaseSpec& spec, const rr::FrameCaptures& expected,
                               const rr::FrameCaptures& actual, bool oracle = false);
};
}  // namespace lsw::suites::render
