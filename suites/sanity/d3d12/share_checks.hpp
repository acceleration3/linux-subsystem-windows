#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class ShareChecks {
public:
    [[nodiscard]] static Outcome heap_resource_nt(Context& context);

    [[nodiscard]] static Outcome keyed_cross_api(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
