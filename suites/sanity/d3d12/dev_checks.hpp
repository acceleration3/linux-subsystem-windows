#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class DevChecks {
public:
    [[nodiscard]] static Outcome create_levels(Context& context);

    [[nodiscard]] static Outcome options_consistent(Context& context);

    [[nodiscard]] static Outcome format_support(Context& context);

    [[nodiscard]] static Outcome node_and_uma(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
