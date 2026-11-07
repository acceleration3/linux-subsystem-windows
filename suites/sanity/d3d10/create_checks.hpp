#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d10 {
class CreateChecks {
public:
    [[nodiscard]] static Outcome device_flags(Context& context);

    [[nodiscard]] static Outcome levels_101(Context& context);

    [[nodiscard]] static Outcome format_support(Context& context);
};
}  // namespace lsw::suites::sanity::d3d10
