#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class ResChecks {
public:
    [[nodiscard]] static Outcome pool_usage_matrix(Context& context);

    [[nodiscard]] static Outcome mip_levels_and_sizes(Context& context);

    [[nodiscard]] static Outcome volume_and_cube(Context& context);

    [[nodiscard]] static Outcome managed_roundtrip(Context& context);

    [[nodiscard]] static Outcome priority_and_lod(Context& context);

    [[nodiscard]] static Outcome autogenmipmap(Context& context);

    [[nodiscard]] static Outcome exhaustion(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
