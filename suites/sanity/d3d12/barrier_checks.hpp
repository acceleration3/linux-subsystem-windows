#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class BarrierChecks {
public:
    [[nodiscard]] static Outcome transition_uav(Context& context);

    [[nodiscard]] static Outcome enhanced(Context& context);

    [[nodiscard]] static Outcome misuse_tolerance(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
