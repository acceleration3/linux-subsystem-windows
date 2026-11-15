#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class LockChecks {
public:
    [[nodiscard]] static Outcome discard_fresh(Context& context);

    [[nodiscard]] static Outcome nooverwrite_inflight(Context& context);

    [[nodiscard]] static Outcome readonly_and_partial(Context& context);

    [[nodiscard]] static Outcome dirty_regions(Context& context);

    [[nodiscard]] static Outcome errors(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
