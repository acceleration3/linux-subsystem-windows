#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class QueryChecks {
public:
    [[nodiscard]] static Outcome occlusion_pipeline(Context& context);

    [[nodiscard]] static Outcome timestamp_calibration(Context& context);

    [[nodiscard]] static Outcome present_queue(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
