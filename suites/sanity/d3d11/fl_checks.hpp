#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class FlChecks {
public:
    [[nodiscard]] static Outcome requested_levels(Context& context);

    [[nodiscard]] static Outcome checkfeaturesupport_options(Context& context);

    [[nodiscard]] static Outcome format_support_vs_creation(Context& context);

    [[nodiscard]] static Outcome msaa_quality_vs_use(Context& context);

    [[nodiscard]] static Outcome limits_at_level(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
