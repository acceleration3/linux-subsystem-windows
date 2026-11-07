#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class CopyChecks {
public:
    [[nodiscard]] static Outcome footprints(Context& context);

    [[nodiscard]] static Outcome region_variants(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
