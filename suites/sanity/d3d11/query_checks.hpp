#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class QueryChecks {
public:
    [[nodiscard]] static Outcome occlusion_counts(Context& context);

    [[nodiscard]] static Outcome timestamp_disjoint(Context& context);

    [[nodiscard]] static Outcome pipeline_stats(Context& context);

    [[nodiscard]] static Outcome event_and_predication(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
