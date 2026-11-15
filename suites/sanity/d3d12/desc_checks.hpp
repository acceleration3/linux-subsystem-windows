#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class DescChecks {
public:
    [[nodiscard]] static Outcome heaps_and_tables(Context& context);

    [[nodiscard]] static Outcome root_signatures(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
