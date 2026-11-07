#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class ResChecks {
public:
    [[nodiscard]] static Outcome usage_bind_cpu_matrix(Context& context);

    [[nodiscard]] static Outcome immutable_initial(Context& context);

    [[nodiscard]] static Outcome views_matrix(Context& context);

    [[nodiscard]] static Outcome sizes_extremes(Context& context);

    [[nodiscard]] static Outcome exhaustion_and_reclaim(Context& context);

    [[nodiscard]] static Outcome exit_reclaim(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
