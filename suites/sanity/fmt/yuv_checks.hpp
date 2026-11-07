#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class YuvChecks {
public:
    [[nodiscard]] static Outcome yuv_nv12_p010(Context& context);

    [[nodiscard]] static Outcome yuv_video_processing(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
