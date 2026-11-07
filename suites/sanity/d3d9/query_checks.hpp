#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class QueryChecks {
public:
    [[nodiscard]] static Outcome occlusion(Context& context);

    [[nodiscard]] static Outcome event_timestamp(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
