#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class ShareChecks {
public:
    [[nodiscard]] static Outcome cross_device(Context& context);

    [[nodiscard]] static Outcome cross_process(Context& context);

    [[nodiscard]] static Outcome d3dimage_pattern(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
