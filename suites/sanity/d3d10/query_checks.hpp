#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d10 {
class QueryChecks {
public:
    [[nodiscard]] static Outcome predicate(Context& context);

    [[nodiscard]] static Outcome clearstate_and_stale(Context& context);
};
}  // namespace lsw::suites::sanity::d3d10
