#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class MapChecks {
public:
    [[nodiscard]] static Outcome persistent(Context& context);

    [[nodiscard]] static Outcome upload_visible(Context& context);

    [[nodiscard]] static Outcome readback_after_fence(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
